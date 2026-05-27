/**
 * @file probe_drift.cpp
 * @brief ASi Felis Origin — VITURE Carina VIO 靜態漂移測試工具
 *
 * 目的:判斷 VIO 的 position 是「靠視覺錨定」(該不漂) 還是「純 IMU 積分」(必漂)。
 *
 * 判斷原理:
 *   - 視覺 VIO       : 眼鏡不動 → position 有界抖動,不單調漂走
 *   - 純 IMU 積分     : accel bias 兩次積分 → position 單調且加速發散 (數秒漂出數公尺)
 *   判斷依據 = position 隨時間是「有界抖動」還是「單調發散」。
 *
 * 與 probe_pose.cpp 的差異 (專為靜態量化設計):
 *   1. 用 POLL 路徑 (get_gl_pose_carina) 測漂移 — pos 像「相對啟動點位移」,
 *      有 status 欄位,取樣間隔自控,時間軸乾淨。callback 路徑 pos[0] 恆=1
 *      (絕對姿態表示),不適合算位移,故本工具關閉 callback 輸出。
 *   2. 記錄穩定後第一筆 pose 當原點,每筆算「離原點距離」= 漂移量直接度量。
 *   3. 累積各軸 min/max → 看是抖動 (range 小) 還是發散 (range 單調變大)。
 *   4. IMU gyro 模長監看:靜止應 < 0.05 rad/s;若超標代表被碰到,標記 [BUMP]。
 *   5. 固定跑 60 秒自動結束 (不靠 Ctrl+C),時間軸乾淨方便算漂移率 mm/s。
 *
 * 測試環境要求 (重要,否則測出假漂移):
 *   - 眼鏡放桌上,完全不動 (排除人體晃動)
 *   - 鏡頭朝向有紋理的場景 (書架/桌面雜物/窗景),不對白牆、不遮鏡頭
 *     → VIO 視覺部分需要場景特徵點,無特徵會退化成純 IMU,測出假漂移
 *
 * 跑之前務必確認 device 乾淨連上 (否則 device not connect → 測出的漂移全是假的):
 *   Get-Process | Where {$_.Name -match "asi_hello|probe"} | Stop-Process -Force
 *   Get-Process | Where {$_.Name -match "SpaceWalker|VITURE"}   # 應為空
 *   # 必要時重插眼鏡 USB-C
 *
 * 判讀:
 *   - 總漂移 < ~5cm 且 range 有界 → VIO translation 靠視覺,健康
 *   - 總漂移持續單調增大 (數十 cm~m) → 純 IMU 積分,VIO 視覺未生效 (檢查場景特徵)
 */

#include "viture_glasses_provider.h"
#include "viture_camera_provider.h"
#include "viture_device_carina.h"
#include "viture_macros_public.h"
#include "viture_result.h"

#include <cstdio>
#include <cmath>
#include <atomic>
#include <chrono>
#include <thread>

#ifdef _WIN32
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#endif

static constexpr int    LUMA_ULTRA_GLASSES_PID = 0x1104;
static constexpr double TEST_DURATION_SEC      = 60.0;   // 固定測試時長
static constexpr int    POLL_INTERVAL_MS       = 200;    // poll 間隔 (~5 Hz)
static constexpr float  STILL_GYRO_THRESH      = 0.05f;  // 靜止判定:gyro 模長閾值
static constexpr int    WARMUP_POLLS           = 5;      // 暖機:丟掉前 N 筆,等 VIO 穩定再設原點

// IMU gyro 模長 (atomic,給 poll 迴圈讀,判斷是否被碰到)
static std::atomic<float> g_gyro_mag{0.0f};
static std::atomic<long>  g_imu_count{0};

static void enable_utf8_console()
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
}

// IMU callback:不印,只更新 gyro 模長供靜止判定
static void on_imu(float* imu, double /*timestamp*/)
{
    float gx = imu[3], gy = imu[4], gz = imu[5];
    g_gyro_mag.store(std::sqrt(gx*gx + gy*gy + gz*gz));
    g_imu_count.fetch_add(1);
}

// Pose callback:靜態測試不用,留空 (避免靜止時 callback 狂噴洗版)
static void on_pose(float* /*pose*/, double /*timestamp*/) {}
static void on_vsync(double /*timestamp*/) {}

