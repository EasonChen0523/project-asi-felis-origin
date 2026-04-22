/**
 * @file main.cpp
 * @brief ASi Felis Origin — VITURE Luma Ultra Hello World
 *
 * 驗證目標（M1 里程碑）：
 *   1. 連線 Luma Ultra HID（XRDeviceProvider）
 *   2. 確認裝置類型為 XR_DEVICE_TYPE_VITURE_CARINA
 *   3. 啟動 RGB 相機串流（XRCameraProvider）
 *   4. 收到第一幀後存成 PNG（hello_frame.png）
 *   5. 列印 IMU 數據（加速度計 + 陀螺儀）
 *   6. 列印 6DoF Pose（position + quaternion）
 *   7. 正常關閉所有資源
 *
 * 編譯（x86_64 開發機）：
 *   mkdir build && cd build
 *   cmake .. -DVITURE_ARCH=x86_64
 *   cmake --build . -j4
 *   ./asi_hello
 *
 * 編譯（Jetson Orin NX, arm64）：
 *   cmake .. -DVITURE_ARCH=arm64
 *   cmake --build . -j6
 */

#include "viture_glasses_provider.h"
#include "viture_camera_provider.h"
#include "viture_device_carina.h"
#include "viture_result.h"

#include <cstdio>
#include <cstring>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <fstream>
#include <csignal>

// ── Luma Ultra 硬體常數（從 viture_camera_provider.h 文件確認）──
static constexpr int LUMA_ULTRA_GLASSES_PID = 0x6272;  // HID PID（lsusb 到貨後確認）
static constexpr int LUMA_ULTRA_CAMERA_VID  = 0x0C45;  // UVC VID（header 文件確認）
static constexpr int LUMA_ULTRA_CAMERA_PID  = 0x636B;  // UVC PID（header 文件確認）

// ── 全域狀態 ────────────────────────────────────────────────
static std::atomic<bool> g_running{true};
static std::atomic<bool> g_frame_saved{false};

// ── 簡易 PPM 儲存（無額外依賴）──────────────────────────────
// SDK 支援 XR_CAMERA_FORMAT_RGB，直接存 PPM 格式（可用 GIMP / eog 開啟）
static bool save_ppm(const char* path,
                     const uint8_t* rgb_data,
                     uint32_t width,
                     uint32_t height)
{
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    // PPM header
    f << "P6\n" << width << " " << height << "\n255\n";
    f.write(reinterpret_cast<const char*>(rgb_data), width * height * 3);
    return f.good();
}

// ── RGB 相機 Callback（camera thread @ 30Hz）────────────────
static void on_rgb_frame(const XRCameraFrame* frame, void* /*user_data*/)
{
    if (g_frame_saved.load()) return;

    printf("[Camera] seq=%u  %ux%u  format=%d  size=%u  ts=%.3f s\n",
           frame->sequence,
           frame->width, frame->height,
           (int)frame->format,
           frame->size,
           frame->timestamp * 1e-9);  // 奈秒 → 秒

    // 只在收到 RGB 格式時儲存
    if (frame->format == XR_CAMERA_FORMAT_RGB && frame->data != nullptr) {
        if (save_ppm("hello_frame.ppm", frame->data, frame->width, frame->height)) {
            printf("[Camera] ✅ Saved: hello_frame.ppm (%ux%u RGB)\n",
                   frame->width, frame->height);
            g_frame_saved.store(true);
        }
    } else if (frame->format == XR_CAMERA_FORMAT_MJPEG) {
        // SDK 以 MJPEG 傳遞時，直接存原始 JPEG 供驗證
        std::ofstream f("hello_frame.jpg", std::ios::binary);
        if (f) {
            f.write(reinterpret_cast<const char*>(frame->data), frame->size);
            printf("[Camera] ✅ Saved: hello_frame.jpg (MJPEG raw, %u bytes)\n", frame->size);
            g_frame_saved.store(true);
        }
    }
}

// ── IMU Callback ────────────────────────────────────────────
static void on_imu(float* imu, double timestamp)
{
    // imu[6] = [ax, ay, az, gx, gy, gz]
    printf("[IMU]  t=%.4f  accel=(%.3f, %.3f, %.3f)  gyro=(%.3f, %.3f, %.3f)\n",
           timestamp,
           imu[0], imu[1], imu[2],
           imu[3], imu[4], imu[5]);
}

