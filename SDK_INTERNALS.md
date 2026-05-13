# VITURE SDK Internals

> Internal architecture, threading model, error codes, and known pitfalls
> of the VITURE XR Glasses SDK — what the official docs don't tell you.
> 從 SDK header 註解、NOTICE 第三方授權清單、與本機 runtime 實測累積的觀察。

This document is the companion to [`SDK_COVERAGE.md`](./SDK_COVERAGE.md).
While `SDK_COVERAGE.md` answers *"what can the SDK do?"*,
this document answers *"how does the SDK actually work internally,
and what surprises lie in wait?"*.

---

## 1. Source Annotation Convention

Every fact in this document is tagged with one of four source markers:

| Tag | Meaning | Strength |
|-----|---------|----------|
| `[doc]` | Stated in official SDK header comments | Strongest — authoritative for code-level behavior |
| `[notice]` | Inferred from the NOTICE file's third-party license list | Medium — structural inference, not assertion |
| `[runtime, SDK vX.Y.Z, <condition>]` | Observed by running our own test program | Weakest — may change across SDK versions or hardware conditions |
| `[product]` | From VITURE marketing materials or third-party product reviews | Medium — useful for product behavior, but may contradict technical reality |

When SDK is upgraded, `[runtime]` claims should be re-verified first.
`[doc]` claims are more stable. `[notice]` claims are structural and
unlikely to change unless VITURE redesigns the SDK. `[product]` claims
require cross-checking — VITURE's own materials sometimes contradict
each other or contradict third-party verification.

---

## 2. SDK Version Identification

There are **three conflicting version strings** you may encounter:

| Source | Version | Notes |
|--------|---------|-------|
| `viture_version.h` macros | **2.2.1** | The authoritative header version `[doc]` |
| Doxygen HTML header | 2.2.0 | One patch behind — Doxygen build snapshot |
| Release Notes page | v2.1.2 (March 2, 2026) | Public announcement, lags actual release |
| Runtime `GetVersionString()` | (matches header) | Use this at runtime to confirm dll/header pairing |

The macros are defined as `[doc]`:

```c
#define VITURE_VERSION_MAJOR 2
#define VITURE_VERSION_MINOR 2
#define VITURE_VERSION_PATCH 1

extern "C" VITURE_API const char* GetVersionString();

namespace viture::version {
    constexpr int kMajor = VITURE_VERSION_MAJOR;
    constexpr int kMinor = VITURE_VERSION_MINOR;
    constexpr int kPatch = VITURE_VERSION_PATCH;
}
```

**Side observation**: the existence of `namespace viture::version` reveals the
SDK itself is C++, even though its public API is C-style. The opaque handles
and function-pointer callbacks are a C facade over a C++ implementation.

---

## 3. Internal Architecture (Inferred from NOTICE)

The NOTICE file shipped with the Windows SDK lists 8 third-party components.
Each can be mapped to one of the 6 DLLs shipped, revealing SDK internal structure.

| DLL | Role | Third-party deps `[notice]` |
|-----|------|----------------------------|
| `glasses.dll` | Public API entry point | (SDK proprietary core) |
| `glasses.lib` | MSVC import library for the above | — |
| `carina_vio.dll` | 6DoF VIO engine (Visual-Inertial Odometry) | Eigen, Flatbuffers |
| `opencv_world4100.dll` | Image processing | OpenCV 4.1.0 |
| `glew32.dll` | OpenGL extension wrangler | GLEW (MIT, not in NOTICE — likely oversight) |
| `libusb-1.0.dll` | USB / UVC / HID transport | libusb 2.1 (LGPL), libuvc, HIDAPI |

Additionally listed in NOTICE but with no dedicated DLL — likely linked into
`glasses.dll` or `carina_vio.dll`:

- **yaml-cpp** — config file parsing (almost certainly device calibration data)
- **tiny-AES-c** — encryption (suggests firmware/device authentication path)

### 3.1 Where the 6DoF Pose Is Actually Computed

This question has **conflicting official narratives** that direct
third-party testing resolves in favor of host-side computation. Read
this section carefully before relying on any tracking behavior assumption.

**Evidence from technical artifacts** `[notice]` `[runtime]`:

- `carina_vio.dll` is 16 MB, depends on Eigen and Flatbuffers, and gets
  loaded into the host process at SDK startup. This is a full
  visual-inertial odometry engine running on the host CPU.
