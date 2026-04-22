# Project ASi Felis Origin — M1

**Smart AR Glasses · Edge AI Pipeline · VITURE Luma Ultra + Jetson Orin NX 16GB**

> M1 Milestone: Hardware validation & SDK Hello World  
> M1 里程碑：硬體驗收與 SDK Hello World

---

## Overview / 專案概述

ASi Felis Origin is a personal Smart AR Glasses project implementing a TwinCore AI Architecture — edge inference on Jetson Orin NX paired with VITURE Luma Ultra for display and sensing.

ASi Felis Origin 是一個個人 Smart AR Glasses 專案，實作 TwinCore AI 雙核心架構——以 Jetson Orin NX 作為邊緣推論核心，搭配 VITURE Luma Ultra 負責顯示與感知。

**Hardware Stack / 硬體堆疊**
- Display & Sensing：VITURE Luma Ultra（6DoF · RGB 1080p@30fps · Stereo Depth）
- Edge AI Core：NVIDIA Jetson Orin NX 16GB（157 TOPS · Ampere sm_87）
- Inference Engine：Gemma 4 E4B via llama.cpp + Whisper.cpp ASR
- Cloud Core（optional）：MSI Cyborg 14 RT4060（RAG · Gemma 4 26B-A4B）

---

## M1 Milestone / M1 里程碑

This repository contains the M1 Hello World — verifying the full SDK pipeline before the Luma Ultra hardware arrives.

本 repo 包含 M1 Hello World——在實機到貨前完成 SDK 管線的完整驗證。

**Verification targets / 驗收目標**
- [x] CMake build — zero errors on x86_64 (WSL2 Ubuntu 24.04)
- [x] SDK shared libraries load correctly
- [x] `xr_device_provider_create` callable
- [x] Device lifecycle (create → init → start → stop → shutdown → destroy) complete
- [ ] Luma Ultra HID PID confirmed via `lsusb` *(pending hardware)*
- [ ] Device type = `XR_DEVICE_TYPE_VITURE_CARINA (2)` *(pending hardware)*
- [ ] RGB camera frame received & saved *(pending hardware)*
- [ ] IMU data stream verified *(pending hardware)*
- [ ] 6DoF pose polling verified *(pending hardware)*

---

## Project Structure / 目錄結構

```
Project ASi Felis Origin/
├── main.cpp              # M1 Hello World — full SDK pipeline
├── CMakeLists.txt        # Cross-platform build (x86_64 / arm64 / Jetson)
├── README.md
├── .gitignore
└── sdk/                  # VITURE XR Glasses SDK (not tracked in git)
    ├── include/          # Headers — tracked
    │   ├── viture_camera_provider.h
    │   ├── viture_device_carina.h
    │   ├── viture_glasses_provider.h
    │   └── ...
    ├── x86_64/           # .so files — NOT tracked (add manually)
    └── aarch64/          # .so files — NOT tracked (add manually)
```

> **Note / 注意：** SDK `.so` files are excluded from git (`.gitignore`).  
> Download from [VITURE Developer Portal](https://developer.viture.com) and place in `sdk/x86_64/` or `sdk/aarch64/`.

---

## Build / 編譯

### Prerequisites / 前置需求

```bash
# Ubuntu 22.04 / 24.04 (WSL2 or Jetson JetPack 6.2)
sudo apt install cmake g++ pkg-config libusb-1.0-0-dev
```

### x86_64 (Development / WSL2)

```bash
mkdir build && cd build
cmake ..                        # defaults to x86_64
cmake --build . -j4
./asi_hello
```

### arm64 (Jetson Orin NX)

```bash
mkdir build && cd build
cmake .. -DVITURE_ARCH=arm64    # maps to sdk/aarch64/
cmake --build . -j6
./asi_hello
```

---

## First Run / 首次執行

After Luma Ultra arrives / 眼鏡到貨後：

```bash
# 1. Confirm HID PID / 確認 HID PID
lsusb | grep 0c45
# Expected / 預期：
# Bus XXX Device XXX: ID 0c45:XXXX  ← HID (6DoF/IMU)
# Bus XXX Device XXX: ID 0c45:636b  ← UVC (RGB Camera, confirmed)

# 2. Update PID in main.cpp line 36
# static constexpr int LUMA_ULTRA_GLASSES_PID = 0xXXXX;

# 3. Add udev rule (run without sudo)
sudo tee /etc/udev/rules.d/99-viture.rules << 'UDEV'
SUBSYSTEM=="usb", ATTRS{idVendor}=="0c45", MODE="0666"
SUBSYSTEM=="video4linux", ATTRS{idVendor}=="0c45", MODE="0666"
UDEV
sudo udevadm control --reload-rules && sudo udevadm trigger

# 4. Build & run
mkdir build && cd build && cmake .. && cmake --build . -j4
./asi_hello
```

**Expected output on success / 成功預期輸出：**
```
[Init] Device type: 2 (expected CARINA=2)          ← ✅
[Camera] ✅ Saved: hello_frame.ppm (1920x1080 RGB)  ← ✅
[Pose/Poll] pos=(0.000, 0.000, 0.000) ... status=stable  ← ✅
```

---

## SDK Notes / SDK 說明

| Header | Purpose |
|---|---|
| `viture_glasses_provider.h` | Device lifecycle · HID interface |
| `viture_camera_provider.h` | RGB camera · UVC · 1080p@30fps MJPEG |
| `viture_device_carina.h` | Carina (Luma Ultra) · Pose · IMU · Depth stereo |

**Known VID/PID / 已知 VID/PID：**

| Device | Interface | VID | PID |
|---|---|---|---|
| Luma Ultra | HID (6DoF/IMU) | 0x0C45 | TBD via `lsusb` |
| Luma Ultra | UVC (RGB Camera) | 0x0C45 | 0x636B ✅ |

---

## Roadmap / 開發路線

| Milestone | Target | Status |
|---|---|---|
| M1 | Hardware validation · SDK Hello World | 🔄 In Progress |
| M2 | RGB pipeline · MJPEG decode · Gemma 4 E4B vision inference | ⏳ |
| M3 | Whisper.cpp ASR · Voice command integration | ⏳ |
| M4 | Function Calling Agent · 5 Tools | ⏳ |
| M5 | 6DoF AR overlay integration | ⏳ |
| M6 | End-to-end demo · Blog post | ⏳ |

---

## License / 授權

Personal research project. Not for redistribution.  
個人研究專案，不開放轉發。

---

*ASi Felis Origin v2.0 · April 2026*
