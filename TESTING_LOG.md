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
