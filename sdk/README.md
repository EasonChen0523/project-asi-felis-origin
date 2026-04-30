# VITURE SDK Drop Zone

This directory holds VITURE XR Glasses SDK binaries downloaded from the
[VITURE Developer Portal](https://www.viture.com/developer).

本目錄存放從 [VITURE Developer Portal](https://www.viture.com/developer) 下載的
VITURE XR Glasses SDK binary 檔案。

---

## Layout / 目錄結構

| Directory | VITURE Official Package | Tracked in git |
|---|---|---|
| `include/`           | (shared across all platforms) | ✅ headers tracked |
| `windows_x86_64/`    | `VITURE_XR_Glasses_SDK_for_Windows_x86_64` | ❌ binaries excluded |
| `linux_x86_64/`      | `VITURE_XR_Glasses_SDK_for_Linux_x86_64`   | ❌ binaries excluded |
| `linux_aarch64/`     | `VITURE_XR_Glasses_SDK_for_Linux_arm64`    | ❌ binaries excluded |
| `LICENSE`            | from SDK release root (all platforms share) | ✅ tracked |
| `NOTICE`             | from SDK release root (all platforms share) | ✅ tracked |

> 命名注意：VITURE 官方 ARM 包用 `arm64`，本 repo 目錄用 `aarch64` 對齊
> CMake 的 `CMAKE_SYSTEM_PROCESSOR` 慣例。請勿混淆。

---

## Setup Steps / 安裝步驟

1. Apply for SDK access via VITURE Developer Portal (free, requires form submission)
2. Download the platform package(s) you need
3. Unzip; the official archive contains a `release/` folder with `include/`, `<arch>/`, `LICENSE`, `NOTICE`
4. Copy `release/<arch>/*` into the corresponding directory above
5. (First time only) Copy `release/LICENSE` and `release/NOTICE` to `sdk/` root

---

## Platform-specific Notes / 平台補充

### Windows x86_64 (M2-M3 main dev platform)

Required runtime files (all 6 should appear in `windows_x86_64/`):

- `glasses.lib` — MSVC import library
- `glasses.dll` — public API entry
- `carina_vio.dll` — 6DoF VIO engine (Eigen + Flatbuffers)
- `opencv_world4100.dll` — image processing (OpenCV 4.1.0)
- `glew32.dll` — OpenGL extension wrangler
- `libusb-1.0.dll` — USB / UVC / HID transport

WinUSB driver requirement: install **SpaceWalker for Windows** first
(driver bundled). Without it, libusb cannot claim the device interfaces.

### Linux x86_64

SDK ships its own `libusb-1.0.so.0` — CMake links against it directly.
No system libusb required for development.

### Linux aarch64 (Jetson Orin NX, M4 deployment)

SDK ships only `libcarina_vio.so` and `libglasses.so`; system libusb is used:

```bash
sudo apt install libusb-1.0-0-dev
```

---

## Third-party Components

See `NOTICE` for the full list of OSS components VITURE SDK depends on
(OpenCV, Eigen, Flatbuffers, libusb, libuvc, HIDAPI, yaml-cpp, tiny-AES-c, GLEW).

⚠️ Known discrepancy: NOTICE declares OpenCV 4.2.0, but Windows package ships
OpenCV 4.1.0 (`opencv_world4100.dll`). Linux x86_64 package does ship 4.2.0.
This appears to be a VITURE NOTICE maintenance lag — verify actual `.dll`/`.so`
version when troubleshooting.