// ── Pose Callback（25Hz，通常直接用 get_gl_pose 更好）────────
static void on_pose(float* pose, double timestamp)
{
    // pose[32]：前 7 個是 [px, py, pz, qw, qx, qy, qz]
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

// ── 玻璃狀態 Callback（亮度、音量等）────────────────────────
static void on_glass_state(int state_id, int value)
{
    printf("[Glass] state_id=%d  value=%d\n", state_id, value);
}

// ── Log Hook（SDK 內部日誌導向 stdout）──────────────────────
static void on_log(int level, const char* tag, const char* message)
{
    const char* level_str[] = {"NONE", "ERROR", "INFO", "DEBUG"};
    if (level > 0 && level <= 3)
        printf("[SDK/%s/%s] %s\n", level_str[level], tag, message);
}

// ── 信號處理（Ctrl+C 優雅退出）──────────────────────────────
static void on_signal(int) { g_running.store(false); }

// ── 主程式 ──────────────────────────────────────────────────
int main()
{
    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    printf("=== ASi Felis Origin — VITURE Luma Ultra Hello World ===\n\n");

    // ── Step 1：設定 SDK log ──────────────────────────────────
    xr_device_provider_set_log_level(2);  // 2 = Info
    xr_device_provider_set_log_hook(on_log);

    // ── Step 2：確認 Luma Ultra PID 有效 ─────────────────────
    // 注意：LUMA_ULTRA_GLASSES_PID 需要 lsusb 到貨後確認，這裡先用 0
    // 可用 xr_device_provider_is_product_id_valid() 掃描有效 PID
    printf("[Init] Checking camera VID/PID validity...\n");
    int cam_valid = xr_camera_provider_is_valid_camera(LUMA_ULTRA_CAMERA_VID,
                                                        LUMA_ULTRA_CAMERA_PID);
    printf("[Init] Camera VID=0x%04X PID=0x%04X valid=%d\n",
           LUMA_ULTRA_CAMERA_VID, LUMA_ULTRA_CAMERA_PID, cam_valid);

    // ── Step 3：建立 XRDeviceProvider（HID 介面）─────────────
    // ⚠️  LUMA_ULTRA_GLASSES_PID 需到貨後用 lsusb 確認再填入
    printf("[Init] Creating XRDeviceProvider (PID=0x%04X)...\n",
           LUMA_ULTRA_GLASSES_PID);

    XRDeviceProviderHandle dev = xr_device_provider_create(LUMA_ULTRA_GLASSES_PID);
    if (!dev) {
        fprintf(stderr, "[ERROR] xr_device_provider_create failed.\n"
                        "        → 確認眼鏡已插上 USB-C\n"
                        "        → 確認 LUMA_ULTRA_GLASSES_PID 正確（lsusb 查詢）\n"
                        "        → 確認有 USB HID 存取權限（Linux: udev rule 或 sudo）\n");
        return 1;
    }

    // ── Step 4：確認裝置類型為 CARINA ─────────────────────────
    // 必須在 initialize 之前設定 DOF 類型
    printf("[Init] Setting 6DOF mode...\n");
    xr_device_provider_set_dof_type_carina(dev, 1);  // 1 = 6DOF

    // ── Step 5：初始化 ────────────────────────────────────────
    printf("[Init] Initializing XRDeviceProvider...\n");
    int ret = xr_device_provider_initialize(dev, nullptr, nullptr);
    if (ret != VITURE_GLASSES_SUCCESS) {
        fprintf(stderr, "[ERROR] xr_device_provider_initialize failed: %d\n", ret);
        xr_device_provider_destroy(dev);
        return 1;
    }

    // ── Step 6：確認裝置類型 ──────────────────────────────────
    int dev_type = xr_device_provider_get_device_type(dev);
    printf("[Init] Device type: %d (expected CARINA=%d)\n",
           dev_type, XR_DEVICE_TYPE_VITURE_CARINA);

    if (dev_type != XR_DEVICE_TYPE_VITURE_CARINA) {
        fprintf(stderr, "[WARN] Unexpected device type. Carina callbacks may not work.\n");
    }

    // ── Step 7：註冊 Carina Callbacks ─────────────────────────
    printf("[Init] Registering Carina callbacks...\n");
    ret = xr_device_provider_register_callbacks_carina(
        dev,
        on_pose,    // XRPoseCallback   (25Hz，可傳 nullptr 不用)
        on_vsync,   // XRVSyncCallback
        on_imu,     // XRImuCallback
        nullptr     // XRCameraCallback（深度相機，此 Hello World 不用）
    );
    if (ret != VITURE_GLASSES_SUCCESS) {
        fprintf(stderr, "[WARN] register_callbacks_carina failed: %d\n", ret);
    }

    // ── Step 8：註冊玻璃狀態 Callback ─────────────────────────
    xr_device_provider_register_state_callback(dev, on_glass_state);

    // ── Step 9：啟動 HID 管線 ─────────────────────────────────
    printf("[Init] Starting XRDeviceProvider...\n");
    ret = xr_device_provider_start(dev);
    if (ret != VITURE_GLASSES_SUCCESS) {
        fprintf(stderr, "[ERROR] xr_device_provider_start failed: %d\n", ret);
        xr_device_provider_shutdown(dev);
        xr_device_provider_destroy(dev);
        return 1;
    }

    // ── Step 10：建立 XRCameraProvider（UVC 介面）────────────
    printf("[Camera] Creating XRCameraProvider (VID=0x%04X PID=0x%04X)...\n",
           LUMA_ULTRA_CAMERA_VID, LUMA_ULTRA_CAMERA_PID);

    XRCameraProviderHandle cam = xr_camera_provider_create(
        LUMA_ULTRA_CAMERA_VID,
        LUMA_ULTRA_CAMERA_PID
    );
    if (!cam) {
        fprintf(stderr, "[ERROR] xr_camera_provider_create failed.\n"
                        "        → 確認 UVC 裝置存在（v4l2-ctl --list-devices）\n"
                        "        → 確認有 /dev/video* 存取權限\n");
        xr_device_provider_stop(dev);
        xr_device_provider_shutdown(dev);
        xr_device_provider_destroy(dev);
        return 1;
    }

    // ── Step 11：啟動相機串流 ─────────────────────────────────
    printf("[Camera] Starting stream (1920x1080 @ 30fps)...\n");
    ret = xr_camera_provider_start(cam, on_rgb_frame, nullptr);
    if (ret != VITURE_GLASSES_SUCCESS) {
        fprintf(stderr, "[ERROR] xr_camera_provider_start failed: %d\n"
                        "        錯誤碼說明：\n"
                        "        -3 = USB_UNAVAILABLE（相機找不到）\n"
                        "        -4 = NOT_SUPPORTED（格式協商失敗）\n"
                        "        -5 = USB_EXEC（串流啟動失敗）\n"
                        "        -6 = INVALID_STATE（已在串流中）\n",
                ret);
        xr_camera_provider_destroy(cam);
        xr_device_provider_stop(dev);
        xr_device_provider_shutdown(dev);
        xr_device_provider_destroy(dev);
        return 1;
    }

    printf("\n[Running] 等待第一幀... (Ctrl+C 退出)\n\n");

    // ── 主迴圈：每秒查詢一次 6DoF Pose ──────────────────────
    int imu_print_count = 0;
    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // 主動查詢 6DoF Pose（比 callback 更精確，可帶預測時間補償延遲）
        float pose[7] = {};
        int pose_status = 0;
        int pose_ret = xr_device_provider_get_gl_pose_carina(
            dev,
            pose,
            0,            // predict_time=0：取當前 pose
            &pose_status
        );

        if (pose_ret == VITURE_GLASSES_SUCCESS) {
            // pose[7] = [px, py, pz, qw, qx, qy, qz]
            printf("[Pose/Poll] pos=(%.3f, %.3f, %.3f)  quat=(%.3f, %.3f, %.3f, %.3f)  "
                   "status=%s\n",
                   pose[0], pose[1], pose[2],
                   pose[3], pose[4], pose[5], pose[6],
                   pose_status == 0 ? "stable" : "unstable");
        }

        // 幀儲存成功後繼續跑 5 秒再退出（讓 IMU/Pose 繼續輸出供觀察）
        if (g_frame_saved.load()) {
            if (++imu_print_count >= 10) {
                printf("\n[Done] 幀已儲存，IMU/Pose 觀察完成，退出。\n");
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

    printf("[Shutdown] Done. Check hello_frame.ppm or hello_frame.jpg\n");
    return 0;
}
