# Project ASi Felis Origin — SDK 覆蓋能力表

## 概述

基於對 VITURE SDK 全 8 個 header 檔案的完整分析（`viture_glasses_provider.h`、`viture_camera_provider.h`、`viture_device_carina.h`、`viture_device.h`、`viture_protocol_public.h`、`viture_macros_public.h`、`viture_result.h`、`viture_version.h`），本文件明確定義了哪些子系統由 SDK 負責、哪些需要自行實作。

適用於 **Luma Ultra (XR_DEVICE_TYPE_VITURE_CARINA)** 裝置。

---

## 子系統覆蓋能力表

| 子系統 | SDK 支援 | 對應 API/Header | 自幹成本 | 備註 |
|--------|----------|-----------------|----------|------|
| **感知輸入** | | | | |
| RGB camera | ✅ 完整 | `xr_camera_provider_*` + `XRCameraFrameCallback` | 零 | 1080p@30fps MJPEG，支援多格式輸出 |
| Stereo depth camera | ✅ 完整 | `xr_device_provider_register_callbacks_carina()` + `XRCameraCallback` | 零 | 雙灰階深度對 |
| 6DoF pose | ✅ 完整 | `xr_device_provider_get_gl_pose_carina()` + `XRPoseCallback` | 零 | OpenGL 座標系，支援預測 |
| IMU raw data | ✅ 完整 | `XRImuCallback`（Carina 專用） | 零 | [ax, ay, az, gx, gy, gz] |
| VSync timing | ✅ 完整 | `XRVSyncCallback` | 零 | 顯示同步信號，關鍵 |
| **控制輸出** | | | | |
| Pose 重置/原點設定 | ✅ 完整 | `reset_pose_carina` / `reset_origin_carina` | 零 | 支援重新校準 tracking |
| 顯示模式設定 | ✅ 完整 | `xr_device_provider_set_display_mode()` | 零 | 2D/3D SBS/高刷，16 種模式 |
| 亮度控制 | ✅ 完整 | `set_brightness_level()` | 零 | Luma 系列：0-8 級 |
| 音量控制 | ✅ 完整 | `set_volume_level()` | 零 | Luma 系列：0-8 級 |
| 電致變色鏡片 | ✅ 完整 | `set_film_mode()` | 零 | 0.0f=透明，1.0f=變暗 |
| **渲染輸出** | | | | |
| AR overlay framebuffer | ❌ | 不存在 | **OpenGL/EGL** | SDK 不負責 framebuffer |
| 左右眼立體渲染 | ❌ | 不存在 | **自己渲染 SBS** | SDK 提供 3D SBS 模式 |
| **音訊** | | | | |
| Microphone input | ❌ | 不存在 | **PulseAudio/ALSA** | 走標準 USB Audio Class |
| Speaker output | ⚠️ 部分 | 只有 `set_volume_level` | **OS audio 路徑** | 音訊資料不經 SDK |

---

## M5 里程碑關鍵結論

### SDK 負責的部分（直接調用）
1. **感知管道**：RGB/Depth/Pose/IMU 全部透過 callback 取得
2. **裝置控制**：解析度、亮度、音量、鏡片透光度
3. **同步信號**：VSync callback 提供 frame timing 基準

### 需要自行實作的部分
1. **OpenGL/EGL context**：建議 GLFW + OpenGL 4.x
2. **SBS 立體渲染**：3840×1080 左右並排 framebuffer
3. **透視矩陣計算**：根據 6DoF pose 更新 view matrix
4. **AR overlay 內容**：HUD、文字、3D 物件渲染
5. **音訊處理**：麥克風輸入透過 PulseAudio，輸出透過 OS

### OS 負責的部分
1. **Display pipeline**：DRM/KMS → USB-C DP Alt Mode → 眼鏡
2. **Audio pipeline**：標準 USB Audio Class 處理
3. **USB management**：裝置插拔、power management

---

## 顯示模式對照表

### 標準模式（適用 Luma Ultra）