- Runtime logs show "UsbProtocolController: Creating V2 protocol
  instances for Gen2 device" and continuous USB I/O threads operating
  on the host side.

**Evidence from VITURE marketing** `[product]`:

- VITURE blog (July 2025) claims Luma Ultra has *"a proprietary chip
  that handles all 6DoF calculations directly on the glasses themselves"*
- VITURE blog also claims *"6DoF inherently includes 3DoF functionality.
  On Luma Ultra, there isn't a separate 'only 3DoF' mode — it always
  operates in full 6DoF by default."*

**Evidence from third-party direct testing** `[product]`:

- Gaming Nexus review (Dec 2025): *"What it doesn't have, like say the
  XREAL One Pro, is an internal chip to facilitate the processing
  needed for 6DoF. That means you'll need to use some piece of software
  on either your phone, computer, or VITURE's own Neckband Pro in order
  to experience it."*
- KOLA YouTube review *"Over Promised, Under Delivered? — Viture Luma
  Ultra Review"* (subtitle transcript, timestamps 4:48–5:11):
  - *"in order for you to use **three dove or six dove**, you have to
    plug this into your pro neck band, your PC or your mobile phone"*
    — confirms both 3DoF and 6DoF require host software
  - *"If your PC is not powerful enough to run Viture SpaceWalker,
    you're also not gonna get three do[F] and six do[F]"*
    — confirms host-side compute requirement (not merely connectivity)
  - *"If your intention is to use this glasses with something like a
    Nintendo Switch or ROG Ally or one of those little small computer
    or someone like me who flies a drone with my glasses, this thing
    the six dove or the three dove is not going to work"*
    — confirms "dumb host" scenarios have zero tracking
- VITURE's own Amazon product description: 6DoF hand gesture support
  works *"when paired with the VITURE Pro Neckband"* — a conditional
  phrasing absent from blog marketing.

**Reconciliation**:

1. The Luma Ultra eyewear hardware is effectively a **pure sensor
   streamer**: IMU, stereo grayscale cameras, RGB camera, and display.
2. **All** tracking fusion (both 3DoF rotation and 6DoF positional)
   happens on the host running this SDK or VITURE's SpaceWalker app.
   This was previously assumed only for 6DoF, but KOLA's direct testing
   shows 3DoF has the same requirement.
3. The "proprietary chip that handles all 6DoF calculations" marketing
   claim has **no independent technical evidence** and **conflicts with
   direct testing**. It is either factually incorrect or referring to
   an unimplemented future capability.
4. **VITURE Luma Ultra is the most host-dependent model** in VITURE's
   product lineup — counter to intuition, the flagship has *less*
   on-device capability than cheaper models (Beast, Luma, older Pro)
   that offer native on-device 3DoF tracking.

This reconciliation is consistent with all available evidence.
See §8.10 for the practical pitfall this creates.

### 3.2 Other Architectural Implications

**The SDK manages USB independently of the OS UVC/HID stack.** `[notice]`
`libusb + libuvc + HIDAPI` in user-space means the SDK does not rely on
`/dev/video0` (Linux) or DirectShow/MediaFoundation (Windows). It opens
the USB device directly.

Cross-platform consequences:
- Compatible with both Linux and Windows via the same architecture
  (libusb is the portability layer)
- On Windows, requires WinUSB driver — typically installed by SpaceWalker
  for Windows
- Does NOT conflict with applications that grab the camera via OS APIs
  (the SDK never claims the OS UVC interface), but does require
  exclusive access to the underlying USB device

**Internal config likely uses YAML files.** `[notice]` `yaml-cpp` strongly
suggests device calibration data (lens intrinsics, IMU bias, etc.) is
loaded from YAML at `initialize()` time. The `cache_file_dir` parameter
to `xr_device_provider_initialize()` is probably where these get stored.

**Firmware authentication is present.** `[notice]` `tiny-AES-c` in a USB
SDK strongly suggests encrypted command/response with the device firmware,
likely for anti-counterfeit purposes. Cloned or modified devices may fail
authentication and become unusable.

### 3.3 Internal Threading Components

Runtime logs `[runtime, SDK v2.2.1, no device]` reveal SDK internal
subsystems:

```
TaskManager initialized with dual-thread model
UsbProtocolController: Creating V2 protocol instances for Gen2 device
UsbProtocolController created with protocol version: V2
USB response monitoring thread started
ReceiveQueue worker thread started
SendQueue started
```

