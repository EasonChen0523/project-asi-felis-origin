/**
 * @file probe_pose.cpp
 * @brief ASi Felis Origin — VITURE Carina 6DoF VIO Pose 診斷工具
 *
 * 目的:釐清為什麼 main.cpp 的 pose 永遠是固定值 pos=(1,0,0) quat=(0,0,1,0),
 *       即使戴著眼鏡劇烈搖頭也不變。
 *
 * 本工具與 main.cpp 的差異(專為 debug VIO 設計):
 *   1. 同時觀察兩條 pose 取得路徑:
 *      - callback path  : on_pose (SDK 主動推,USB monitoring thread)
 *      - poll path      : get_gl_pose_carina (主動拉,主迴圈)
 *      → 分離「VIO 全死」vs「只有某條路徑死」
 *   2. poll 無論成功/失敗都印 (印 ret + pose_status),不像 main.cpp 只在
 *      SUCCESS 才印 → 揪出「get_gl_pose_carina 回傳非 0 所以靜默」的情況
 *   3. IMU 只在 gyro 模長 > 0.5 rad/s 時印 (搖頭才印),消除 IMU 洪水,
 *      讓 pose 看得清楚。搖頭時 IMU 會印 = 證明有動作,對照 pose 該不該變。
 *   4. poll 間隔 200ms,跑到 Ctrl+C (自己控制觀察時長)
 *   5. pose 變化偵測:每筆 poll 與上一筆比較,完全相同則標 [STATIC],
 *      有變化則標 [CHANGED] + 印出差異 → 一眼看出 pose 是死是活
 *
 * 用法:
 *   1. 確認 SpaceWalker 完全關閉 (Get-Process SpaceWalker)
 *   2. 戴上眼鏡,執行後持續左右搖頭 (yaw) + 點頭 (pitch)
 *   3. 觀察:搖頭時 [IMU] 有印 (證明動作被捕捉),但 [POLL] 是 STATIC 還是 CHANGED?
 *   4. Ctrl+C 結束
 *
 * 編譯:加進 CMakeLists 當第三個 target,或:
 *   cl /utf-8 /EHsc probe_pose.cpp /I sdk\include sdk\windows_x86_64\glasses.lib
 */

#include "viture_glasses_provider.h"
#include "viture_camera_provider.h"
#include "viture_device_carina.h"
#include "viture_macros_public.h"
#include "viture_result.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <atomic>
#include <chrono>
#include <thread>
#include <csignal>

#ifdef _WIN32
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#endif

static constexpr int LUMA_ULTRA_GLASSES_PID = 0x1104;  // confirmed via probe_pid

static std::atomic<bool> g_running{true};

// callback pose 的最後一筆(供主迴圈對照 callback vs poll)
static std::atomic<float> g_cb_px{0}, g_cb_py{0}, g_cb_pz{0};
static std::atomic<float> g_cb_qw{0}, g_cb_qx{0}, g_cb_qy{0}, g_cb_qz{0};
static std::atomic<double> g_cb_ts{0};
static std::atomic<long> g_cb_count{0};

static void enable_utf8_console()
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
}

// ── IMU callback:只在明顯轉頭時印 (gyro 模長 > 0.5 rad/s) ──
static void on_imu(float* imu, double timestamp)
{
    // Carina layout: [ax, ay, az, gx, gy, gz]
    float gx = imu[3], gy = imu[4], gz = imu[5];
    float gyro_mag = std::sqrt(gx*gx + gy*gy + gz*gz);

    if (gyro_mag > 0.5f) {
        printf("[IMU]  t=%.3f  gyro=(%.3f, %.3f, %.3f) |g|=%.3f  <-- motion detected\n",
               timestamp, gx, gy, gz, gyro_mag);
    }
}

// ── Pose callback:記錄到 atomic,同時印出 ──
static void on_pose(float* pose, double timestamp)
{
    g_cb_px.store(pose[0]); g_cb_py.store(pose[1]); g_cb_pz.store(pose[2]);
    g_cb_qw.store(pose[3]); g_cb_qx.store(pose[4]);
    g_cb_qy.store(pose[5]); g_cb_qz.store(pose[6]);
    g_cb_ts.store(timestamp);
    g_cb_count.fetch_add(1);

    printf("[POSE/cb]   t=%.3f  pos=(%.3f, %.3f, %.3f)  quat=(%.3f, %.3f, %.3f, %.3f)\n",
           timestamp,
           pose[0], pose[1], pose[2],
           pose[3], pose[4], pose[5], pose[6]);
}

static void on_vsync(double) {}

static void on_signal(int) { g_running.store(false); }

