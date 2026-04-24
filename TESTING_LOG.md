# 黑足貓多模態測試記錄

## 2026-04-23

### 環境
- 硬體：ASUS TUF GAMING F16 RTX 5060 8GB
- 模型：Ollama gemma4:e4b Q4_K_M
- Ollama 版本：0.21.1

### 文字推論
- eval rate：48.32 tokens/s ✅

### 視覺推論
- 場景理解（雪山、建築）✅
- 人物行為識別 ✅
- 漢字 OCR（明善寺）✅ 部分可用
- 小字/反光標籤（梅酒瓶）❌ 失準
- 圖片清晰度對識別影響顯著

### 音訊推論
- Ollama gemma4:e4b 未打包 audio encoder
- GitHub issue #15333（已知 bug）
- 架構決定：Whisper.cpp 負責 ASR

### 影片推論
- Ollama 不支援
- 待 M2：llama.cpp + llama-mtmd-cli on Jetson

### System Prompt
- v4 + Few-shot：行為穩定，無幻覺 ✅

### 編譯環境補充
- CUDA arch：sm_89（相容模式，非原生 sm_120 Blackwell）
- 影響：效能略低，功能完全正常
- 待辦：未來可重新編譯指定 sm_120 優化效能

## 2026-04-24 — Whisper.cpp large-v3-turbo 驗證

### 環境
- Whisper.cpp 最新版，GCC-12 + CUDA 12.1（sm_89 相容模式）
- 模型：ggml-large-v3-turbo.bin（1623 MB）
- 音訊：4745_voice_only.wav（6秒，截取自 4.5s）

### 測試結果
- 輸出：「耶 转转转转 / 转转转转」✅
- fallbacks：0p / 0h（完全穩定）
- 實際推論時間：~1357ms（排除模型載入）
- 對照 E4B 音訊：中文→日語幻覺 ❌

### 關鍵發現
- VAD 前處理必要：背景外語干擾導致 large-v3-turbo 幻覺循環
- 純中文語音段落：large-v3-turbo 正確識別
- 結論：asr_transcribe Tool 選定 Whisper large-v3-turbo + VAD

### ASi Felis 音訊管線確定
麥克風 → VAD → Whisper large-v3-turbo (-l zh) → 文字 → E4B

## 2026-04-24 — PaddleOCR 3.5.0 日文 OCR 驗證

### 環境
- PaddleOCR 3.5.0 + PaddlePaddle 3.0.0（CPU 模式）
- 模型：PP-OCRv5_server（自動下載）
- 測試圖片：清水寺籤詩（日文直排，E4B 完全失敗的測試案例）

### 結果（對照 E4B）
| 項目 | E4B Q6_K | PaddleOCR |
|---|---|---|
| 音羽山清水寺 | ❌ 寄田山清水亭 | ✅ 0.989 |
| 第二十三 | ❌ 第十二帖 | ✅ 0.999 |
| 吉 | ❌ 無 | ✅ 0.997 |
| 籤詩正文 | ❌ 鯉魚料理幻覺 | ✅ 完整讀出 |

### 結論
- PaddleOCR 完全解決 E4B 的日文直排 OCR 問題
- ocr_read Tool 選定：PaddleOCR lang='japan'/'ch'
- ASi Felis CJK OCR 管線確認可行

## 2026-04-24 — Whisper 多語言混合測試（IMG_4760）

### 音訊條件
- 21.4 秒，英語主體 + 日語地名（Kurobe）+ 中文短句
- 說話者：兩人，自拍旅遊情境

### 結果
- 英語句子：完美識別 ✅
- 「Kurobe」日語地名：完美識別 ✅
- 「Alpine Route」：誤識為「I-Pand-Root」❌（地名上下文缺失）
- 中文短句（強制英語模式）：轉譯為英語而非轉錄 ⚠️
- fallbacks：0p / 0h（完全穩定）
- 推論時間：~1.1-1.4 秒（21 秒音訊）

### 架構決定更新
- asr_transcribe Tool：language 預設改為 auto
- 純中文指令場景：-l zh
- 旅遊混合語境：-l auto（Whisper 自動偵測主語言）

## 2026-04-24 — WSL2 Webcam 視覺管線驗證（Gemini 協作）

### 實驗目標
在 VITURE Luma Ultra 到貨前，用 ASUS TUF F16 內建 Webcam 驗證 WSL2 視覺管線

### 架構
Windows USB → usbipd-win → WSL2 vhci-hcd → /dev/video0 → OpenCV

### 關鍵發現
- MJPEG 格式鎖定 + 20 幀預熱解決白曝光問題
- Permission Denied 需 sudo chmod 666 /dev/video*
- 成功截取 1920x1080 JPEG

### 對 M1 的直接應用
- usbipd 掛載流程與 Luma Ultra UVC 完全相同
- asi_vision_test.py 的 MJPEG + 預熱模式可直接對接 E4B 管線
- udev rule 自動化掛載待實作

## 2026-04-24 — WSL2 音訊管線驗證（Gemini 協作）

### 實驗目標
在 VITURE Luma Ultra 到貨前，驗證 WSL2 音訊擷取管線

### 架構
Realtek 麥克風 → Windows RDP → WSLg PulseServer → RDPSource → parec + sox → WAV

### 關鍵指令
export PULSE_SERVER=unix:/mnt/wslg/PulseServer
timeout 5s parec -d RDPSource --format=s16le --rate=44100 --channels=1 | \
  sox -t raw -r 44100 -e signed-integer -b 16 -c 1 - output.wav

### 疑難排解
- No soundcards: 需設定 PULSE_SERVER 環境變數
- 44 bytes WAV: timeout SIGKILL 截斷 header → 改用 sox 封裝
- Empty stream: Windows 隱私設定需開啟遠端桌面麥克風存取

### M1 對應
- 音訊管線架構確認，Luma Ultra 麥克風到貨後直接替換 RDPSource
- WAV → Whisper large-v3-turbo 自動化迴圈已就緒
