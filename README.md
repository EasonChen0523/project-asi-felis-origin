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

**Development Platforms / 開發平台**
- M2-M3 主開發平台：Windows 11 (MSVC) — VITURE SDK 官方 first-class 支援、繞開 WSL2 USB 複合裝置死鎖風險
- 輔助開發 / 推論：WSL2 Ubuntu 24.04 (gcc) — 跑 Whisper / E4B / PaddleOCR
- M4 部署目標：Jetson Orin NX (L4T aarch64)

---

## M1 Milestone / M1 里程碑

This repository contains the M1 Hello World — verifying the full SDK pipeline before the Luma Ultra hardware arrives.

本 repo 包含 M1 Hello World——在實機到貨前完成 SDK 管線的完整驗證。

**Verification targets / 驗收目標**
- [x] CMake build — zero errors on Linux x86_64 (WSL2 Ubuntu 24.04)
- [x] SDK shared libraries load correctly
- [x] `xr_device_provider_create` callable
- [x] Device lifecycle (create → init → start → stop → shutdown → destroy) complete
- [ ] CMake build — zero errors on Windows x86_64 (MSVC) *(M2 prep)*
- [ ] Luma Ultra HID PID confirmed via `lsusb` / Device Manager *(pending hardware)*
- [ ] Device type = `XR_DEVICE_TYPE_VITURE_CARINA (2)` *(pending hardware)*
- [ ] RGB camera frame received & saved *(pending hardware)*
- [ ] IMU data stream verified *(pending hardware)*
- [ ] 6DoF pose polling verified *(pending hardware)*

---

## Project Structure / 目錄結構

```
Project ASi Felis Origin/
├── main.cpp                    # M1 Hello World — full SDK pipeline
├── CMakeLists.txt              # Cross-platform build (Windows / Linux x86_64 / Linux aarch64)
├── README.md
├── .gitignore
└── sdk/                        # VITURE XR Glasses SDK (binaries not tracked in git)
    ├── include/                # Headers — tracked
    │   ├── viture_camera_provider.h
    │   ├── viture_device_carina.h
    │   ├── viture_glasses_provider.h
    │   └── ...
    ├── windows_x86_64/         # Windows .dll/.lib — NOT tracked (M2 main platform)
    ├── linux_x86_64/           # Linux .so — NOT tracked
    └── linux_aarch64/          # Jetson .so — NOT tracked (M4 deployment)
```

