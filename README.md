# arstream

A mobile app (Godot, Android first / iOS later) that captures camera video and IMU data via ARCore, compresses it in real time with a hardware H.264 encoder, and streams it over a single TCP connection using a custom, versioned binary protocol to a "destination" server. The repo also includes a Python reference server that speaks this protocol — both a working example and living documentation of the wire format.

The native side (camera capture, encoding, networking) is written in **GDExtension (C/C++)** — no business logic in Kotlin/Swift; the same C++ codebase is compiled per platform. The protocol, `StreamSink`/`StreamClient` (memory-first/disk-overflow resilient sending), and the file sink are fully platform-independent and will be used unchanged on iOS. There are two narrow exceptions, both pure bridges rather than logic: (1) On Android, since Godot's GDExtension has no official way to obtain the JNIEnv/Activity context (see [godot-proposals #6734](https://github.com/godotengine/godot-proposals/issues/6734)), a ~20-line, zero-business-logic Kotlin "bootstrap shim" is used to start ARCore. (2) On iOS, since ARKit/AVFoundation/CoreMotion have no C API, a thin Objective-C++ bridge will be needed — this doesn't even carry the JNI-style context-passing burden Android has, since Obj-C++ can call directly (not yet built, requires a Mac — see [`docs/IOS_HANDOVER.md`](docs/IOS_HANDOVER.md)).

## Architecture

Short version: phone → `ArCapture` (GDExtension) → ARCore or Camera2 fallback → `AMediaCodec` H.264 → custom binary protocol → TCP → `server/` (Python, decodes with PyAV).

Detailed diagram and design rationale: [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

## Wire protocol

If an external "adapter" is going to consume this stream, the only document you need is [`docs/PROTOCOL.md`](docs/PROTOCOL.md). Version: v1.0.

## Device compatibility

ARCore doesn't run on every Android device (requires Android 7.0+ AND Google's certified-device list). The app gracefully falls back to raw `Camera2` + `SensorManager` capture when ARCore is unavailable/unsupported. Tested devices and findings: [`docs/DEVICE_COMPATIBILITY.md`](docs/DEVICE_COMPATIBILITY.md).

## Status

M1–M5 complete: Camera2 → `AMediaCodec` H.264 (zero-copy) → **real TCP streaming**, tested end-to-end from an A30s to a laptop on the same WiFi LAN. `StreamSink`'s memory-first/disk-overflow buffer was verified against a real network outage (~25s): memory filled and overflowed to disk, then drained fully in order once the connection returned, zero loss. Preview can be toggled independently of recording; the wire protocol's C++/Python implementations are tested against `fixtures/*.bin`. `server/` now runs a **plugin-based web dashboard** instead of raw logging — live session monitoring (WebSocket), downloading past recordings as `.zip`, loading external plugins; the full path (TCP ingest → plugin dispatch → EventBus → WebSocket → dashboard) and the `.zip` download were verified with a synthetic client, 48 pytest tests pass (see [`server/README.md`](server/README.md)). Still missing: HELLO/HELLO_ACK handshake, the ARCore path (M6). Progress: [`docs/ROADMAP.md`](docs/ROADMAP.md).

## Quick start

Required tools (versions verified in `mobile/native/SConstruct` and this repo):

- Godot 4.6.x editor (with export templates)
- Android NDK **29.0.14206865**, under `$ANDROID_HOME/ndk/29.0.14206865` (or the `ANDROID_NDK_ROOT` environment variable)
- Android SDK (build-tools + platform-tools), set in Godot Editor Settings under `export/android/android_sdk_path`
- JDK 21+ (for apksigner), set in Editor Settings under `export/android/java_sdk_path`
- Debug signing key — a PKCS12/JKS file at the `keystore/debug` path in Editor Settings or `mobile/export_presets.cfg` (can be generated with `openssl req`+`openssl pkcs12 -export`, `keytool` is not required)
- SCons (`pip install scons`)

```bash
# build the native (GDExtension) library -- Android
cd mobile/native
ANDROID_NDK_ROOT=/path/to/ndk/29.0.14206865 scons platform=android arch=arm64 target=template_debug

# the editor's own platform must also be built (otherwise Godot can't load the
# GDExtension when opening the project, which blocks export):
scons platform=windows target=template_debug   # or linux/macos, depending on your host

# headless export (without ever opening the Godot editor)
godot --headless --path mobile --export-debug "Android" ../build/arstream-debug.apk

# install on device
adb install -r build/arstream-debug.apk
```

```bash
# reference server -- TCP ingest server speaking the protocol + plugin-based web dashboard
cd server && pip install -e . && arstream-server --host 0.0.0.0 --port 9999 --web-port 8080 --out ./captures
# dashboard: http://localhost:8080  (live sessions, download past recordings as .zip, auto API docs at /docs)
```

Installation, dependency rationale, and a guide to writing your own plugin: [`server/README.md`](server/README.md).

The iOS side doesn't exist yet (requires a Mac) — see [`docs/IOS_HANDOVER.md`](docs/IOS_HANDOVER.md) for where to start.

## Testing

```bash
# Python: protocol implementation, against fixtures/*.bin
cd server && pip install -e ".[dev]" && pytest tests/ -v

# C++: against the same fixtures, no godot-cpp/device required
cd mobile/native/tests && scons && ./protocol_test
```

Both implementations are tested against `fixtures/*.bin` (see `fixtures/generate_fixtures.py` — generated directly from the spec, independent of both implementations); neither is the other's reference.

## What's left

See [`TODO.md`](TODO.md) for actionable next steps, and [`docs/ROADMAP.md`](docs/ROADMAP.md) for the full milestone history.

## License

[MIT](LICENSE).
