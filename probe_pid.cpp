/**
 * @file probe_pid.cpp
 * @brief ASi Felis Origin — VITURE Glasses PID Probe
 *
 * 目的：用 SDK 自己的驗證 API 找出 Luma Ultra 正確的 glasses product_id。
 *
 * 背景：Luma Ultra USB 拓撲（實機 Device Manager 觀察）：
 *   - 眼鏡本體:   VID 0x35CA, PID 0x1104 ("VITURE Luma Ultra XR GLASSES")
 *   - 麥克風+HID群: VID 0x35CA, PID 0x1102 (composite, IMU 通道在此)
 *   - 相機 (UVC):  VID 0x0C45, PID 0x636B
 *
 * SDK 的 xr_device_provider_create(product_id) 要哪一個 PID 尚未確定，
 * 本程式用 is_product_id_valid() + get_market_name() 讓 SDK 自己回答，
 * 不呼叫 create() / initialize()，零副作用、不接管 USB。
 *
 * 編譯（沿用主專案 CMake，或單獨編）：
 *   cl /utf-8 /EHsc probe_pid.cpp /I sdk\include sdk\windows_x86_64\glasses.lib
 *   （或加進 CMakeLists 當第二個 target）
 *
 * 用法：
 *   probe_pid.exe
 */

#include "viture_glasses_provider.h"
#include "viture_macros_public.h"
#include "viture_result.h"

#include <cstdio>
#include <cstring>

#ifdef _WIN32
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#endif

static void enable_utf8_console()
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
}

// 候選 PID 清單。
// 主要懷疑對象：0x1102 (composite, 含 IMU) 與 0x1104 (眼鏡本體)。
// 順便掃描鄰近區段 (0x1100-0x110F) 以防 SDK 用其他 PID 對應 glasses provider。
static const int kCandidatePids[] = {
    0x1100, 0x1101, 0x1102, 0x1103,
    0x1104, 0x1105, 0x1106, 0x1107,
    0x1108, 0x1109, 0x110A, 0x110B,
    0x110C, 0x110D, 0x110E, 0x110F,
    // 相機 PID 也丟進來對照（預期 glasses provider 不認得它）
    0x636B,
};

int main()
{
    enable_utf8_console();

    printf("=== ASi Felis Origin - VITURE Glasses PID Probe ===\n\n");

    // 降低 SDK log 噪音（只看我們自己的輸出）
    xr_device_provider_set_log_level(LOG_LEVEL_ERROR);

    printf("%-8s  %-7s  %s\n", "PID", "valid", "market_name");
    printf("--------  -------  ------------------------------\n");

    const int n = static_cast<int>(sizeof(kCandidatePids) / sizeof(kCandidatePids[0]));
    for (int i = 0; i < n; ++i) {
        int pid = kCandidatePids[i];

        int valid = xr_device_provider_is_product_id_valid(pid);

        char name[256] = {0};
        int len = static_cast<int>(sizeof(name));
        int ret = xr_device_provider_get_market_name(pid, name, &len);

        if (valid == 1) {
            // 有效 PID：印出 market name（若取得成功）
            if (ret == VITURE_GLASSES_SUCCESS && name[0] != '\0') {
                printf("0x%04X    %-7s  %s\n", pid, "YES", name);
            } else {
                printf("0x%04X    %-7s  (valid, name fetch ret=%d)\n", pid, "YES", ret);
            }
        } else {
            // 無效 PID：只在它是我們重點懷疑對象時才印，減少雜訊
            if (pid == 0x1102 || pid == 0x1104 || pid == 0x636B) {
                printf("0x%04X    %-7s  -\n", pid, "no");
            }
        }
    }

    printf("\n");
    printf("解讀：\n");
    printf("  - valid=YES 且有 market_name 的 PID，就是 SDK 認得的 glasses product_id\n");
    printf("  - 若 0x1102 與 0x1104 都 valid，優先選 market_name 顯示為 Luma Ultra 的那個\n");
    printf("  - 相機 PID 0x636B 預期為 no（它是 camera provider 的，不是 glasses provider 的）\n");

    return 0;
}