So the SDK runs (at minimum):

1. **TaskManager** — dual-thread model (purpose unclear, likely work
   queue dispatcher)
2. **ReceiveQueue worker thread** — incoming USB data
3. **SendQueue thread** — outgoing USB commands
4. **USB response monitoring thread** — invokes pose/IMU/VSync/state
   callbacks
5. **Camera thread** (dedicated, separate provider) — invokes camera
   frame callbacks

`xr_device_provider_get_thread_id()` exposes 2 of these thread IDs for
external use (priority tuning, debugging). `[doc]`

---

## 4. API Layering Across Device Generations

The SDK supports three device generations with **intentionally divergent
APIs**. Choosing the wrong header for your device will silently produce
no behavior or return `INVALID_PARAM`.

### 4.1 Device Generation Enum

`[doc, viture_glasses_provider.h]`

```c
typedef enum {
    XR_DEVICE_TYPE_VITURE_GEN1   = 0,
    XR_DEVICE_TYPE_VITURE_GEN2   = 1,
    XR_DEVICE_TYPE_VITURE_CARINA = 2
} XRDeviceType;
```

| Value | Name | Hardware mapping |
|-------|------|------------------|
| 0 | GEN1 | Viture One / One Lite / Pro |
| 1 | GEN2 | Viture Luma / Luma Pro / Beast |
| 2 | CARINA | Viture Luma Ultra |

### 4.2 Header-to-Generation Mapping

| Header | Applies to | Purpose |
|--------|-----------|---------|
| `viture_glasses_provider.h` | All generations | Lifecycle, state callback, log hook |
| `viture_device.h` | **GEN1/GEN2 only** | `open_imu`, raw/pose IMU callbacks (NWU coords) |
| `viture_device_carina.h` | **Carina only** | High-level pose / VSync / IMU / stereo camera callbacks (OpenGL coords) |
| `viture_camera_provider.h` | Devices with RGB camera | Separate provider, separate handle, separate thread |
| `viture_protocol_public.h` | All generations | Display modes, brightness, volume, state IDs (not yet fully audited — see §9) |
| `viture_macros_public.h` | All generations | `LOG_LEVEL_*`, `VITURE_API` export macro |
| `viture_result.h` | All generations | Success / error code definitions |
| `viture_version.h` | All generations | Version macros |

### 4.3 Cross-Generation Asymmetry

The SDK deliberately keeps Carina and GEN1/GEN2 APIs separate because the
underlying sensor pipelines are different:

- GEN1/GEN2: device streams **raw IMU**, optionally device-side processed
  pose; `xr_device_provider_open_imu()` toggles streaming on/off
- Carina: device streams **stereo camera + IMU**, host-side VIO produces
  6DoF pose; no `open_imu` needed (camera + IMU are always-on once
  registered)

`[doc, viture_device.h]`:
> Open imu (no effect for carina device)

`[doc, viture_device_carina.h]`:
> Callbacks registered here are ignored for devices that are not
> `XR_DEVICE_TYPE_VITURE_CARINA`.

---

## 5. Threading Model

### 5.1 Callback Thread Ownership

| Callback | Invoking thread | Source |
|----------|----------------|--------|
| `XRPoseCallback` (Carina) | "USB monitoring thread" | `[runtime]` + `[doc, viture_device.h]` (says "imu read thread" for GEN1/GEN2 — likely renamed/unified for Carina) |
| `XRImuCallback` (Carina) | same | same |
| `XRVSyncCallback` | same | `[runtime]` |
| `GlassStateCallback` | same | `[runtime]` |
| `XRCameraCallback` (Carina stereo) | same | `[doc]` (registered alongside above) |
| `XRCameraFrameCallback` (RGB UVC) | **Dedicated camera thread** | `[doc, viture_camera_provider.h]` |
| `LogHook` | Caller of the log site (i.e. whichever thread emitted the log) | `[runtime]` inferred |

**Important**: the RGB camera callback runs on a **different thread**
from all other callbacks. State shared between `XRCameraFrameCallback`
and (e.g.) `XRPoseCallback` requires actual synchronization primitives —
`std::atomic` is only sufficient for individual primitive values;
compound state needs `std::mutex` or equivalent.

### 5.2 Thread Lifetime

Threads are created at different stages:

- `ReceiveQueue` / `SendQueue` / `TaskManager`: at `xr_device_provider_create()` `[runtime]`
- `USB monitoring thread`: at `xr_device_provider_initialize()` `[runtime]`
- `Camera thread`: at `xr_camera_provider_start()` `[doc]`

