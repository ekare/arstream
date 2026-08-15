# iOS/ARKit Handover Document

This document exists so that once a Mac (Mini or otherwise) is available,
the iOS side doesn't have to start from scratch. It explains how to build
the iOS counterpart of the architecture already built, tested, and verified
on the A30s on the Android side (`mobile/native/`) — it doesn't write code
(can't be compiled/tested without a Mac), but it pins down EXACTLY what
needs to be written, which files are already ready, which part is even
easier than Android, and the open questions that need investigating.

**Read first:** [`ARCHITECTURE.md`](ARCHITECTURE.md) (especially the "Key
decisions" and "JNI bootstrap exception" sections) and
[`PROTOCOL.md`](PROTOCOL.md). Everything below builds on those.

---

## 1. The good news: iOS will be SIMPLER than Android

Android's biggest friction point was this: Godot's GDExtension has no
official way to reach the `JNIEnv*`/`Activity` context on Android (see
[godot-proposals #6734](https://github.com/godotengine/godot-proposals/issues/6734)),
so a tiny Kotlin "bootstrap shim" was needed just to start ARCore.

**iOS doesn't have this problem.** Objective-C++ (`.mm`) is a superset of
C++ — you can call ARKit, AVFoundation, CoreMotion classes (`ARSession`,
`AVCaptureSession`, `CMMotionManager`) DIRECTLY, with no bridge/context
handoff at all. So on iOS, not only will there be **no business logic in
Swift, there won't even be a need for a tiny bootstrap file like Android's**
— just `.mm` files where ARKit/AVFoundation calls are interleaved directly
into otherwise plain C++ logic.

The one real exception is that ARKit/AVFoundation/CoreMotion have no C API
— so any code touching these three frameworks must be `.mm` (Obj-C++), but
the logic inside those files is still our own code, not some third-party
"plugin system."

**VideoToolbox (the H.264 encoder), on the other hand, IS a C API**
(`VTCompressionSession`, `<VideoToolbox/VideoToolbox.h>`) — exactly the same
situation as `AMediaCodec` on Android. So `encode/h264_encoder_ios.cpp` **can
be written as plain C++**, it doesn't even need to be `.mm`.

---

## 2. Reusable as-is (already ready, don't touch)

None of these are Android-specific — platform-independent C++ that will
build and run on iOS exactly as-is:

| File | Note |
|---|---|
| `net/protocol.h`, `net/protocol.cpp` | Doesn't even depend on godot-cpp, fully portable. Already tested via `mobile/native/tests/protocol_test.cpp`. |
| `sink/output_sink.h` | Interface, no platform code. |
| `sink/file_sink.h/.cpp` | `fopen`/`fwrite` — POSIX, works identically on iOS. |
| `sink/stream_sink.h/.cpp` | Memory-first/disk-overflow buffer logic, `std::thread`/`std::mutex` — all portable. **Received a small fix recently** (see §6). |
| `net/stream_client.h/.cpp` | POSIX BSD sockets (`socket`, `connect`, `send`, `getaddrinfo`) — identical API on Android and iOS/Darwin. **Recently opened up for `IOS_ENABLED` too** (see §6). |

`ArCapture` (`ar_capture.h/.cpp`) also stays largely the same — an
`#ifdef IOS_ENABLED` block just needs to be added parallel to the existing
Android-specific `#ifdef ANDROID_ENABLED` blocks (see §4).

---

## 3. New files to be written (with their Android counterpart)

| iOS | Android counterpart | What it does |
|---|---|---|
| `capture/ios/arkit_capture_session.h/.mm` | `capture/android/camera2_capture_session.h/.cpp` | Starts an `ARSession` (`ARWorldTrackingConfiguration`), receives frame+pose+point-cloud+intrinsics via `ARSessionDelegate`. |
| `capture/ios/avf_capture_session.h/.mm` | Camera2CaptureSession's fallback role | Raw capture via `AVCaptureSession` + `AVCaptureVideoDataOutput` when ARKit is unavailable/not wanted. |
| `capture/ios/imu_sampler_ios.h/.mm` | `ImuSampler` (never written as a separate file on Android, would have been added if NDK `ASensorManager` were needed) | Raw accelerometer/gyro via `CMMotionManager`. |
| `encode/h264_encoder_ios.h/.cpp` | `encode/h264_encoder_android.h/.cpp` | `VTCompressionSession`, a plain C API — doesn't even need to be `.mm`. |

The glob for these files is already in place in SConstruct (there since M1,
unused):
```python
elif env["platform"] == "ios":
    sources += Glob("src/capture/ios/*.mm")
    sources += Glob("src/encode/*ios*.cpp")
```

**The one thing missing**: on iOS the output should be a `StaticLibrary`,
not a `SharedLibrary` (the pattern used by godot-cpp's own test project, see
`mobile/thirdparty/godot-cpp/test/SConstruct`):
```python
elif env["platform"] == "ios":
    if env["ios_simulator"]:
        library = env.StaticLibrary("../bin/libarcapture.{}.{}.simulator.a".format(env["platform"], env["target"]), source=sources)
    else:
        library = env.StaticLibrary("../bin/libarcapture.{}.{}.a".format(env["platform"], env["target"]), source=sources)
else:
    library = env.SharedLibrary(...)  # existing code
```
Without adding this to `mobile/native/SConstruct`, the iOS build will
(likely) fail with a linker error.

---

## 4. The change needed in `ar_capture.h/.cpp`

Currently:
```cpp
#ifdef ANDROID_ENABLED
#include "capture/android/camera2_capture_session.h"
#include "encode/h264_encoder_android.h"
...
#endif
```
Next to it, following the same pattern:
```cpp
#elif defined(IOS_ENABLED)
#include "capture/ios/arkit_capture_session.h"
#include "encode/h264_encoder_ios.h"
...
#endif
```
The actual logic inside `start_capture()`/`stop_capture()`/
`on_preview_frame()` etc. can be copied almost verbatim — class names change
(`Camera2CaptureSession` → `ArkitCaptureSession`/`AvfCaptureSession`), the
flow stays the same: the encoder starts first (so its surface/pixel-buffer
pool is ready), then the capture session starts, `sensor_orientation_` is
queried (see §5).

`preview_width_`/`preview_height_`/`_update_preview_texture` are entirely
platform-independent (Image/ImageTexture, Godot's own classes) — no changes
needed there at all.

---

## 5. Open questions / things to research (first thing once on a Mac)

These have an Android counterpart but haven't been verified on iOS yet —
an "M0"-equivalent verification pass is needed:

1. **Requesting permission**: `OS.request_permission("android.permission.CAMERA")`
   was Android-specific. Does Godot's `OS` class have an iOS equivalent for
   camera permission (`OS.request_permission()` is probably somewhat
   cross-platform but iOS support hasn't been verified), or will
   `AVCaptureDevice.requestAccess(for: .video)` need to be called from the
   native side — needs research. `NSCameraUsageDescription` is required in
   `Info.plist` (set via the export preset).

2. **Rotation**: solved on Android via `ACAMERA_SENSOR_ORIENTATION` (see
   `query_back_camera_sensor_orientation` in `camera2_capture_session.h`).
   The iOS equivalent is `AVCaptureConnection.videoRotationAngle` (iOS 17+)
   or the older `videoOrientation` API — which one to use depends on the
   target minimum iOS version, needs research. No protocol-side changes are
   needed — `VIDEO_CONFIG`'s `rotation` field is already platform-independent
   (see `PROTOCOL.md` §3.3).

3. **Zero-copy frame flow**: on Android, `AMediaCodec_createInputSurface()`
   let the camera write directly into the encoder's Surface (no CPU copy).
   On iOS, `ARFrame.capturedImage`/`AVCaptureVideoDataOutput` hand you a
   `CVPixelBuffer`; it's possible to pass this directly to
   `VTCompressionSessionEncodeFrame()`, but true zero-copy requires the
   pixel buffer pool (`CVPixelBufferPool`) to match the format the encoder
   expects — a reasonable order is to start with a simple CPU-copy path
   first (similar to the pre-`AMediaCodec` Android approach), then optimize.

4. **Minimum iOS version**: on Android the API 26 requirement showed up
   during the build (`AMediaCodec_createInputSurface` needs API 26+). On
   iOS, ARKit already requires iOS 11+, and VideoToolbox is much older —
   probably not an issue, but will become clear during the build.

5. **In-editor loading**: in M1, a separate build was needed for Windows
   (the Godot editor blocked export if it couldn't load the extension on
   its own platform). Since the editor will run natively on macOS this
   time, `scons platform=macos target=template_debug` will be needed (in
   addition to Android/iOS, a separate third build) — the
   `macos.debug`/`macos.release` entries also need to be added to
   `mobile/arcapture.gdextension`.

---

## 6. Prep work already done in this session

- **`stream_client.cpp`**: the build condition was widened from
  `#ifdef ANDROID_ENABLED` to
  `#if defined(ANDROID_ENABLED) || defined(IOS_ENABLED)`.
  Since the POSIX sockets code is identical on Android and iOS, this was a
  safe, mechanical fix (didn't require a Mac, verified via the Android
  build) — StreamSink/StreamClient will now actually work on iOS too, with
  no extra code needed.
- This document (`docs/IOS_HANDOVER.md`).

---

## 7. Toolchain (to set up on the Mac)

- Xcode (latest stable) — from the App Store.
- Godot 4.6.x editor (macOS `.dmg`/`.zip`) — the export template package
  (`Godot_v4.6.1-stable_export_templates.tpz`, the **exact same file**
  already downloaded on Windows, platform-independent) already includes the
  iOS and macOS templates, no separate download needed; it unpacks the same
  way into `~/Library/Application Support/Godot/export_templates/` instead
  of `%APPDATA%`.
- SCons (`pip install scons` — same step as on Windows).
- The `godot-cpp` submodule is already in the repo (`git submodule update --init`).
- A free Apple ID is enough to install a debug build on a physical device
  (needs re-signing every 7 days); TestFlight/App Store requires a paid
  Apple Developer account ($99/year) — not needed for now.

**Build commands** (same pattern as Android/Windows):
```bash
# iOS device (arm64, not the simulator)
cd mobile/native
scons platform=ios arch=arm64 ios_simulator=no target=template_debug

# also need to build for the editor's own platform (macOS)
scons platform=macos target=template_debug
```

---

## 8. Suggested milestone order (parallel to Android's M1-M6)

| # | Stage | Verification |
|---|---|---|
| iM0 | Toolchain + `platform=macos` build + editor opens the empty project | Equivalent to Android M1's editor-loading step |
| iM1 | `avf_capture_session.mm` (plain AVFoundation first, not ARKit) + `imu_sampler_ios.mm` | Frame/Hz counters, no crashes |
| iM2 | `h264_encoder_ios.cpp` (VideoToolbox), recording to a local file | Verify with `ffprobe` (same method as Android M3) |
| iM3 | End-to-end same-LAN streaming test | **`server/` should work completely unchanged** — the protocol was already tested platform-independently |
| iM4 | Rotation (§5.2) | Visual verification via screenshot (same as we did on Android) |
| iM5 | `arkit_capture_session.mm` (ARKit itself) | pose/point-cloud/intrinsics on a real device — **ARKit doesn't work in the Simulator, a real iPhone/iPad is required** |

iM0-iM4 can actually be done **with just a Mac, no physical iPhone needed**
(the Simulator doesn't support the AVFoundation camera, but build/link/
protocol verification is fully covered by the Mac alone — see the SimCam
note from our conversation, which could later be used for frame-generation
testing in CI). iM5 (ARKit actually working) requires a real device.