int main()
{
    enable_utf8_console();

    printf("=== ASi Felis Origin - Carina VIO 靜態漂移測試 ===\n");
    printf("環境要求: 眼鏡放桌上不動,鏡頭朝有紋理場景 (勿對白牆/遮鏡頭)\n");
    printf("時長: %.0f 秒 | poll 間隔: %d ms | 路徑: get_gl_pose_carina (poll)\n",
           TEST_DURATION_SEC, POLL_INTERVAL_MS);
    printf("判讀: 總漂移有界=VIO靠視覺(健康) / 單調發散=純IMU積分(視覺未生效)\n\n");

    xr_device_provider_set_log_level(LOG_LEVEL_ERROR);

    // ── 標準 Carina 啟動序列 ──
    XRDeviceProviderHandle dev = xr_device_provider_create(LUMA_ULTRA_GLASSES_PID);
    if (!dev) { fprintf(stderr, "[FATAL] create failed\n"); return 1; }

    xr_device_provider_set_dof_type_carina(dev, 1);  // 6DOF

    int ret = xr_device_provider_initialize(dev, nullptr, nullptr);
    if (ret != VITURE_GLASSES_SUCCESS) {
        fprintf(stderr, "[FATAL] initialize failed: %d (device 是否乾淨連上?)\n", ret);
        xr_device_provider_destroy(dev);
        return 1;
    }

    int dev_type = xr_device_provider_get_device_type(dev);
    printf("[Init] device_type = %d (CARINA=2)\n", dev_type);

    xr_device_provider_register_callbacks_carina(dev, on_pose, on_vsync, on_imu, nullptr);
    xr_device_provider_start(dev);

    // ── 暖機:等 VIO 穩定,確認 device 真的有資料 (擋掉 device not connect) ──
    printf("[Warmup] 等待 VIO 穩定 (丟掉前 %d 筆)...\n", WARMUP_POLLS);
    int warmup_ok = 0;
    for (int i = 0; i < WARMUP_POLLS * 3 && warmup_ok < WARMUP_POLLS; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(POLL_INTERVAL_MS));
        float pose[7] = {};
        int status = -999;
        int pret = xr_device_provider_get_gl_pose_carina(dev, pose, 0.0, &status);
        if (pret == VITURE_GLASSES_SUCCESS && status == 0) ++warmup_ok;
        else {
            fprintf(stderr, "[Warmup] poll ret=%d status=%d (等待中...)\n", pret, status);
        }
    }
    if (warmup_ok < WARMUP_POLLS) {
        fprintf(stderr, "[FATAL] VIO 未能穩定 — device 可能沒乾淨連上。\n");
        fprintf(stderr, "        請重插眼鏡 USB-C、確認 SpaceWalker 沒跑,再重試。\n");
        xr_device_provider_stop(dev);
        xr_device_provider_shutdown(dev);
        xr_device_provider_destroy(dev);
        return 1;
    }

    // ── 設原點:暖機後第一筆穩定 pose ──
    float origin[3] = {};
    {
        float pose[7] = {};
        int status = -999;
        xr_device_provider_get_gl_pose_carina(dev, pose, 0.0, &status);
        origin[0] = pose[0]; origin[1] = pose[1]; origin[2] = pose[2];
    }
    printf("[Origin] 原點設定: pos=(%.4f, %.4f, %.4f)\n", origin[0], origin[1], origin[2]);
    printf("\n--- 開始 60 秒靜態觀察 (請勿觸碰眼鏡) ---\n");
    printf("%-8s %-10s  %-26s  %-10s %s\n",
           "t(s)", "dist(cm)", "offset from origin (cm)", "|gyro|", "note");

    // ── 累積統計 ──
    float min_off[3] = { 1e9f, 1e9f, 1e9f };
    float max_off[3] = { -1e9f, -1e9f, -1e9f };
    float max_dist   = 0.0f;       // 離原點最遠距離 (cm)
    float last_dist  = 0.0f;       // 最後一筆距離 (cm) — 看終點 vs 峰值,判斷單調發散
    int   bump_count = 0;          // 被碰到的次數 (gyro 超標)
    int   n_samples  = 0;

    auto t_start = std::chrono::steady_clock::now();
    int  poll_n  = 0;

    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(POLL_INTERVAL_MS));

        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - t_start).count();
        if (elapsed >= TEST_DURATION_SEC) break;

        float pose[7] = {};
        int status = -999;
        int pret = xr_device_provider_get_gl_pose_carina(dev, pose, 0.0, &status);
        ++poll_n;

        if (pret != VITURE_GLASSES_SUCCESS) {
            printf("%-8.1f POLL FAILED ret=%d status=%d\n", elapsed, pret, status);
            continue;
        }

        // 離原點位移 (公尺 → 公分)
        float ox = (pose[0] - origin[0]) * 100.0f;
        float oy = (pose[1] - origin[1]) * 100.0f;
        float oz = (pose[2] - origin[2]) * 100.0f;
        float dist = std::sqrt(ox*ox + oy*oy + oz*oz);

        // 更新統計
        if (ox < min_off[0]) min_off[0] = ox;  if (ox > max_off[0]) max_off[0] = ox;
        if (oy < min_off[1]) min_off[1] = oy;  if (oy > max_off[1]) max_off[1] = oy;
        if (oz < min_off[2]) min_off[2] = oz;  if (oz > max_off[2]) max_off[2] = oz;
        if (dist > max_dist) max_dist = dist;
        last_dist = dist;
        ++n_samples;

        // 靜止判定
        float gmag = g_gyro_mag.load();
        const char* note = "";
        if (gmag > STILL_GYRO_THRESH) { note = "[BUMP] 被碰到?"; ++bump_count; }

        // 每秒印一筆 (POLL_INTERVAL=200ms → 每 5 筆印 1 筆),避免洗版
        if (poll_n % 5 == 0) {
            printf("%-8.1f %-10.2f  (%+6.2f, %+6.2f, %+6.2f)      %-6.3f %s\n",
                   elapsed, dist, ox, oy, oz, gmag, note);
        }
    }

    // ── 漂移報告 ──
    float range[3] = {
        max_off[0] - min_off[0],
        max_off[1] - min_off[1],
        max_off[2] - min_off[2]
    };
    double drift_rate = (TEST_DURATION_SEC > 0) ? (max_dist * 10.0 / TEST_DURATION_SEC) : 0.0; // mm/s

    printf("\n======== 靜態漂移報告 (%.0f 秒) ========\n", TEST_DURATION_SEC);
    printf("有效取樣數      : %d\n", n_samples);
    printf("IMU 總筆數      : %ld\n", g_imu_count.load());
    printf("被碰到次數      : %d  (gyro > %.2f rad/s)\n", bump_count, STILL_GYRO_THRESH);
    printf("\n離原點距離:\n");
    printf("  峰值最遠      : %.2f cm\n", max_dist);
    printf("  最後一筆      : %.2f cm\n", last_dist);
    printf("  平均漂移率    : %.2f mm/s (以峰值/時長計)\n", drift_rate);
    printf("\n各軸位移範圍 (max - min):\n");
    printf("  X range       : %.2f cm  [%.2f .. %.2f]\n", range[0], min_off[0], max_off[0]);
    printf("  Y range       : %.2f cm  [%.2f .. %.2f]\n", range[1], min_off[1], max_off[1]);
    printf("  Z range       : %.2f cm  [%.2f .. %.2f]\n", range[2], min_off[2], max_off[2]);

    printf("\n判讀:\n");
    if (bump_count > n_samples / 10) {
        printf("  ⚠ 被碰到次數偏多 (%d/%d) — 測試期間眼鏡有晃動,結果不純。建議重測。\n",
               bump_count, n_samples);
    }
    printf("  - 峰值≈最後一筆 且 距離小 (<~5cm)  → 有界抖動,VIO 靠視覺,健康 ✅\n");
    printf("  - 最後一筆 >> 起始 且 持續增大       → 單調發散,純 IMU 積分,視覺未生效 ⚠\n");
    printf("    (純 IMU 積分 60 秒通常漂數十 cm~數 m,且 X/Y/Z 其中一軸明顯主導)\n");

    // ── 清理 ──
    xr_device_provider_stop(dev);
    xr_device_provider_shutdown(dev);
    xr_device_provider_destroy(dev);
    return 0;
}
