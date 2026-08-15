# Architecture

## Overview

```
┌─────────────────────────── mobile/ (Godot project) ───────────────────────────┐
│                                                                                  │
│   GDScript (UI/orchestration)              native/ (GDExtension, C++)           │
│   ┌──────────────────┐                    ┌─────────────────────────────────┐  │
│   │ scenes/Main.tscn  │  method/signal     │ ArCapture (Godot class)          │  │
│   │ CaptureService.gd │◄──────────────────►│  ├─ CaptureController            │  │
│   │ (Idle/Connecting/ │  (low frequency)   │  │   ├─ ArCoreCaptureSession     │  │
│   │  Streaming/Error) │                    │  │   │   (arcore_c_api.h)       │  │
│   └──────────────────┘                    │  │   └─ Camera2CaptureSession    │  │
│                                             │  │       (NDK libcamera2ndk)    │  │
│                                             │  ├─ ImuSampler (NDK ASensor*)    │  │
│                                             │  ├─ H264Encoder (NDK AMediaCodec)│  │
│                                             │  └─ StreamClient+ProtocolWriter │  │
│                                             │      (BSD sockets, TCP)         │  │
│                                             └──────────────┬──────────────────┘  │
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
| ARCore vs. Camera2 fallback decided at runtime inside `CaptureController` | ARCore may not be supported on older/low-end devices (see `docs/DEVICE_COMPATIBILITY.md`) — the app falls back to raw camera+IMU capture instead of crashing. |
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

**The only thing that needs it:** `mobile/android/plugins/jni_bootstrap/` — a single Kotlin file (`JniBootstrapPlugin.kt`) that registers with Godot's Android Plugin system (`@UsedByGodot`) and, at a lifecycle point like `onMainCreate`/`onMainActivityResult`, hands the `JNIEnv*` and `Activity` reference to the native side (an `extern "C"` function) exactly once. From then on, `ArCoreCaptureSession` holds onto those pointers and makes all `arcore_c_api.h` calls (`ArSession_create`, `ArSession_update`, etc.) directly from C++. This file contains **zero ARCore/business logic** — only pointer handoff.

Detailed wire format: [`PROTOCOL.md`](PROTOCOL.md). Milestones and deferred decisions: [`ROADMAP.md`](ROADMAP.md).