| 巨集 | 解析度 | 更新率 | 用途 |
|------|--------|--------|------|
| `VITURE_DISPLAY_MODE_1920_1080_60HZ` (`0x31`) | 1920×1080 | 60Hz | 2D 標準 |
| `VITURE_DISPLAY_MODE_3840_1080_60HZ` (`0x32`) | 3840×1080 | 60Hz | **3D SBS（M5 主用）** |
| `VITURE_DISPLAY_MODE_1920_1080_90HZ` (`0x33`) | 1920×1080 | 90Hz | 2D 高刷 |
| `VITURE_DISPLAY_MODE_1920_1080_120HZ` (`0x34`) | 1920×1080 | 120Hz | 2D 極高刷 |
| `VITURE_DISPLAY_MODE_3840_1080_90HZ` (`0x35`) | 3840×1080 | 90Hz | 3D SBS 高刷 |
| `VITURE_DISPLAY_MODE_1920_1200_*` (`0x41-0x44`) | 1920×1200 | 60/90/120Hz | 高解析度 2D |
| `VITURE_DISPLAY_MODE_3840_1200_*` (`0x42/0x45`) | 3840×1200 | 60/90Hz | 高解析度 3D SBS |

### 便捷切換函式

```c
// 2D ↔ 3D 快速切換（預設 60Hz）
xr_device_provider_switch_dimension(handle, 1);  // 進入 3D (0x32)
xr_device_provider_switch_dimension(handle, 0);  // 進入 2D (0x31)
```

---

## API 使用範例

### M5 初始化流程

```c
// 1. 裝置生命週期
XRDeviceProviderHandle device = xr_device_provider_create(product_id);
xr_device_provider_initialize(device, NULL, NULL);
xr_device_provider_start(device);

// 2. 切換到 3D SBS 模式（M5 AR overlay 必須）
xr_device_provider_set_display_mode(device, VITURE_DISPLAY_MODE_3840_1080_60HZ);

// 3. 註冊 Carina callbacks
xr_device_provider_register_callbacks_carina(
    device,
    pose_callback,     // 6DoF pose → 更新 view matrix
    vsync_callback,    // VSync → 觸發渲染
    imu_callback,      // IMU → 穩定化/預測
    camera_callback    // 雙目相機 → 場景理解
);

// 4. RGB camera（場景理解用）
XRCameraProviderHandle camera = xr_camera_provider_create(vid, pid);
xr_camera_provider_start(camera, rgb_frame_callback, NULL);
```

### M5 runtime 控制

```c
// 根據 AR 內容動態調整鏡片
if (outdoor_scene || high_contrast_overlay) {
    xr_device_provider_set_film_mode(device, 1.0f);  // 變暗
} else {
    xr_device_provider_set_film_mode(device, 0.0f);  // 透明
}

// 亮度自適應
int brightness = calculate_ambient_brightness();
xr_device_provider_set_brightness_level(device, brightness);
```

---

## M5 技術棧建議

| 層級 | 元件 | 推薦選擇 | 理由 |
|------|------|----------|------|
| **GL Context** | GLFW, SDL2, EGL | **GLFW** | Jetson L4T 原生，開發簡單 |
| **渲染引擎** | OpenGL, Vulkan | **OpenGL 4.x** | 控制細，效能佳 |
| **數學庫** | GLM, Eigen | **GLM** | OpenGL 座標系一致 |
| **HUD** | Dear ImGui, CEGUI | **Dear ImGui** | 輕量，Function Calling 友好 |
| **字型** | FreeType, stb_truetype | **FreeType** | 中文支援完整 |

---

## 除錯檢查清單

### M5 啟動時驗證

- [ ] `xr_device_provider_get_device_type() == XR_DEVICE_TYPE_VITURE_CARINA`
- [ ] `xr_device_provider_set_display_mode(0x32)` 回傳 `VITURE_GLASSES_SUCCESS`
- [ ] VSync callback 每 16.67ms 觸發一次（60Hz）
- [ ] 6DoF pose 回傳合理數值（非全零、非 NaN）
- [ ] RGB camera 取得 1920×1080 MJPEG frame

### M5 runtime 驗證

- [ ] SBS framebuffer 左右半邊內容不同步（立體視差正確）
- [ ] 頭部轉動時 AR overlay 跟隨 6DoF pose
- [ ] 鏡片變色在戶外場景自動觸發
- [ ] OpenGL 渲染無 GL_ERROR

---

## 版本記錄

- **v1.0** (2026-04-26): 首版，基於 VITURE SDK 完整 header 分析
- 未來更新：Luma Ultra 實機驗證後補充實測數據

---

## 參考資料

- VITURE SDK include 目錄：`sdk/include/*.h`
- OpenGL 座標系：右手系 (x→right, y→up, z→backward)
- USB-C DP Alt Mode 規格：DisplayPort 1.4+ via USB-C
- Jetson L4T 文件：NVIDIA Developer Documentation