All are torn down in reverse order during stop/shutdown/destroy.

---

## 6. Coordinate Systems & IMU Data Formats

The SDK uses **different coordinate systems and data layouts per
generation**. Mixing them in a "unified IMU abstraction" requires
explicit conversion.

### 6.1 IMU Data Layout

`[doc]`

| Generation | Buffer length | Layout |
|-----------|---------------|--------|
| GEN1 (One/Pro/Lite) | 10 floats | `[gx, gy, gz, ax, ay, az, 0, 0, 0, temp]` (3 zero pads where magnetometer would be) |
| GEN2 (Luma/Luma Pro/Beast) | 10 floats | `[gx, gy, gz, ax, ay, az, mx, my, mz, temp]` |
| Carina (Luma Ultra) | 6 floats | `[ax, ay, az, gx, gy, gz]` (no magnetometer, no temp) |

**Note**: GEN1/GEN2 are *gyro-first*, Carina is *accel-first*. This is
intentional but easy to miss. A future "unified IMU layer" must convert
explicitly — there is no documented utility for this.

### 6.2 Pose Coordinate Systems

`[doc]`

| API | Coordinate system |
|-----|-------------------|
| GEN1/GEN2 `VitureImuPoseCallback` | North-West-Up (NWU): X→North, Y→West, Z→Up |
| Carina `XRPoseCallback` | (timestamp + 32-float buffer, format under-documented — see §9) |
| Carina `get_gl_pose_carina()` | OpenGL right-handed: X→right, Y→up, Z→backward |

The Carina world frame internally is the **`Twb` transform** (transform
from world to body, standard SLAM/VIO notation). `get_gl_pose_carina()`
returns a re-projection of this into OpenGL coordinates suitable for
direct use in graphics pipelines.

### 6.3 Gravity-Anchored Pitch and Roll

`[doc, viture_device_carina.h, reset_origin_carina]`

> Pitch and roll are gravity-anchored by the Carina VIO system and are
> NOT affected by this call regardless of the quaternion passed. They
> always reflect the device's absolute orientation relative to gravity.

This is a standard VIO design choice (gravity vector provides an absolute
reference for two of the three rotational degrees of freedom), but it has
practical consequences:

- `reset_origin_carina()` only resets **position and yaw**
- Even if you pass a non-identity quaternion to it, pitch/roll are ignored
- Tilting your head will always reflect in the pose output, regardless of
  reset operations

---

## 7. Error Code Reference

`[doc, viture_result.h]`

| Code | Macro | Meaning |
|------|-------|---------|
| 0 | `VITURE_GLASSES_SUCCESS` | Operation completed successfully |
| -1 | `VITURE_GLASSES_ERROR_INVALID_PARAM` | Null handle, null output pointer, or argument out of range |
| -2 | `VITURE_GLASSES_ERROR_USB_UNAVAILABLE` | USB connection not available or not established |
| -3 | `VITURE_GLASSES_ERROR_USB_EXEC` | USB read/write operation failed |
| -4 | `VITURE_GLASSES_ERROR_NOT_SUPPORTED` | Feature not supported by this device model |
| -5 | `VITURE_GLASSES_ERROR_NO_DATA` | No valid response data (timeout or empty) |
| -6 | `VITURE_GLASSES_ERROR_DATA_PARSE` | Response data format or length mismatch |
| -7 | `VITURE_GLASSES_ERROR_DEVICE_REJECTED` | Device firmware rejected the command |
| -8 | `VITURE_GLASSES_ERROR_CALIB_INIT` | Calibration initialization failed (`initialize` only) |
| -9 | `VITURE_GLASSES_ERROR_SERIAL_FETCH` | Serial number retrieval failed (`initialize` only) |
| -10 | `VITURE_GLASSES_ERROR_INVALID_STATE` | Operation invalid in current state (e.g. already streaming, not initialized) |
| -99 | `VITURE_GLASSES_ERROR_UNKNOWN` | Unclassified error |

### 7.1 Per-API Error Code Subset

Each API documents which error codes it can return — only a subset of the
full table is applicable per call. Always check the header for the
specific API.

For example, `xr_camera_provider_start()` documents `[doc]`:

- `-1` invalid handle
- `-2` USB_UNAVAILABLE — camera device not found or failed to open
- `-3` USB_EXEC — failed to start streaming
- `-4` NOT_SUPPORTED — failed to negotiate stream format
- `-10` INVALID_STATE — already streaming

While `xr_device_provider_initialize()` can additionally return `-8` and
`-9` (which no other API uses).

### 7.2 Observed Error Codes (Runtime)

`[runtime, SDK v2.2.1, no device]`

When running the Hello World with no device connected and PID = 0x0000:

| API call | Returned code | Interpretation |
|----------|---------------|----------------|
| `xr_device_provider_create(0x0000)` | non-NULL handle | **Does not fail at construction**; see §8.1 |
| `xr_device_provider_initialize(handle, NULL, NULL)` | 0 (SUCCESS) | Initialize succeeds even with no device |
| `xr_device_provider_get_device_type(handle)` | 1 (GEN2) | Fallback to GEN2 default; see §8.2 |
| `xr_device_provider_register_callbacks_carina(...)` | -1 (INVALID_PARAM) | Likely because internal device type is not CARINA |
| `xr_device_provider_start(handle)` | 0 (SUCCESS) | Start also succeeds (only thread orchestration) |
| `xr_camera_provider_create(0x0C45, 0x636B)` | non-NULL handle | Construction does not check USB presence |
| `xr_camera_provider_start(...)` | -2 (USB_UNAVAILABLE) | This is where actual USB enumeration happens |

---

## 8. Known Pitfalls & Counterintuitive Behavior

中文踩雷紀錄區。這些是「文件沒寫但你會踩到」的事，按踩雷嚴重程度排序。

### 8.1 `create()` 不檢查 PID 也不檢查裝置存在

`[runtime, SDK v2.2.1, no device]`

`xr_device_provider_create(product_id)` 傳入任何整數（包括 0x0000、0xFFFF、
某些 AI 生成的「看起來合理」的值）都會回傳 non-NULL handle，並啟動完整
thread pool（TaskManager + ReceiveQueue + SendQueue + UsbProtocolController）。

實際的失敗發生在後續步驟：
- `register_callbacks_carina()` 回 `-1`（因為內部裝置類型不對）
- `xr_camera_provider_start()` 回 `-2`（USB 找不到裝置）

**陷阱**：不要把 `create()` 成功當作「裝置存在」的證據。`create()` 只是配置
internal state。Sentinel value PID（例如 0x0000）能順利通過 create，並讓
你以為「程式至少跑到一半」，但實際從未跟硬體通訊。

**建議做法**：用 `xr_device_provider_get_device_type()` 的回傳值來判斷裝置
是否真的連上——如果回傳值符合預期的 device type（例如 Carina = 2），才能
確認 USB 通訊正常。

### 8.2 `get_device_type()` 沒裝置時 fallback 為 GEN2 (=1)

`[runtime, SDK v2.2.1, no device]`

沒裝置 + 任意 PID 的情況下，`get_device_type()` 回 1 (GEN2)，runtime log 也
顯示「V2 protocol for Gen2 device」、「VitureGen2DeviceProvider」。

可能解釋：SDK 內部 fallback 邏輯預設用「最新一代非 Carina 裝置」當 default
scaffolding，等實際 USB 通訊建立後才校正。

**陷阱**：device_type = 1 不代表你的硬體是 Luma 或 Beast——可能根本沒裝置
連上。判斷裝置真實連線必須結合 `device_type` + 後續 USB 通訊成功
（例如 `xr_camera_provider_start` 回 0）。

### 8.3 `is_valid_camera()` 不是 presence check

`[doc + runtime, SDK v2.2.1, no device]`

```c
xr_camera_provider_is_valid_camera(0x0C45, 0x636B)  // returns 1
```

即使眼鏡沒插上，這個 API 仍會回 1。它只檢查「VID/PID 組合是 SDK 認知裡的
支援裝置嗎」，**不檢查 USB bus 上是否實際存在該裝置**。

文件命名容易誤導。實際做 presence check 唯一可靠方式：直接呼叫
`xr_camera_provider_start()` 看回傳值。

### 8.4 `predict_time` 單位是 nanoseconds，但 type 是 `double`

`[doc, viture_device_carina.h]`

```c
int xr_device_provider_get_gl_pose_carina(
    XRDeviceProviderHandle handle,
    float *pose,
    double predict_time,    // ← nanoseconds, double type
    int *pose_status);
```

直覺上 `double` 應該配 seconds，但這裡是 nanoseconds。Forward prediction 範例：

