# Project ASi Felis Origin — 模型部署與實驗數據記錄

## 2026-04-23 — 黑足貓多模態測試（Gemma 4 E4B）

### 環境
- 開發機：ASUS TUF GAMING F16 RTX 5060 8GB
- 模型：Ollama gemma4:e4b Q4_K_M
- 推論引擎：Ollama 0.21.1

### 結果
文字推論效能：**48.32 tokens/s**，符合實時對話需求。

視覺推論表現：
- 場景理解（雪山、建築）✅
- 人物行為識別 ✅
- 漢字 OCR（明善寺）✅ 部分可用
- 小字/反光標籤（梅酒瓶）❌ 失準
- 圖片清晰度對識別影響顯著

### 結論
Gemma 4 E4B 文字推論穩定，視覺能力可滿足 Project ASi 場景理解需求。**音訊處理確定走 Whisper.cpp 獨立路徑**。

---

## 2026-04-23 — 編譯環境補充（llama.cpp sm_89 相容模式）

### 環境
- GPU：RTX 5060（sm_120 Blackwell 架構）
- PyTorch：不支援 sm_120，轉向 llama.cpp
- llama.cpp：手動編譯，CUDA_ARCHITECTURES=89

### 結果
成功編譯，執行正常。以 sm_89 相容模式運行 sm_120 硬體，推論效能略低於理論峰值但可接受。

### 結論
RTX 5060 + llama.cpp + sm_89 組合確認可行，作為 Edge AI Core 備選方案。

---

## 2026-04-24 — Whisper large-v3-turbo 驗證

### 環境
- 模型：ggml-large-v3-turbo.bin（1623 MB）
- 測試音訊：4745_voice_only.wav（6 秒純中文）
- 指令：`./main -m models/ggml-large-v3-turbo.bin -f 4745_voice_only.wav`

### 結果
**推論時間：~1357ms**
**輸出：**「耶 转转转转 / 转转转转」✅
**Fallbacks：** 0p / 0h（完全穩定）

21 秒多語言混合測試（IMG_4760.wav）：
- **推論時間：1.1–1.4s**
- 「Kurobe」日語地名：完美識別 ✅
- 「Alpine Route」：誤識為「I-Pand-Root」❌（地名上下文缺失）
- 中文短句（強制英語模式）：轉譯為英語而非轉錄 ⚠️
- **關鍵發現：需要 VAD 前處理**，避免背景外語造成的幻覺循環

### 結論
**Whisper large-v3-turbo 選定**作為 ASR 引擎。音訊管線架構：
```
麥克風 → VAD → Whisper large-v3-turbo (-l zh/auto) → 文字 → E4B
```

---

## 2026-04-24 — PaddleOCR 3.5.0 日文驗證

### 環境
- 版本：PaddleOCR 3.5.0（CPU 模式）
- 模型：PP-OCRv5_server
- 測試圖：日文直排籤詩（清水寺）

### 結果
**完全解決 Gemma 4 E4B OCR 失敗問題**
- 信心度：0.989 / 0.999 / 0.997
- 正確讀出：「吉」「音羽山清水寺」「第二十三」
- 對比：E4B 產生「鯉魚料理」幻覺 vs PaddleOCR 完整識別

### 結論
**PaddleOCR 確定**作為精密文字識別工具。Tool 選型鎖定：
```
ocr_read       → PaddleOCR (lang='japan'/'ch')
asr_transcribe → Whisper large-v3-turbo + VAD
vision_analyze → Gemma 4 E4B (場景/人物/非小字 OCR)
```

---

## 2026-04-24 — WSL2 USB 存取可行性驗證（Webcam 原型）

### 環境
- WSL2 Ubuntu 24.04
- Webcam：ASUS FHD (VID 3277, PID 0096)
- 傳輸：usbipd-win → vhci-hcd → `/dev/video0`

### 結果
- **USB 掛載流程驗證**：✅ `usbipd attach` 成功
- **權限問題解決**：`sudo chmod 666 /dev/video*` 有效
- **MJPEG 格式處理**：V4L2 + MJPG fourcc → 1920×1080 成功
- **自動曝光穩定化**：`cap.grab()` 預熱 20 次解決「太陽拳」

### Debug Journal
| 現象 | 解法 |
|---|---|
| Permission Denied | `sudo chmod 666 /dev/video*` |
| 全白曝光 | 移除手動亮度控制 + 增加預熱 |
| `select() timeout` | `wsl --shutdown` 後重新 attach |

### 架構決定
- **RGB / Depth 取像**：採用 VITURE SDK callback（viture_camera_provider.h）
- **Webcam 實驗用途**：
  1. 驗證 usbipd-win 掛載流程（Luma Ultra 到貨後直接套用）
  2. 驗證 udev rule 語法（VID 0x0C45 規則已就緒）
  3. MJPEG decode 原型（M2 可沿用）
- **不採用**：V4L2 / /dev/video0 直接讀取（SDK 已抽象化）

---

## 2026-04-24 — WSL2 音訊管線驗證（RDPSource → WAV）

### 環境
- WSL2 + WSLg PulseAudio 橋接
- 環境變數：`PULSE_SERVER=unix:/mnt/wslg/PulseServer`
- Windows 端：開啟「遠端桌面」麥克風存取權限

