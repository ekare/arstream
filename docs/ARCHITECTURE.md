# Architecture

## Overview

```
┌─────────────────────────── mobile/ (Godot project) ───────────────────────────┐
│                                                                                  │
│   GDScript (UI/orchestration)              native/ (GDExtension, C++)           │
│   ┌──────────────────┐                    ┌─────────────────────────────────┐  │
│   │ scripts/Main.gd   │  method/signal     │ ArCapture (Godot class)          │  │
│   │ (permission flow, │◄──────────────────►│  ├─ CaptureController            │  │
│   │  start/stop UI,   │  (low frequency)   │  │   ├─ ArCoreCaptureSession     │  │
│   │  preview toggle)  │                    │  │   │   (arcore_c_api.h +      │  │
│   └──────────────────┘                    │  │   │    GlBlitRenderer/EGL)    │  │
│                                             │  │   └─ Camera2CaptureSession    │  │
│                                             │  │       (NDK libcamera2ndk)    │  │
│                                             │  ├─ SensorSampler (NDK ASensor*)│  │
│                                             │  ├─ H264Encoder (NDK AMediaCodec)│  │
│                                             │  └─ StreamSink/FileSink         │  │
│                                             │      (StreamClient: BSD sockets)│  │
│                                             └──────────────┬──────────────────┘  │
│   android/plugins/jni_bootstrap/ (Kotlin) ─────────────────┘ JNIEnv*/Activity   │
│   (separate Gradle module — see "JNI bootstrap exception" below)               │
└────────────────────────────────────────────────────────────┼─────────────────────┘
                                                               │ TCP, docs/PROTOCOL.md
                                                               ▼
                                      ┌─────────────────────────────────────────┐
                                      │ server/ (Python reference backend)        │
                                      │  ingest.py  (decode protocol, dispatch)   │
                                      │  protocol.py (same format, separate impl) │
                                      │              │                            │
                                      │              ▼                            │
                                      │  plugins/ (RecorderPlugin, StatsPlugin,   │
                                      │            + external ones from            │
                                      │            --plugins-dir)                 │
                                      │              │                            │
                                      │              ▼                            │
                                      │  EventBus ──► web/app.py (FastAPI)         │
                                      │                 /api/sessions, /ws/live,   │
                                      │                 dashboard.html             │
                                      └─────────────────────────────────────────┘
                                                               │
                                                               ▼
                                        (external adapter — developed elsewhere,
                                         conforms to docs/PROTOCOL.md)
```

## Key decisions and rationale

| Decision | Rationale |
|---|---|
| Godot + GDExtension (C/C++), NO business logic in Kotlin/Swift | ARCore's official C API (`arcore_c_api.h`) + Android NDK's Camera2/MediaCodec/Sensor C APIs make this possible. Camera2 fallback, `AMediaCodec` encode, `ASensorManager` IMU — all pure C/C++, no JNIEnv needed, verified. There are narrow exceptions for ARCore and iOS below (see "JNI bootstrap exception"). |
| Hot data path (capture→encode→network) is entirely native, never touches GDScript | At 30fps, GDScript↔native marshalling gains nothing; staying native is the natural choice both for performance and for ARCore/NDK API access. GDScript is UI/lifecycle only. |
| ARCore vs. Camera2 fallback decided at runtime inside `CaptureController` | ARCore may not be supported on older/low-end devices (see `docs/DEVICE_COMPATIBILITY.md`) — the app falls back to raw camera+IMU capture instead of crashing. `decide_backend()` reads the *cached* result of the one `ArCoreApk_checkAvailability()` call made at startup (not re-queried per capture, see the JNI section below) plus an optional `debug.arstream.capture_backend` system property to force a specific backend for testing. |
| Sensors (`SensorSampler`) run unconditionally, independent of the camera backend | ARCore's own pose is a *fused* estimate, not a substitute for raw telemetry — the project always sends whatever `ASensorManager` reports (not just accel/gyro) alongside whichever camera backend is active, so a downstream consumer never has to special-case "what sensors did this session have." |
| Single TCP connection, custom binary framing (not WebRTC/UDP) | Godot's built-in WebRTC support is data-channel only (no RTP/media track) — no real win for video. Simple, debuggable, consistent with the "keep it simple" philosophy. In v2, the same message format could be offered over a different transport (WebRTC data-channel, UDP/QUIC) — see `docs/ROADMAP.md`. |
| Annex-B NAL framing (not AVCC) | The natural output of both `AMediaCodec` and `VTCompressionSession`, and the natural input to PyAV/ffmpeg — no conversion step. |
| Python reference server, NOT a replacement for the external adapter | `server/` is the protocol's "living documentation" and our own verification receiver — not a production ingestion service. |
| `server/` is plugin-based (not raw logging) | Anything that processes/observes the incoming stream (recording, stats, future analysis) uses the same `StreamPlugin` interface; `ingest.py` knows nothing beyond decoding the protocol. A plugin's failure never stops the others or the ingest itself (`PluginManager` isolates it). |
| Dashboard runs FastAPI/uvicorn in the same asyncio loop | Runs in the same process/loop as the ingest server (`asyncio.gather` in `cli.py`) — no separate process/IPC. `EventBus` (an asyncio.Queue-based pub-sub) bridges plugins to the dashboard's `/ws/live`. |

