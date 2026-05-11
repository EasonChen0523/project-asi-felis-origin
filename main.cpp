/**
 * @file main.cpp
 * @brief ASi Felis Origin — VITURE Luma Ultra Hello World
 *
 * 驗證目標（M1 里程碑）：
 *   1. 連線 Luma Ultra HID（XRDeviceProvider）
 *   2. 確認裝置類型為 XR_DEVICE_TYPE_VITURE_CARINA
 *   3. 啟動 RGB 相機串流（XRCameraProvider）
 *   4. 收到第一幀後存成 PPM 或 JPG
 *   5. 列印 IMU 數據（加速度計 + 陀螺儀）
 *   6. 列印 6DoF Pose（position + quaternion）
 *   7. 正常關閉所有資源
 *
 * 編譯（Windows x86_64 — M2 主開發平台）：
 *   cmake -B build-win -G "Visual Studio 17 2022" -A x64
 *   cmake --build build-win --config Release
 *   .\build-win\Release\asi_hello.exe
 *
 * 編譯（Linux x86_64 — WSL2 / Native）：
 *   mkdir build-wsl && cd build-wsl
 *   cmake ..
 *   cmake --build . -j4
 *   ./asi_hello
 *
 * 編譯（Linux aarch64 — Jetson Orin NX）：
 *   cmake -B build-jetson -DVITURE_PLATFORM=linux_aarch64
 *   cmake --build build-jetson -j6
 */

#include "viture_glasses_provider.h"
#include "viture_camera_provider.h"
#include "viture_device_carina.h"
#include "viture_result.h"

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <fstream>
#include <csignal>

// ── Platform-specific includes ──────────────────────────────
#ifdef _WIN32
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>      // SetConsoleOutputCP for UTF-8
#endif

// ── Luma Ultra 硬體常數 ─────────────────────────────────────
//
// Camera VID/PID: 已從 viture_camera_provider.h API document 確認
//   Luma Cyber / Luma Pro / Luma Ultra : 0x0C45 : 0x636B
//   Beast                              : 0x0C45 : 0x6368
//
static constexpr int LUMA_ULTRA_CAMERA_VID  = 0x0C45;  // confirmed via SDK doc
static constexpr int LUMA_ULTRA_CAMERA_PID  = 0x636B;  // confirmed via SDK doc

// ⚠️  Glasses HID PID: SDK 文件未列出，必須等 Luma Ultra 到貨後查證
//     Linux  : lsusb | grep -iE '0c45|35ca'
//     Windows: Device Manager → "Universal Serial Bus controllers" → 找 VITURE 條目
//
// 使用 0x0000 作為明顯非法的 sentinel 值，原因：
//   xr_device_provider_create() 不會在建構時驗證 PID（runtime 實測證實）。
//   即使傳入無意義的 PID，SDK 還是會配置 controller、啟動 thread pool，
//   直到後面 callback registration 或 USB 通訊時才悄悄失敗。
//   用「看起來合理」的 placeholder（例如某些 AI 生成的 magic number）會在
//   沒裝置時掩蓋根本原因，讓未來的開發者誤判錯誤來源。
//
static constexpr int LUMA_ULTRA_GLASSES_PID = 0x0000;  // SENTINEL: replace on hardware arrival

// ── 全域狀態 ────────────────────────────────────────────────
static std::atomic<bool> g_running{true};
static std::atomic<bool> g_frame_saved{false};

// ── 平台輔助：UTF-8 console 設定 ────────────────────────────
//
// Windows console 預設 codepage 不是 UTF-8，中文/emoji 會顯示亂碼。
// Linux / macOS terminal 預設多為 UTF-8，無需處理。
static void enable_utf8_console()
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
}

// ── 平台輔助：印出 USB 存取問題的疑難排解提示 ───────────────
static void print_usb_troubleshoot_hints(const char* context)
{
#ifdef _WIN32
    fprintf(stderr,
        "        -> Install SpaceWalker for Windows (includes WinUSB driver)\n"
        "        -> Open Device Manager, look for VITURE entries\n"
        "        -> Confirm USB-C cable supports DP Alt Mode\n"
        "        -> context: %s\n", context);
#else
    fprintf(stderr,
        "        -> Confirm glasses are plugged via USB-C\n"
        "        -> Check UVC device exists (v4l2-ctl --list-devices)\n"
        "        -> Check /dev/video* permission (udev rule or sudo)\n"
        "        -> context: %s\n", context);
#endif
}

