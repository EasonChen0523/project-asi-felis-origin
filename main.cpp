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
 *
 * SDK behavior notes:
 *   See SDK_INTERNALS.md for detailed SDK quirks observed during development.
 *   This file references specific sections (e.g. §8.1) where relevant.
 */

#include "viture_glasses_provider.h"
#include "viture_camera_provider.h"
#include "viture_device_carina.h"
#include "viture_macros_public.h"   // LOG_LEVEL_INFO etc.
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
// 使用 0x0000 作為明顯非法的 sentinel 值。
// 詳見 SDK_INTERNALS.md §8.1: xr_device_provider_create() 不會在建構時驗證
// PID，即使傳入無意義的值，SDK 仍會配置完整 thread pool 並回傳 non-NULL handle，
// 真正的失敗在 USB read 階段才出現。「看起來合理」的 placeholder（例如 AI 生成
// 的 magic number）會掩蓋根本原因，因此選用顯眼的 sentinel 0x0000。
//
static constexpr int LUMA_ULTRA_GLASSES_PID = 0x1104;  // confirmed: VID 0x35CA PID 0x1104
                                                        // via probe_pid (is_product_id_valid +
                                                        // get_market_name = "Luma Ultra") and
                                                        // Device Manager enumeration.

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
// ⚠️  Thread model: callback 在 SDK 的 "dedicated camera thread" 上呼叫，
//     與 device provider 的 "USB monitoring thread"（跑 pose / IMU / VSync
//     callback）是不同 thread。詳見 SDK_INTERNALS.md §5.1.
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
        // NOTE: This branch is dead code under SDK v2.2.1 — the SDK is hard-coded
        // to deliver MJPEG only (see SDK_INTERNALS.md §8.5). XRCameraFormat enum
        // values for RGB/YUYV/NV12/GRAY are reserved for future SDK versions.
        // Kept as defensive code in case future SDK adds format negotiation.
        if (save_ppm("hello_frame.ppm", frame->data, frame->width, frame->height)) {
            printf("[Camera] [OK] Saved: hello_frame.ppm (%ux%u RGB)\n",
                   frame->width, frame->height);
            g_frame_saved.store(true);
        }
    } else if (frame->format == XR_CAMERA_FORMAT_MJPEG) {
        // Expected path under current SDK: always MJPEG @ 1920x1080@30fps.
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
//
// Carina IMU layout: 6 floats [ax, ay, az, gx, gy, gz] (accel-first).
// Note: GEN1/GEN2 layouts are different (10 floats, gyro-first).
// See SDK_INTERNALS.md §6.1 if porting to other VITURE devices.
//
static void on_imu(float* imu, double timestamp)
{
    printf("[IMU]  t=%.4f  accel=(%.3f, %.3f, %.3f)  gyro=(%.3f, %.3f, %.3f)\n",
           timestamp,
           imu[0], imu[1], imu[2],
           imu[3], imu[4], imu[5]);
}

// ── Pose Callback (SDK USB monitoring thread) ───────────────
//
// pose[32]: 前 7 個是 [px, py, pz, qw, qx, qy, qz] (推測).
// 後 25 個 floats 用途未文件化 — 詳見 SDK_INTERNALS.md §9 TBD.
// 一般使用情境用 xr_device_provider_get_gl_pose_carina() 取代此 callback
// 可能更精確（支援 predict_time 補償延遲）。
//
static void on_pose(float* pose, double timestamp)
{
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
//
// state_id 與 value 的合法範圍定義在 viture_protocol_public.h 的
// VITURE_CALLBACK_ID_* 常數。該 header 尚未審閱 — 見 SDK_INTERNALS.md §9 TBD.
//
static void on_glass_state(int state_id, int value)
{
    printf("[Glass] state_id=%d  value=%d\n", state_id, value);
}

// ── Log Hook ────────────────────────────────────────────────
//
// 觀察：SDK 即使註冊了 log_hook，內部 stdout 仍會自己印一份，導致每條
// 訊息重複輸出。這是 SDK 設計而非 bug，無法只關閉內建輸出而保留 hook。
// 詳見 SDK_INTERNALS.md §8.6.
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
    // 用 if constexpr 而非 if，明示這是 compile-time check。當 PID 改成
    // 真值時整段 sentinel 警告區塊會被編譯器完整 dead-code-eliminate，
    // 沒有任何 runtime overhead。同時消除 MSVC /W4 的 C4127 警告。
    if constexpr (LUMA_ULTRA_GLASSES_PID == 0x0000) {
        fprintf(stderr,
            "[WARN] LUMA_ULTRA_GLASSES_PID is still the placeholder sentinel (0x0000).\n"
            "       This PID must be replaced with the actual value after Luma Ultra arrives.\n"
            "       Steps to obtain real PID:\n");
        print_usb_troubleshoot_hints("PID discovery");
        fprintf(stderr,
            "       Continuing for SDK link/runtime validation (will fail at USB read).\n\n");
    }

    // ── Step 1: 設定 SDK log ──────────────────────────────────
    // LOG_LEVEL_INFO = 2, defined in viture_macros_public.h.
    xr_device_provider_set_log_level(LOG_LEVEL_INFO);
    xr_device_provider_set_log_hook(on_log);

    // ── Step 2: 確認相機 VID/PID 是 SDK 支援的合法組合 ───────
    //
    // ⚠️  is_valid_camera() 只檢查 VID/PID 是否為 SDK 認知的支援裝置，
    //     不檢查裝置是否實際插上 USB。即使眼鏡未連線，此 API 仍會回傳 1。
    //     詳見 SDK_INTERNALS.md §8.3.
    //
    printf("[Init] Checking camera VID/PID is SDK-supported...\n");
    int cam_valid = xr_camera_provider_is_valid_camera(LUMA_ULTRA_CAMERA_VID,
                                                        LUMA_ULTRA_CAMERA_PID);
    printf("[Init] Camera VID=0x%04X PID=0x%04X SDK-supported=%d (not a presence check)\n",
           LUMA_ULTRA_CAMERA_VID, LUMA_ULTRA_CAMERA_PID, cam_valid);

    // ── Step 3: 建立 XRDeviceProvider ─────────────────────────
    //
    // ⚠️  xr_device_provider_create() 不在建構時驗證 PID 或 USB 裝置存在性。
    //     即使裝置未連線或 PID 錯誤，此呼叫仍會回傳有效 handle，並啟動內部
    //     thread pool (TaskManager + ReceiveQueue + SendQueue +
    //     UsbProtocolController monitoring thread)。
    //     真正的失敗在後續步驟才會出現。詳見 SDK_INTERNALS.md §8.1.
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
    //
    // is_6dof 是 bool-like flag，合法值只有 0 (3DOF) 和 1 (6DOF)，不是 enum。
    // 詳見 SDK_INTERNALS.md §8.7.
    //
    printf("[Init] Setting 6DOF mode...\n");
    xr_device_provider_set_dof_type_carina(dev, 1);  // 1 = 6DOF (is_6dof flag)

    // ── Step 5: 初始化 ────────────────────────────────────────
    //
    // Error codes (per SDK header):
    //   -1 INVALID_PARAM   null handle / invalid config
    //   -8 CALIB_INIT      calibration initialization failed
    //   -9 SERIAL_FETCH    serial number retrieval failed
    //   -99 UNKNOWN        other error
    //
    printf("[Init] Initializing XRDeviceProvider...\n");
    int ret = xr_device_provider_initialize(dev, nullptr, nullptr);
    if (ret != VITURE_GLASSES_SUCCESS) {
        fprintf(stderr, "[ERROR] xr_device_provider_initialize failed: %d\n", ret);
        xr_device_provider_destroy(dev);
        return 1;
    }

    // ── Step 6: 確認裝置類型 ──────────────────────────────────
    //
    // 沒裝置時實測會回傳 1 (GEN2)，SDK 預設 fallback 值。
    // 有裝置且 PID 正確時應回傳 XR_DEVICE_TYPE_VITURE_CARINA (2)。
    // 詳見 SDK_INTERNALS.md §8.2.
    //
    int dev_type = xr_device_provider_get_device_type(dev);
    printf("[Init] Device type: %d (expected CARINA=%d)\n",
           dev_type, XR_DEVICE_TYPE_VITURE_CARINA);

    if (dev_type != XR_DEVICE_TYPE_VITURE_CARINA) {
        fprintf(stderr, "[WARN] Unexpected device type. "
                        "Either no glasses connected, or PID mismatch.\n");
    }

    // ── Step 7: 註冊 Carina Callbacks ─────────────────────────
    //
    // SDK header 說 "callbacks registered here are ignored for non-Carina
    // devices"，但 runtime 實測沒 Carina 時實際是 return -1 (INVALID_PARAM)，
    // 不是 silently ignored。詳見 SDK_INTERNALS.md §8.8.
    //
    printf("[Init] Registering Carina callbacks...\n");
    ret = xr_device_provider_register_callbacks_carina(
        dev,
        on_pose,    // XRPoseCallback
        on_vsync,   // XRVSyncCallback
        on_imu,     // XRImuCallback
        nullptr     // XRCameraCallback (stereo depth; not used in Hello World)
    );
    if (ret != VITURE_GLASSES_SUCCESS) {
        fprintf(stderr, "[WARN] register_callbacks_carina failed: %d "
                        "(likely cause: device_type is not CARINA)\n", ret);
    }

    // ── Step 8: 註冊玻璃狀態 Callback ─────────────────────────
    xr_device_provider_register_state_callback(dev, on_glass_state);

    // ── Step 9: 啟動 HID 管線 ─────────────────────────────────
    //
    // Error codes (per SDK header):
    //   -1 INVALID_PARAM   null handle
    //   -99 UNKNOWN        other error
    //
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
    //
    // Camera is hard-coded to 1920x1080 @ 30fps MJPEG.
    // Error codes (per SDK header for xr_camera_provider_start):
    //   -1  INVALID_PARAM   invalid handle
    //   -2  USB_UNAVAILABLE camera device not found or failed to open
    //   -3  USB_EXEC        failed to start streaming
    //   -4  NOT_SUPPORTED   failed to negotiate stream format
    //   -10 INVALID_STATE   already streaming
    // Other error codes from viture_result.h (e.g. -5 NO_DATA, -7 DEVICE_REJECTED)
    // are NOT documented as possible returns from this specific API.
    //
    printf("[Camera] Starting stream (1920x1080 @ 30fps, MJPEG)...\n");
    ret = xr_camera_provider_start(cam, on_rgb_frame, nullptr);
    if (ret != VITURE_GLASSES_SUCCESS) {
        fprintf(stderr, "[ERROR] xr_camera_provider_start failed: %d\n"
                        "        Possible return codes for this API:\n"
                        "        -1  INVALID_PARAM    invalid handle\n"
                        "        -2  USB_UNAVAILABLE  camera device not found or failed to open\n"
                        "        -3  USB_EXEC         failed to start streaming\n"
                        "        -4  NOT_SUPPORTED    failed to negotiate stream format\n"
                        "        -10 INVALID_STATE    already streaming\n"
                        "        See SDK_INTERNALS.md §7 for full error code reference.\n",
                ret);
        xr_camera_provider_destroy(cam);
        xr_device_provider_stop(dev);
        xr_device_provider_shutdown(dev);
        xr_device_provider_destroy(dev);
        return 1;
    }

    printf("\n[Running] Waiting for first frame... (Ctrl+C to exit)\n\n");

    // ── 主迴圈：每 0.5 秒查詢一次 6DoF Pose ─────────────────
    //
    // get_gl_pose_carina 的 predict_time 單位是 nanoseconds（雖然 type 是
    // double，這違反直覺）。傳 0 表示「取目前 pose」。
    // 對 forward prediction 範例見 SDK_INTERNALS.md §8.4.
    //
    int observe_tick = 0;
    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        float pose[7] = {};
        int pose_status = 0;
        int pose_ret = xr_device_provider_get_gl_pose_carina(
            dev,
            pose,
            0.0,          // predict_time = 0 ns: current pose, no prediction
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