int main()
{
    enable_utf8_console();
    std::signal(SIGINT, on_signal);

    printf("=== ASi Felis Origin - Carina 6DoF VIO Pose Probe ===\n");
    printf("戴上眼鏡並持續左右搖頭。觀察:\n");
    printf("  [IMU]     gyro 動作偵測 (搖頭時才印)\n");
    printf("  [POSE/cb] callback 推送的 pose\n");
    printf("  [POLL]    get_gl_pose_carina 主動查詢 (含 ret + status + 變化偵測)\n");
    printf("Ctrl+C 結束。\n\n");

    xr_device_provider_set_log_level(LOG_LEVEL_ERROR);  // 降噪,只看 ERROR

    // ── 標準 Carina 啟動序列 ──
    XRDeviceProviderHandle dev = xr_device_provider_create(LUMA_ULTRA_GLASSES_PID);
    if (!dev) { fprintf(stderr, "[FATAL] create failed\n"); return 1; }

    xr_device_provider_set_dof_type_carina(dev, 1);  // 6DOF

    int ret = xr_device_provider_initialize(dev, nullptr, nullptr);
    if (ret != VITURE_GLASSES_SUCCESS) {
        fprintf(stderr, "[FATAL] initialize failed: %d\n", ret);
        xr_device_provider_destroy(dev);
        return 1;
    }

    int dev_type = xr_device_provider_get_device_type(dev);
    printf("[Init] device_type = %d (CARINA=2)\n", dev_type);

    ret = xr_device_provider_register_callbacks_carina(dev, on_pose, on_vsync, on_imu, nullptr);
    printf("[Init] register_callbacks_carina ret = %d\n", ret);

    ret = xr_device_provider_start(dev);
    printf("[Init] start ret = %d\n", ret);

    printf("\n--- 開始觀察 (搖頭!) ---\n\n");

    // ── 主迴圈:poll pose,200ms 間隔,變化偵測 ──
    float last[7] = {0};
    bool have_last = false;
    int poll_n = 0;

    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        float pose[7] = {};
        int pose_status = -999;
        int pret = xr_device_provider_get_gl_pose_carina(dev, pose, 0.0, &pose_status);

        ++poll_n;

        // 無論成功失敗都印 ret
        if (pret != VITURE_GLASSES_SUCCESS) {
            printf("[POLL #%d] get_gl_pose_carina FAILED ret=%d status=%d\n",
                   poll_n, pret, pose_status);
            continue;
        }

        // 變化偵測:跟上一筆比
        const char* tag = "?";
        float dmax = 0.0f;
        if (have_last) {
            bool same = true;
            for (int i = 0; i < 7; ++i) {
                float d = std::fabs(pose[i] - last[i]);
                if (d > dmax) dmax = d;
                if (d > 1e-6f) same = false;
            }
            tag = same ? "STATIC " : "CHANGED";
        } else {
            tag = "first  ";
        }

        printf("[POLL #%d %s] ret=%d status=%s  pos=(%.4f, %.4f, %.4f)  "
               "quat=(%.4f, %.4f, %.4f, %.4f)  maxd=%.5f\n",
               poll_n, tag, pret,
               pose_status == 0 ? "stable  " : "UNSTABLE",
               pose[0], pose[1], pose[2],
               pose[3], pose[4], pose[5], pose[6],
               dmax);

        for (int i = 0; i < 7; ++i) last[i] = pose[i];
        have_last = true;
    }

    // ── 摘要 ──
    printf("\n--- 觀察結束 ---\n");
    printf("Pose callback 觸發次數: %ld\n", g_cb_count.load());
    printf("最後一筆 callback pose: pos=(%.4f, %.4f, %.4f) quat=(%.4f, %.4f, %.4f, %.4f) @ t=%.3f\n",
           g_cb_px.load(), g_cb_py.load(), g_cb_pz.load(),
           g_cb_qw.load(), g_cb_qx.load(), g_cb_qy.load(), g_cb_qz.load(),
           g_cb_ts.load());
    printf("Poll 次數: %d\n", poll_n);

    // ── 清理 ──
    xr_device_provider_stop(dev);
    xr_device_provider_shutdown(dev);
    xr_device_provider_destroy(dev);

    printf("\n判讀指引:\n");
    printf("  - 若搖頭時 [IMU] 有印,但 [POLL] 全是 STATIC → VIO pose 路徑死 (兩條都死)\n");
    printf("  - 若 [POLL] 是 CHANGED 但 [POSE/cb] 不動 → 只有 callback 路徑死,poll 可用\n");
    printf("  - 若 status=UNSTABLE 持續 → VIO 未收斂 (可能缺視覺輸入)\n");
    printf("  - 若 poll FAILED ret≠0 → get_gl_pose_carina 根本沒回有效資料\n");
    return 0;
}