| Frame rate | Frame time | predict_time 該傳 |
|-----------|-----------|-------------------|
| 60 Hz | 16.67 ms | `16_666_666.0` (NOT `0.0167`) |
| 90 Hz | 11.11 ms | `11_111_111.0` |
| 120 Hz | 8.33 ms | `8_333_333.0` |
| current pose (no prediction) | — | `0.0` |

**陷阱**：傳 `0.0167` 給「60Hz 預測」是「預測 0.0167 ns 之後」≈ 0，
拿到的等於「目前 pose」而非「下一幀 pose」。AR overlay 會跟頭部移動有
延遲但 debug 不易，因為函式回傳值都正常。

### 8.5 RGB camera 永遠回 MJPEG，不回 RGB

`[doc, viture_camera_provider.h]`

```
Camera uses fixed configuration: 1920x1080@30fps, MJPEG format
```

`XRCameraFormat` enum 列了 RGB / YUYV / NV12 / GRAY 共 4 個額外格式，看似
camera 可以多格式輸出。**實際上 SDK 一律回 MJPEG**，其他格式定義為了將來
擴充。

**陷阱**：任何「if frame->format == XR_CAMERA_FORMAT_RGB」的分支永遠是
dead code。如果你想要 RGB，必須自己拿 MJPEG 解碼（OpenCV `imdecode` 或
libjpeg-turbo）。

### 8.6 `log_hook` 不會 suppress SDK 內建 stdout

`[runtime, SDK v2.2.1, no device]`

註冊 `xr_device_provider_set_log_hook()` 後，每條 log 訊息會在 stdout 出現
兩次：
```
[I][libglasses] ReceiveQueue worker thread started.        ← SDK 自己 print
[SDK/INFO/libglasses] ReceiveQueue worker thread started.  ← 透過 hook callback
```

`log_hook` 是「加上自訂處理」而非「取代預設處理」。`[doc]` 寫得很明確：

> When a log hook is set, all log messages will be passed to the callback
> **in addition to** the default logging mechanism

如果你不想看到重複訊息，目前沒有 API 關閉內建 stdout 輸出。
`xr_device_provider_set_log_level(LOG_LEVEL_NONE)` 會同時關閉兩邊，無法
只關內建保留 hook。

### 8.7 `set_dof_type_carina` 必須在 `initialize` 之前呼叫

`[doc, viture_device_carina.h]`

```
Must be called after xr_device_provider_create and before
xr_device_provider_initialize. Default is 6DOF.
```

呼叫順序固定：

```
create → set_dof_type_carina → initialize → register_callbacks_carina → start
```

`set_dof_type_carina` 的參數命名為 `is_6dof`（bool-like flag），合法值只有
0 (3DOF) 和 1 (6DOF)，**不是 enum**。`int` type 是 C API 跨語言相容性
所致。

### 8.8 `register_callbacks_carina` 文件說「ignored」但實測 return -1

`[doc + runtime, SDK v2.2.1, no device]`

文件原文：
> Callbacks registered here are ignored for devices that are not
> `XR_DEVICE_TYPE_VITURE_CARINA`.

但 runtime 沒 Carina 裝置時實測 return `-1 (INVALID_PARAM)`，不是 silently
ignore。可能解讀：
1. 文件不精確：實際會 return error
2. 「ignored」指 callback 註冊上去但未來不會被呼叫，return 值另有其因

**結論**：等實機驗證。沒 Carina 但有其他裝置（例如 GEN2）時，此 API 行為
仍待確認。

### 8.9 IMU 順序在 Carina 跟 GEN1/GEN2 是反的

`[doc]`

| Generation | First three values | Next three values |
|-----------|--------------------|-------------------|
| GEN1/GEN2 | gyroscope (gx, gy, gz) | accelerometer (ax, ay, az) |
| Carina | **accelerometer (ax, ay, az)** | **gyroscope (gx, gy, gz)** |

**陷阱**：寫跨 generation IMU 處理時，不能直接 memcpy 共用 buffer。複用
程式碼必須顯式對應每個欄位。

### 8.10 「無主機端 = 完全無 tracking」（連 3DoF 都沒有）

`[product]` `[notice]`

VITURE 對 Luma Ultra tracking 能力的說法在不同管道有顯著差異：