// ── 簡易 PPM 儲存（無額外依賴，跨平台）──────────────────────
static bool save_ppm(const char* path,
                     const uint8_t* rgb_data,
                     uint32_t width,
                     uint32_t height)
{
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << "P6\n" << width << " " << height << "\n255\n";
    f.write(reinterpret_cast<const char*>(rgb_data),
            static_cast<std::streamsize>(width) * height * 3);
    return f.good();
}

// ── RGB 相機 Callback ───────────────────────────────────────
//
// ⚠️  Thread model: 從 runtime log 觀察，callback 在 SDK 的
//     "USB response monitoring thread" 上呼叫，不是 main thread。
//     存取共享資料必須透過 atomic / mutex（g_frame_saved 已是 atomic）。
//
static void on_rgb_frame(const XRCameraFrame* frame, void* /*user_data*/)
{
    if (g_frame_saved.load()) return;

    printf("[Camera] seq=%u  %ux%u  format=%d  size=%u  ts=%.3f s\n",
           frame->sequence,
           frame->width, frame->height,
           static_cast<int>(frame->format),
           frame->size,
           frame->timestamp * 1e-9);  // ns -> s

    if (frame->format == XR_CAMERA_FORMAT_RGB && frame->data != nullptr) {
        if (save_ppm("hello_frame.ppm", frame->data, frame->width, frame->height)) {
            printf("[Camera] [OK] Saved: hello_frame.ppm (%ux%u RGB)\n",
                   frame->width, frame->height);
            g_frame_saved.store(true);
        }
    } else if (frame->format == XR_CAMERA_FORMAT_MJPEG) {
        std::ofstream f("hello_frame.jpg", std::ios::binary);
        if (f) {
            f.write(reinterpret_cast<const char*>(frame->data),
                    static_cast<std::streamsize>(frame->size));
            printf("[Camera] [OK] Saved: hello_frame.jpg (MJPEG raw, %u bytes)\n",
                   frame->size);
            g_frame_saved.store(true);
        }
    }
}

// ── IMU Callback (SDK USB monitoring thread) ────────────────
static void on_imu(float* imu, double timestamp)
{
    // imu[6] = [ax, ay, az, gx, gy, gz]
    printf("[IMU]  t=%.4f  accel=(%.3f, %.3f, %.3f)  gyro=(%.3f, %.3f, %.3f)\n",
           timestamp,
           imu[0], imu[1], imu[2],
           imu[3], imu[4], imu[5]);
}

// ── Pose Callback (25Hz, SDK USB monitoring thread) ─────────
static void on_pose(float* pose, double timestamp)
{
    // pose[32]: 前 7 個是 [px, py, pz, qw, qx, qy, qz]
    printf("[Pose] t=%.4f  pos=(%.3f, %.3f, %.3f)  quat=(%.3f, %.3f, %.3f, %.3f)\n",
           timestamp,
           pose[0], pose[1], pose[2],
           pose[3], pose[4], pose[5], pose[6]);
}

// ── VSync Callback ──────────────────────────────────────────
static void on_vsync(double timestamp)
{
    (void)timestamp;  // 保留供後續 AR overlay 同步使用
}

// ── 玻璃狀態 Callback ───────────────────────────────────────
static void on_glass_state(int state_id, int value)
{
    printf("[Glass] state_id=%d  value=%d\n", state_id, value);
}

// ── Log Hook ────────────────────────────────────────────────
//
// 觀察：SDK 即使註冊了 log_hook，內部 stdout 仍會自己印一份，
// 導致每條訊息重複。暫時保留供後續整合 spdlog / 日誌檔案使用。
//
static void on_log(int level, const char* tag, const char* message)
{
    static const char* level_str[] = {"NONE", "ERROR", "INFO", "DEBUG"};
    if (level > 0 && level <= 3)
        printf("[SDK/%s/%s] %s\n", level_str[level], tag, message);
}

// ── 信號處理 ────────────────────────────────────────────────
// Windows CRT 不支援 SIGTERM，分平台註冊。
static void on_signal(int) { g_running.store(false); }

static void install_signal_handlers()
{
    std::signal(SIGINT, on_signal);
#ifndef _WIN32
    std::signal(SIGTERM, on_signal);
#endif
}

