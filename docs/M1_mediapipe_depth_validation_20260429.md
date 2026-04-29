# NeoWalker M1 階段實驗報告：MediaPipe 空間深度感知管線部署與實測

**日期:** 2026-04-29
**目標:** 部署 MediaPipe 手部特徵點模型，驗證擷取 Z 軸深度數據（Z-Axis Depth），為「空間視差 UI（Binocular Disparity UI）」提供互動觸發參數。
**環境:** Windows 11 + WSL2 (Ubuntu 24.04 Noble) / ASUS TUF Gaming F16 (RTX-5060) / Python 3.12.3

---

## 1. 執行摘要 (Executive Summary)
本次實驗旨在於 F16 實驗室環境中建立 NeoWalker 平台的視覺感知神經。過程中遭遇了 GCC 編譯器世代衝突與 WSL2 虛擬 USB 硬體死鎖等底層基礎設施阻礙。最終透過放棄舊版 Legacy API 轉移至 Tasks API，並運用「感官數據注入（Sensory Data Injection）」戰術繞過硬體韌體缺陷，成功精準捕獲食指與手掌的相對 Z 軸深度數據，為 M1 階段的感知管線畫下句點。

---

## 2. 部署歷程與技術阻礙排除

### 2.1 C++ 核心編譯兵變與架構轉移
* **初始戰略:** 嘗試使用 Bazel 編譯 MediaPipe C++ 桌面版，以期未來順利移植至 Orin NX。
* **阻礙:** F16 筆電 CPU 觸發了 XNNPACK 對 `AVX-VNNI-INT8` 指令集的優化需求。然而，專案為了防範 CUDA 衝突，已強制將編譯環境降級至 GCC 12，導致編譯器無法識別該現代指令集而崩潰。
* **戰術修正:** 繞過底層編譯，轉向 Python 生態系。清理 Bazel 快取，建立獨立的 `neowalker_env` 虛擬環境。

### 2.2 世代斷層與 Tasks API 升級
* **阻礙:** 在 Python 3.12 現代環境下，MediaPipe 舊版的 Legacy `solutions` API 發生相容性斷裂（`solutions` 屬性遺失）。
* **戰術修正:** 全面升級至新世代 **MediaPipe Tasks API (v0.10+)**。
    * 手動獲取 Float16 量化版的 `hand_landmarker.task`（容量僅 7.5MB，極度節省 Orin NX 記憶體）。
    * 安裝 `libgles2 libgl1` 補齊 WSL2 Headless 環境缺失的 OpenGL ES 圖形渲染底層庫。

### 2.3 WSL2 硬體穿透與韌體死鎖 (Firmware Deadlock)
* **行動:** 使用 `usbipd` 將 ASUS FHD Webcam 強制掛載進 Ubuntu 領空，並透過 `chmod 666 /dev/video*` 解決權限阻擋。
* **阻礙 1 (頻寬窒息):** 預設未壓縮的 1080p YUYV 影像塞爆了 WSL2 虛擬 USB 網路頻寬，導致 `select() timeout`。嘗試強制寫入 MJPG / VGA (640x480) 格式。
* **阻礙 2 (硬體死鎖):** Linux `uvcvideo` 驅動程式在探測 ASUS 複合式鏡頭時，引發硬體邏輯錯亂。導致 IR (紅外線) 與 RGB (彩色) 鏡頭同時啟動（紅燈與白燈齊亮），USB 匯流排徹底當機。

### 2.4 破局戰術：感官數據注入 (Sensory Data Injection)
* 為推進 Z 軸邏輯驗證，暫時放棄不穩定的 WSL2 `usbipd` 橋接。
* 在 Windows 原生環境錄製帶有「深度推拉」與「捏合」動作的測試影片 (`hand_test.mp4`)。
* 修改 OpenCV 讀取管線，將資料來源從 `/dev/video2` 轉為讀取本地 MP4 進行模擬實戰。

---

## 3. 數據實測與成果分析

透過讀取模擬實戰影片，Tasks API 視覺管線成功上線，並穩定輸出手掌根部 (`wrist.z`) 與食指指尖 (`index_tip.z`) 的 3D 空間相對深度。

**關鍵數據節錄：**
```text
[基準靜止態 / Hover]
[Z軸深度偵測] 手掌根部: -0.0000 | 食指指尖: -0.0606 | 相對捏合: -0.0606

[深度觸發態 / Z-Axis Push]
[Z軸深度偵測] 手掌根部: -0.0000 | 食指指尖: -0.2143 | 相對捏合: -0.2143