| 來源 | 說法 |
|---|---|
| VITURE 部落格 (Jul 2025) | 眼鏡內建「proprietary chip that handles all 6DoF calculations directly on the glasses themselves」 |
| VITURE 部落格 (同篇) | 「6DoF inherently includes 3DoF... no separate 'only 3DoF' mode — always operates in full 6DoF by default」 |
| VITURE 產品頁 (最新) | 「full 6DoF support, **not only on Pro Neckband but also across macOS, Windows**」（言外之意：仍須主機端） |
| VITURE Amazon 描述 | 6DoF + 手勢支援「**when paired with the VITURE Pro Neckband**」（明確 conditional） |
| Gaming Nexus 評測 (Dec 2025) | 「**no internal chip** to facilitate 6DoF processing... you'll need to use some piece of software on either your phone, computer, or Neckband Pro」 |
| KOLA YouTube 評測（直接觀察） | 無論 3DoF 還是 6DoF 都需要 SpaceWalker 跑在主機上；「dumb host」場景（Switch / ROG Ally / 無人機遙控器）連 3DoF 都沒有 |

KOLA 評測的關鍵字幕（時間軸 4:48–5:11）直接觀察到：

> *"in order for you to use **three dove or six dove**, you have to plug
> this into your pro neck band, your PC or your mobile phone... If your
> PC is not powerful enough to run Viture SpaceWalker, you're also not
> gonna get three do[F] and six do[F]"*

> *"If your intention is to use this glasses with something like a
> Nintendo Switch or ROG Ally... or someone like me who flies a drone
> with my glasses, this thing the six dove or the three dove is not
> going to work"*

技術證據（§3.1）顯示 `carina_vio.dll` 16 MB 的 VIO engine 在**主機端**
跑——KOLA 評測進一步證實這不只是 6DoF 的問題，**連 3DoF 都需要主機端 SDK**。

**最終事實**：
- 眼鏡是純粹的 sensor streamer（IMU + stereo cameras + RGB camera + display）
- 眼鏡端**沒有任何 tracking fusion**——所有 3DoF rotation 與 6DoF positional
  都在主機端的 SDK / SpaceWalker 計算
- VITURE 部落格「always 6DoF default」說法成立的隱含條件是「**主機端
  SDK 或 SpaceWalker 已連線且能正常運行**」
- VITURE 部落格「proprietary on-device 6DoF chip」說法**無獨立證據支持**，
  與直接觀察衝突

**陷阱**：

1. **「dumb host」場景完全沒 tracking**：Nintendo Switch、ROG Ally、無人機
   遙控器、舊款手機等不能跑 SpaceWalker 的裝置接 Luma Ultra，只能當外接
   螢幕用，沒有任何 tracking
2. **「主機算力門檻」比想像中高**：不只「能裝 SpaceWalker」，要「跑得動」。
   若主機端 VIO 跑不順，tracking 會 degrade 甚至完全失效。對 ASi Felis 而言
   Jetson Orin NX 需要實機 benchmark 確認 VIO + LLM + Whisper 同時跑的 CPU
   預算
3. **Luma Ultra 是 VITURE 最依賴主機的型號**（反直覺）：VITURE 其他產品
   （Beast、Luma、舊款 Pro）反而有 native 3DoF on-device 處理。買 Luma Ultra
   不能假設「旗艦 = 最獨立」——是「旗艦 = 最依賴 SDK」
4. **行銷 vs 真實能力**：「proprietary 6DoF chip」這類 marketing claim
   不應該影響架構設計判斷。決策時以 §3.1 的技術證據為準

**TBD（§9）**：實機到貨後驗證——確認在不同主機條件下眼鏡的真實行為，
分離「沒 SDK 跑」vs「SDK 跑不動」兩種失敗模式。

---

## 9. Unverified / TBD

明確標示「還沒查、不要假設」的事項。