// ── 主程式 ──────────────────────────────────────────────────
int main()
{
    enable_utf8_console();
    install_signal_handlers();

    printf("=== ASi Felis Origin - VITURE Luma Ultra Hello World ===\n");
#ifdef _WIN32
    printf("Platform: Windows x86_64 (MSVC)\n\n");
#elif defined(__aarch64__) || defined(_M_ARM64)
    printf("Platform: Linux aarch64 (Jetson)\n\n");
#else
    printf("Platform: Linux x86_64\n\n");
#endif

    // ── Sentinel check: PID 必須在到貨後替換 ─────────────────
    if (LUMA_ULTRA_GLASSES_PID == 0x0000) {
        fprintf(stderr,
            "[WARN] LUMA_ULTRA_GLASSES_PID is still the placeholder sentinel (0x0000).\n"
            "       This PID must be replaced with the actual value after Luma Ultra arrives.\n"
            "       Steps to obtain real PID:\n");
        print_usb_troubleshoot_hints("PID discovery");
        fprintf(stderr,
            "       Continuing for SDK link/runtime validation (will fail at USB read).\n\n");
    }

    // ── Step 1: 設定 SDK log ──────────────────────────────────
    xr_device_provider_set_log_level(2);  // 2 = Info (TODO: verify enum in SDK header)
    xr_device_provider_set_log_hook(on_log);

    // ── Step 2: 確認相機 VID/PID 是 SDK 支援的合法組合 ───────
    //
    // ⚠️  is_valid_camera() 只檢查 VID/PID 是否為 SDK 認知的支援裝置，
    //     不檢查裝置是否實際插上 USB。即使眼鏡未連線，此 API 仍會回傳 1。
    //     這是 SDK API 命名上的誤導，不要當作 presence check。
    //
    printf("[Init] Checking camera VID/PID is SDK-supported...\n");
    int cam_valid = xr_camera_provider_is_valid_camera(LUMA_ULTRA_CAMERA_VID,
                                                        LUMA_ULTRA_CAMERA_PID);
    printf("[Init] Camera VID=0x%04X PID=0x%04X SDK-supported=%d (note: not a presence check)\n",
           LUMA_ULTRA_CAMERA_VID, LUMA_ULTRA_CAMERA_PID, cam_valid);

    // ── Step 3: 建立 XRDeviceProvider ─────────────────────────
    //
    // ⚠️  xr_device_provider_create() 不在建構時驗證 PID 或 USB 裝置存在性。
    //     即使裝置未連線或 PID 錯誤，此呼叫仍會回傳有效 handle，
    //     並啟動內部 thread pool (TaskManager + ReceiveQueue + SendQueue +
    //     UsbProtocolController monitoring thread)。
    //     真正的失敗在後續 callback registration 或 camera start 時才會出現。
    //
    printf("[Init] Creating XRDeviceProvider (PID=0x%04X)...\n",
           LUMA_ULTRA_GLASSES_PID);

    XRDeviceProviderHandle dev = xr_device_provider_create(LUMA_ULTRA_GLASSES_PID);
    if (!dev) {
        fprintf(stderr, "[ERROR] xr_device_provider_create returned NULL.\n");
        print_usb_troubleshoot_hints("xr_device_provider_create");
        return 1;
    }

    // ── Step 4: 設定 6DOF mode (必須在 initialize 之前) ──────
    printf("[Init] Setting 6DOF mode...\n");
    xr_device_provider_set_dof_type_carina(dev, 1);  // 1 = 6DOF (TODO: verify enum)

    // ── Step 5: 初始化 ────────────────────────────────────────
    printf("[Init] Initializing XRDeviceProvider...\n");
    int ret = xr_device_provider_initialize(dev, nullptr, nullptr);
    if (ret != VITURE_GLASSES_SUCCESS) {
        fprintf(stderr, "[ERROR] xr_device_provider_initialize failed: %d\n", ret);
        xr_device_provider_destroy(dev);
        return 1;
    }

    // ── Step 6: 確認裝置類型 ──────────────────────────────────
    //
    // 沒裝置時實測會回傳 1（非 CARINA），SDK 預設 fallback 值。
    // 有裝置且 PID 正確時應回傳 XR_DEVICE_TYPE_VITURE_CARINA (2)。
    //
    int dev_type = xr_device_provider_get_device_type(dev);
    printf("[Init] Device type: %d (expected CARINA=%d)\n",
           dev_type, XR_DEVICE_TYPE_VITURE_CARINA);

    if (dev_type != XR_DEVICE_TYPE_VITURE_CARINA) {
        fprintf(stderr, "[WARN] Unexpected device type. "
                        "Either no glasses connected, or PID mismatch.\n");
    }

    // ── Step 7: 註冊 Carina Callbacks ─────────────────────────
    printf("[Init] Registering Carina callbacks...\n");
    ret = xr_device_provider_register_callbacks_carina(
        dev,
        on_pose,    // XRPoseCallback   (25Hz; nullptr to skip)
        on_vsync,   // XRVSyncCallback
        on_imu,     // XRImuCallback
        nullptr     // XRCameraCallback (depth camera; not used in Hello World)
    );
    if (ret != VITURE_GLASSES_SUCCESS) {
        fprintf(stderr, "[WARN] register_callbacks_carina failed: %d "
                        "(common cause: USB read error when no device present)\n", ret);
    }

    // ── Step 8: 註冊玻璃狀態 Callback ─────────────────────────
    xr_device_provider_register_state_callback(dev, on_glass_state);

    // ── Step 9: 啟動 HID 管線 ─────────────────────────────────
    printf("[Init] Starting XRDeviceProvider...\n");
    ret = xr_device_provider_start(dev);
    if (ret != VITURE_GLASSES_SUCCESS) {
        fprintf(stderr, "[ERROR] xr_device_provider_start failed: %d\n", ret);
        xr_device_provider_shutdown(dev);
        xr_device_provider_destroy(dev);
        return 1;
    }

    // ── Step 10: 建立 XRCameraProvider ────────────────────────
    printf("[Camera] Creating XRCameraProvider (VID=0x%04X PID=0x%04X)...\n",
           LUMA_ULTRA_CAMERA_VID, LUMA_ULTRA_CAMERA_PID);

    XRCameraProviderHandle cam = xr_camera_provider_create(
        LUMA_ULTRA_CAMERA_VID,
        LUMA_ULTRA_CAMERA_PID
    );
    if (!cam) {
        fprintf(stderr, "[ERROR] xr_camera_provider_create returned NULL.\n");
        print_usb_troubleshoot_hints("xr_camera_provider_create");
        xr_device_provider_stop(dev);
        xr_device_provider_shutdown(dev);
        xr_device_provider_destroy(dev);
        return 1;
    }

    // ── Step 11: 啟動相機串流 ─────────────────────────────────
    printf("[Camera] Starting stream (1920x1080 @ 30fps)...\n");
    ret = xr_camera_provider_start(cam, on_rgb_frame, nullptr);
    if (ret != VITURE_GLASSES_SUCCESS) {
        fprintf(stderr, "[ERROR] xr_camera_provider_start failed: %d\n"
                        "        Error codes:\n"
                        "        -2 = observed when device not present (semantics TBD)\n"
                        "        -3 = USB_UNAVAILABLE (camera not found)\n"
                        "        -4 = NOT_SUPPORTED   (format negotiation failed)\n"
                        "        -5 = USB_EXEC        (stream start failed)\n"
                        "        -6 = INVALID_STATE   (already streaming)\n",
                ret);
        xr_camera_provider_destroy(cam);
        xr_device_provider_stop(dev);
        xr_device_provider_shutdown(dev);
        xr_device_provider_destroy(dev);
        return 1;
    }

    printf("\n[Running] Waiting for first frame... (Ctrl+C to exit)\n\n");

    // ── 主迴圈：每 0.5 秒查詢一次 6DoF Pose ─────────────────
    int observe_tick = 0;
    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        float pose[7] = {};
        int pose_status = 0;
        int pose_ret = xr_device_provider_get_gl_pose_carina(
            dev,
            pose,
            0,            // predict_time=0: 取當前 pose
            &pose_status
        );

        if (pose_ret == VITURE_GLASSES_SUCCESS) {
            printf("[Pose/Poll] pos=(%.3f, %.3f, %.3f)  quat=(%.3f, %.3f, %.3f, %.3f)  "
                   "status=%s\n",
                   pose[0], pose[1], pose[2],
                   pose[3], pose[4], pose[5], pose[6],
                   pose_status == 0 ? "stable" : "unstable");
        }

        if (g_frame_saved.load()) {
            if (++observe_tick >= 10) {
                printf("\n[Done] Frame saved, IMU/Pose observation complete. Exiting.\n");
                g_running.store(false);
            }
        }
    }

    // ── 清理資源（反向順序）─────────────────────────────────
    printf("\n[Shutdown] Stopping camera stream...\n");
    xr_camera_provider_stop(cam);
    xr_camera_provider_destroy(cam);

    printf("[Shutdown] Stopping device provider...\n");
    xr_device_provider_stop(dev);
    xr_device_provider_shutdown(dev);
    xr_device_provider_destroy(dev);

    printf("[Shutdown] Done. Check hello_frame.ppm or hello_frame.jpg in CWD.\n");
    return 0;
}