### 結果
- **音訊路由成功**：Windows 麥克風 → WSL2 錄音
- **WAV 格式穩定**：`parec | sox` 管線取代 `parecord`
- **Header 毀損問題解決**：sox 自動處理 44-byte header

### Debug Journal
| 現象 | 解法 |
|---|---|
| `No soundcards found` | `export PULSE_SERVER=...` |
| 44-byte 毀損 WAV | 改用 `sox` 處理 header |
| Empty Stream (00 00) | Windows 隱私權設定 |

### 架構決定
- **音訊管線架構確認**，Luma Ultra 麥克風到貨後直接替換 RDPSource
- **WAV → Whisper large-v3-turbo 自動化迴圈已就緒**

---

## 2026-04-26 — VITURE SDK 顯示能力完整評估

### 評估範圍
通讀 `sdk/include/` 全 8 個 header，釐清 SDK 能力邊界：
- `viture_glasses_provider.h`（生命週期 + 狀態回呼）
- `viture_camera_provider.h`（RGB 取像）
- `viture_device_carina.h`（Pose/IMU/VSync/Stereo，Luma Ultra 專用）
- `viture_device.h`（GEN1/GEN2 IMU，Luma Ultra 不適用）
- `viture_protocol_public.h`（33KB，裝置控制核心）
- 其他：macros, result, version

### 關鍵發現

**1. 顯示輸出：SDK 不負責 framebuffer**
- **確認：無任何 `*_submit_frame()` / `*_render()` / `*_set_framebuffer()` API**
- **Luma Ultra 對 OS 而言就是外接螢幕**，畫面走 Linux DRM/KMS → USB-C DP Alt Mode
- **SDK 職責**：設定眼鏡硬體解析度/更新率，不管 framebuffer 內容

**2. 解析度支援超出預期**
```
0x31: 1920×1080 @ 60Hz   (2D 標準)
0x32: 3840×1080 @ 60Hz   (3D SBS)
0x33: 1920×1080 @ 90Hz   (2D 高刷)
0x34: 1920×1080 @ 120Hz  (2D 極高刷)
0x35: 3840×1080 @ 90Hz   (3D SBS 高刷)
0x41-45: 1920/3840×1200 系列
```

**3. 電致變色鏡片控制**
`xr_device_provider_set_film_mode(handle, 1.0f)` → 鏡片變暗（戶外/強對比 AR）
`xr_device_provider_set_film_mode(handle, 0.0f)` → 鏡片透明（文字 HUD）

**4. VSync 同步支援**
`XRVSyncCallback` 提供顯示時序，配合 OpenGL frame timing

### M5 AR Overlay 渲染架構確定

```
[Jetson Orin NX]
       ↓
[OpenGL 4.x + GLFW 全螢幕]
       ↓
[渲染 3840×1080 SBS framebuffer]
   - 左半邊：左眼透視矩陣 + AR overlay
   - 右半邊：右眼透視矩陣 + AR overlay
       ↓
[set_display_mode(0x32) 切換 3D SBS]
       ↓
[Linux DRM → USB-C DP Alt Mode]
       ↓
[眼鏡硬體自動分流左右眼 micro-OLED]

並行：
- XRVSyncCallback → 觸發渲染
- XRPoseCallback → 更新 view matrix  
- XRCameraFrameCallback → 餵 E4B 場景理解
- set_film_mode → 動態調整鏡片透光
```

### 技術棧選定
| 元件 | 選擇 | 理由 |
|---|---|---|
| GL context | GLFW | Jetson L4T 原生支援 |
| 渲染框架 | 純 OpenGL 4.x | 效能最佳，控制最細 |
| 數學庫 | GLM | OpenGL 座標系一致 |
| HUD 框架 | Dear ImGui | Function Calling 結果可直接顯示 |

### SDK 覆蓋能力表
詳見附錄 `SDK_COVERAGE.md`

### 結論
M5 AR overlay 路徑完全釐清：
1. **SDK 負責**：輸入感知（RGB/Pose/IMU）+ 顯示控制（解析度/鏡片）
2. **自己負責**：OpenGL 渲染 SBS framebuffer
3. **OS 負責**：Framebuffer → 眼鏡的 DP Alt Mode 傳輸

Luma Ultra 實機到貨後，M5 可直接開工。

---

## 測試環境統一記錄

### 硬體環境
- **開發機**：ASUS TUF GAMING F16
  - GPU：NVIDIA RTX 5060 8GB (sm_120 Blackwell)
  - RAM：32GB DDR5
  - OS：Windows 11 + WSL2 Ubuntu 24.04

### 軟體工具鏈
- **AI 推論**：Ollama 0.1.32, llama.cpp (sm_89 mode)
- **視覺**：OpenCV 4.x, PaddleOCR 3.5.0
- **音訊**：Whisper.cpp, PulseAudio via WSLg
- **開發**：VS Code, CMake 3.28, GNU 13.3.0

### 待實機驗證項目
- [ ] Luma Ultra 到貨（預計 5 月初，訂單 #88199146589）
- [ ] USB HID PID 確認（main.cpp 第 36 行）
- [ ] RGB 幀接收（`XRCameraFrameCallback`）
- [ ] IMU / 6DoF Pose 精度（`XRPoseCallback`）
- [ ] VSync 同步測試（`XRVSyncCallback`）