## JNI bootstrap exception (Android)

Godot 4.x's GDExtension system provides **no official way** to access the `JNIEnv*`/`Activity` context from native code on Android — Godot 3.x's GDNative had this (`godot_android_get_env()`), it wasn't carried over to 4.x, and it's still an open engine gap: [godot-proposals #6734](https://github.com/godotengine/godot-proposals/issues/6734). The only workaround the community has found (and what the one existing prior-art project, [`paddy-exe/arcore-gdextension`](https://github.com/paddy-exe/arcore-gdextension), does too): a tiny Android Plugin (Kotlin) that, once instantiated by Godot, hands the `JNIEnv*`/`Activity` pointers it receives to a native method and does nothing else.

**Explicitly out of scope for this** (no JNIEnv needed, pure C/C++, verified):
- Camera2 fallback capture (`libcamera2ndk`)
- `AMediaCodec` H.264 encoding
- `ASensorManager`/`ASensorEventQueue` IMU sampling (`ASensorManager_getInstanceForPackage` wants a package name, not a Context)
- Network/protocol layer (BSD sockets)
- Requesting camera permission (`OS.request_permission("CAMERA")` — from GDScript, Godot's own engine API, doesn't need our JNI bridge)

**The only thing that needs it:** `mobile/android/plugins/jni_bootstrap/` — a standalone Gradle module (its own `build.gradle.kts`, `settings.gradle.kts`, wrapper — deliberately *not* part of Godot's own managed `mobile/android/build/`), built independently and its output `.aar` handed to Godot via `mobile/addons/jni_bootstrap/` (`plugin.cfg` + `export_plugin.gd`, an `EditorExportPlugin`). `JniBootstrapPlugin.kt` registers with Godot's Android Plugin v2 system and, from `onGodotMainLoopStarted()`, hands the `JNIEnv*`/`Activity` to native code (a plain JNI `extern "C"` function, `android_context.cpp`) exactly once. From then on, `android_context::get_env()`/`get_activity()` are available to any native code, and `ArCoreCaptureSession` makes all `arcore_c_api.h` calls directly from C++. This file contains **zero ARCore/business logic** — only pointer handoff — but getting even that working surfaced three non-obvious pitfalls, each now a comment at its fix site (also summarized in `ROADMAP.md`'s M6 section):

1. **`onGodotMainLoopStarted()` is not the UI thread.** Despite reading like an Activity lifecycle callback, it fires on Godot's own engine thread. Anything that must run on Android's actual UI thread (ARCore's async availability check aborts otherwise — confirmed on-device, `SIGABRT` inside `libarcore_sdk_c.so`) has to be explicitly wrapped in `Activity.runOnUiThread{}`.
2. **The GDExtension `.so` has a build-variant-suffixed filename**, not a plain `libarcapture.so` (SCons' own naming convention, `libarcapture.<platform>.<target>.<arch>.so`) — `System.loadLibrary("arcapture")` can't find it by a hardcoded name. `JniBootstrapPlugin.kt` discovers the real filename at runtime by reading the APK's own zip entries before loading it, so it works regardless of debug/release/ABI.
3. **ARCore's C API isn't purely native under the hood** — it calls back into ARCore's own Java classes via JNI internally. Vendoring only the raw `.so`+header (`mobile/thirdparty/fetch_arcore_sdk.ps1`) is not enough; the real `com.google.ar:core` Gradle dependency has to be resolved into the final APK too, or those internal calls crash under `-Xcheck:jni`. Since a locally-built `.aar` (unlike one resolved from a Maven repo) has no `pom.xml` for Gradle to infer transitive dependencies from, this is declared instead via `EditorExportPlugin._get_android_dependencies()` in `export_plugin.gd`, which Godot's own Gradle build does see.

## ARCore GL render pipeline (Android)

Camera2 writes video frames to the encoder's `Surface` with zero CPU involvement (hardware Surface-to-Surface). ARCore has no equivalent path — it exposes the camera image as a `GL_TEXTURE_EXTERNAL_OES` GL texture (`ArSession_setCameraTextureName`), so getting it into the encoder requires an actual GPU draw call. `ArCoreCaptureSession` owns a dedicated render thread that drives `ArSession_update()`, then uses `GlBlitRenderer` (a minimal EGL/GLES2 helper, no external dependency) to blit that texture as a full-screen quad into the encoder's `Surface` (wrapped via `eglCreateWindowSurface`) — and, when preview is on, into an offscreen FBO read back with `glReadPixels` at preview resolution, converted to luma so `CaptureController`'s `PreviewFrameCallback` shape stays identical across both backends.

Two device-confirmed pitfalls worth knowing if this code is touched again:
- **EGL contexts are per-thread.** All GL/EGL setup (context creation, camera texture, window surface, `ArSession_resume()`) has to happen *on the render thread itself*, not on whichever thread called `start()` — doing it on the caller and only running the *update loop* on a separate thread produces `AR_ERROR_MISSING_GL_CONTEXT` on every `ArSession_update()` call. `start()` still returns success/failure synchronously to its caller via `std::promise`/`std::future`.
- **ARCore doesn't automatically match the encoder's aspect ratio.** Its own default camera config picked 640×480 (4:3) against a 1280×720 (16:9) encoder target on-device, stretching the blitted image. `select_matching_camera_config()` queries `ArSession_getSupportedCameraConfigsWithFilter` and applies (`ArSession_setCameraConfig`, before `ArSession_resume()`) whichever supported config's aspect ratio is closest to the encoder's.

## `OutputSink`: `save` vs `stream` mode

`ArCapture::start_capture()`'s `mode` config picks which `OutputSink` implementation receives every `write_*` call (`write_video_config`, `write_chunk`, `write_imu_batch`, `write_pose_sample`, `write_point_cloud`, `write_camera_intrinsics`) — `CaptureController`/`ArCoreCaptureSession`/`Camera2CaptureSession` never know or care which one is active.

- **`stream`** → `StreamSink`: encodes each call with `net/protocol.{h,cpp}` and sends it over TCP per `docs/PROTOCOL.md`, memory-first with disk overflow if the connection drops (see `ROADMAP.md`'s M5 entry). This is what `server/`'s `RecorderPlugin` receives and writes to `captures/<session_id>/` — see [Recorded files](../server/README.md#recorded-files) for that layout.
- **`save`** → `FileSink`: writes straight to local files, no server needed, for on-device debugging. Given `output_path` (e.g. `capture.h264`), it produces:

  | File | Contents |
  |---|---|
  | `<output_path>` | raw Annex-B H.264 (SPS/PPS + frames, concatenated in order) |
  | `<output_path>.imu.jsonl` | one line per sensor sample: `{sensor_type, timestamp_ns, x, y, z}` |
  | `<output_path>.poses.jsonl` | one line per `POSE_SAMPLE` (ARCore only): `{timestamp_ns, tracking_state, x, y, z, qx, qy, qz, qw}` |
  | `<output_path>.points.jsonl` | one line per `POINT_CLOUD` (ARCore only): `{timestamp_ns, points: [[x,y,z,confidence], ...]}` |
  | `<output_path>.intrinsics.json` | latest `CAMERA_INTRINSICS` snapshot (ARCore only): `{fx, fy, cx, cy, width, height}`, overwritten on change |

  Same JSON shapes as the server's `imu.jsonl`/`poses.jsonl`/`points.jsonl`/`intrinsics.json` (deliberately — `FileSink` and `RecorderPlugin` are independent implementations of the same idea in two languages), just without `frames.jsonl`'s byte-offset index. Pull with `adb shell run-as <package> cat files/<output_path>.poses.jsonl`, e.g. (files live under the app's private `files/` dir, `output_path` is relative — see `Main.gd`'s `OS.get_user_data_dir()` use).

Detailed wire format: [`PROTOCOL.md`](PROTOCOL.md). Milestones and deferred decisions: [`ROADMAP.md`](ROADMAP.md).