| Topic | Why unverified | Plan |
|-------|---------------|------|
| `XRPoseCallback` 的 32-float buffer 詳細格式 | `[doc]` 只說 "Array of 32 float pose data"，未說明每個 index 對應什麼。已知前 7 個是 `[px, py, pz, qw, qx, qy, qz]` (從 `get_gl_pose_carina` 推測)，後 25 個未知 | 等實機驗證後逐欄位 dump |
| Display mode 完整列表 | `viture_protocol_public.h` 尚未審閱（33 KB header） | M5 渲染開發時讀 |
| State callback `glass_state_id` 與 `glass_value` 的合法值 | `viture_protocol_public.h` 內有 `VITURE_CALLBACK_ID_*` 常數，尚未審閱 | M5 渲染開發時讀 |
| `viture_protocol_public.h` 的 brightness / volume / film mode 命令 | 尚未審閱 | M3-M5 階段需要時讀 |
| Carina 不接裝置時 `register_callbacks_carina` 為何 return -1 | 文件說 "ignored" 但實測 return error | 等實機驗證 |
| Camera frame `XRCameraFrame::data` 的 ownership / lifetime | `[doc]` 寫 "valid only during callback" 但沒說明 buffer 是否在 callback 結束後被 SDK 重用 | 假設「callback 結束即失效」，必要時 deep copy |
| `cache_file_dir` 實際存什麼檔案 | NOTICE 推論是 calibration YAML，未實證 | 實機跑後檢視該目錄 |
| `XRImuCallback` (Carina) 的呼叫頻率 | 文件未明說，runtime 未驗證 | 實機到貨後測 |
| `XRVSyncCallback` 觸發頻率與抖動 | 文件未明說 | 實機到貨後測，M5 渲染同步關鍵 |
| Pose callback 文件寫 "25Hz" 但有問號字元 `25?浹z` | Doxygen 編碼或原註解格式問題，可能是 25 Hz | 觀察實機觀察 callback 間隔確認 |
| Encryption (tiny-AES-c) 的實際用途 | `[notice]` 推論是 firmware 認證，無證據 | 不影響使用，記為背景知識 |
| **無主機端 SDK 時眼鏡的行為** | KOLA 評測說無 SpaceWalker 完全無 tracking，但「**接 PC 但沒跑任何 SDK / SpaceWalker**」狀態下，眼鏡 IMU 串流是否仍在 USB 上活躍？或眼鏡端會自動進入低耗電 idle？ | 實機到貨後測三組對照：(1) 接 PC 但不跑任何 SDK; (2) 接 PC 跑 SpaceWalker; (3) 接低算力主機（如 Switch）只當螢幕用 |
| Luma Ultra 與 VITURE 其他型號的 SDK 行為差異 | KOLA 觀察 Beast / Luma / 舊款 Pro 有 on-device 3DoF，但 SDK header 將 GEN1/GEN2/CARINA 三條 API 線分開——SDK 跑這些型號時 host CPU 負載是否真的差異很大？ | 取得多代裝置實測（需額外硬體投入）或社群既有 benchmark |

---

## 10. Document Revision Log

- **v0.1** (2026-05-13): Initial draft based on:
  - SDK headers `viture_result.h`, `viture_version.h`, `viture_macros_public.h`,
    `viture_device.h`, `viture_device_carina.h`, `viture_camera_provider.h`,
    `viture_glasses_provider.h` (SDK v2.2.1)
  - NOTICE file (Windows SDK v2.2.1 release)
  - Runtime observations from `asi_hello.exe` on Windows x86_64,
    no Luma Ultra hardware connected, PID = 0x0000 sentinel
  - `viture_protocol_public.h` deliberately excluded (TBD §9)

- **v0.2** (2026-05-13, internal — superseded by v0.3): First attempt
  at triangulating 6DoF computation location, introduced `[product]`
  source tag. Tentatively concluded that "rotational 3DoF may be
  on-device IMU sufficient". Not committed to git; superseded by v0.3
  before commit.

- **v0.3** (2026-05-13): Sharpened §3.1 based on KOLA YouTube subtitle
  transcript (timestamps 4:48–5:11) showing both 3DoF and 6DoF require
  host-side SpaceWalker:
  - Eyewear is a pure sensor streamer; no on-device tracking fusion
    of any kind
  - Renamed §8.10 to reflect that "no host = no tracking at all",
    not "no host = 3DoF only"
  - Added observation that Luma Ultra is the *most* host-dependent
    model in VITURE's lineup (Beast/Luma/older Pro have on-device 3DoF)
  - Concluded the "proprietary on-device 6DoF chip" marketing claim
    has no independent technical evidence and conflicts with direct
    testing
  - Added §9 TBD entry distinguishing "no SDK running" vs "SDK can't
    keep up" failure modes
  - Sources added: KOLA YouTube *"Over Promised, Under Delivered? —
    Viture Luma Ultra Review"* subtitle transcript

---

*Companion to [`SDK_COVERAGE.md`](./SDK_COVERAGE.md) ·
Part of [Project ASi Felis Origin](./README.md)*