> **Note / 注意：** SDK binary files are excluded from git (`.gitignore`).
> Download from [VITURE Developer Portal](https://www.viture.com/developer) and place in the corresponding platform directory.

**Naming Convention / 命名慣例**
目錄採 `{os}_{arch}` 格式，與 VITURE 官方下載檔名一致：
- `VITURE_XR_Glasses_SDK_for_Windows_x86_64` → `sdk/windows_x86_64/`
- `VITURE_XR_Glasses_SDK_for_Linux_x86_64`   → `sdk/linux_x86_64/`
- `VITURE_XR_Glasses_SDK_for_Linux_arm64`    → `sdk/linux_aarch64/` *(注意：VITURE 用 `arm64`，目錄用 `aarch64` 對齊 CMake `CMAKE_SYSTEM_PROCESSOR`)*

---

## Build / 編譯

### Windows x86_64 (M2 Main Platform)

**Prerequisites / 前置需求**
- Visual Studio 2022 (with "Desktop development with C++" workload)
- CMake 3.16+
- VITURE SpaceWalker for Windows (includes WinUSB driver — required for SDK to enumerate device)
- Unzip `VITURE_XR_Glasses_SDK_for_Windows_x86_64` → `sdk/windows_x86_64/`

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
.\build\Release\asi_hello.exe
```

CMake 預設自動偵測為 `windows_x86_64`，無需傳 `-DVITURE_PLATFORM`。

### Linux x86_64 (WSL2 / Native)

**Prerequisites / 前置需求**

```bash
# Ubuntu 22.04 / 24.04 (WSL2 or Native)
sudo apt install cmake g++ pkg-config libusb-1.0-0-dev
```

```bash
mkdir build && cd build
cmake ..                                    # auto-detected as linux_x86_64
cmake --build . -j4
./asi_hello
```

### Linux aarch64 (Jetson Orin NX)

```bash
# Jetson JetPack 6.2 (Ubuntu 22.04 base)
sudo apt install cmake g++ pkg-config libusb-1.0-0-dev
```

```bash
mkdir build && cd build
cmake .. -DVITURE_PLATFORM=linux_aarch64    # explicit override
cmake --build . -j6
./asi_hello
```

> Jetson 的 `CMAKE_SYSTEM_PROCESSOR` 通常會自動偵測為 `aarch64`，理論上不傳 `-DVITURE_PLATFORM` 也可以。明示傳遞是為了交叉編譯場景（例如 x86_64 host 編譯 aarch64 target）。

---

## First Run / 首次執行

After Luma Ultra arrives / 眼鏡到貨後：

### Windows

```powershell
# 1. Install SpaceWalker for Windows (driver included)
#    https://www.viture.com/download

# 2. Plug in Luma Ultra → confirm device enumeration
#    Open "Device Manager" → look for VITURE entries under
#    "Universal Serial Bus controllers" / "Audio inputs and outputs" /
#    "Cameras" / "Human Interface Devices"

# 3. Build & run
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
.\build\Release\asi_hello.exe
```

### Linux

```bash
# 1. Confirm HID PID / 確認 HID PID
lsusb | grep -iE '0c45|35ca'
# Expected / 預期：
# Bus XXX Device XXX: ID 0c45:XXXX  ← HID (6DoF/IMU)
# Bus XXX Device XXX: ID 0c45:636b  ← UVC (RGB Camera, confirmed)
# Bus XXX Device XXX: ID 35ca:1102  ← UAC (Microphone)

# 2. Update PID in main.cpp line 36
# static constexpr int LUMA_ULTRA_GLASSES_PID = 0xXXXX;

# 3. Add udev rule (run without sudo)
sudo tee /etc/udev/rules.d/99-viture.rules << 'UDEV'
SUBSYSTEM=="usb", ATTRS{idVendor}=="0c45", MODE="0666"
SUBSYSTEM=="usb", ATTRS{idVendor}=="35ca", MODE="0666"
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

### Public Headers

| Header | Purpose |
|---|---|
| `viture_glasses_provider.h` | Device lifecycle · HID interface |
| `viture_camera_provider.h` | RGB camera · UVC · 1080p@30fps MJPEG |
| `viture_device_carina.h` | Carina (Luma Ultra) · Pose · IMU · Depth stereo |

詳細子系統覆蓋對照：見 [`SDK_COVERAGE.md`](./SDK_COVERAGE.md)

### Runtime Dependencies (Windows)

SDK Windows 包包含以下 6 個檔案，CMake 會自動拷貝 `.dll` 到輸出目錄：

| File | Role | Third-party |
|---|---|---|
| `glasses.lib` | MSVC import library | — |
| `glasses.dll` | Public API entry | — |
| `carina_vio.dll` | 6DoF VIO engine (主機端 SLAM 計算) | Eigen, Flatbuffers |
| `opencv_world4100.dll` | Image processing | OpenCV 4.1.0 |
| `glew32.dll` | OpenGL extension wrangler | GLEW |
| `libusb-1.0.dll` | USB / UVC / HID 通訊 | libusb, libuvc, HIDAPI |

詳見 SDK release 內附的 `NOTICE` 檔案。

### Known VID/PID

| Device | Interface | VID | PID |
|---|---|---|---|
| Luma Ultra | HID (6DoF/IMU) | 0x0C45 | TBD via `lsusb` |
| Luma Ultra | UVC (RGB Camera) | 0x0C45 | 0x636B ✅ |
| Luma Ultra | UAC (Microphone) | 0x35CA | 0x1102 ✅ |

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

VITURE SDK redistributed under its own license — see `sdk/LICENSE` and `sdk/NOTICE`.

---

*ASi Felis Origin v2.0 · April 2026*
