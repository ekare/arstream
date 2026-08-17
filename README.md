# arstream

A mobile app (Godot, Android first / iOS later) that captures camera video and IMU data via ARCore, compresses it in real time with a hardware H.264 encoder, and streams it over a single TCP connection using a custom, versioned binary protocol to a "destination" server. The repo also includes a Python reference server that speaks this protocol — both a working example and living documentation of the wire format.

The native side (camera capture, encoding, networking) is written in **GDExtension (C/C++)** — no business logic in Kotlin/Swift; the same C++ codebase is compiled per platform. The protocol, `StreamSink`/`StreamClient` (memory-first/disk-overflow resilient sending), and the file sink are fully platform-independent and will be used unchanged on iOS. There are two narrow exceptions, both pure bridges rather than logic: (1) On Android, since Godot's GDExtension has no official way to obtain the JNIEnv/Activity context (see [godot-proposals #6734](https://github.com/godotengine/godot-proposals/issues/6734)), a zero-business-logic Kotlin "bootstrap shim" hands that context to native code once (see `docs/ARCHITECTURE.md`'s "JNI bootstrap exception" for the three non-obvious pitfalls this surfaced). (2) On iOS, since ARKit/AVFoundation/CoreMotion have no C API, a thin Objective-C++ bridge will be needed — this doesn't even carry the JNI-style context-passing burden Android has, since Obj-C++ can call directly (not yet built, requires a Mac — see [`docs/IOS_HANDOVER.md`](docs/IOS_HANDOVER.md)).

## Architecture

Short version: phone → `ArCapture` (GDExtension) → ARCore or Camera2 fallback → `AMediaCodec` H.264 → custom binary protocol → TCP → `server/` (Python, decodes with PyAV).

Detailed diagram and design rationale: [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

## Wire protocol

If an external "adapter" is going to consume this stream, the only document you need is [`docs/PROTOCOL.md`](docs/PROTOCOL.md). Version: v1.0.

## Device compatibility

ARCore doesn't run on every Android device (requires Android 7.0+ AND Google's certified-device list). The app gracefully falls back to raw `Camera2` + `SensorManager` capture when ARCore is unavailable/unsupported. Tested devices and findings: [`docs/DEVICE_COMPATIBILITY.md`](docs/DEVICE_COMPATIBILITY.md).

## Status

M1–M6 complete. Camera2 → `AMediaCodec` H.264 (zero-copy) → **real TCP streaming**, tested end-to-end from an A30s to a laptop on the same WiFi LAN; `StreamSink`'s memory-first/disk-overflow buffer was verified against a real ~25s network outage with zero loss. The **ARCore path is now live and device-verified**: `ArCoreApk_checkAvailability()` reports `SUPPORTED_INSTALLED` on the A30s, and a forced-ArCore recording produced 900 frames @ 29.4fps real-time through ARCore's GL camera texture → `AMediaCodec`, with pose/intrinsics data flowing correctly (point-cloud and frame-orientation still need a hand-held, non-automated test — see `TODO.md`). `CaptureController` picks ARCore vs. Camera2 at runtime; every sensor the device reports (not just accel/gyro) is sent unconditionally regardless of which backend is active. Preview toggles independently of recording; the wire protocol's C++/Python implementations are tested against `fixtures/*.bin`. `server/` runs a **plugin-based web dashboard** — live session monitoring (WebSocket), downloading past recordings as `.zip` (see [Recorded files](server/README.md#recorded-files) for exactly what's in it), loading external plugins; 54 pytest tests pass (see [`server/README.md`](server/README.md)). Still missing: HELLO/HELLO_ACK handshake, GitHub-quality CI (M7). Progress: [`docs/ROADMAP.md`](docs/ROADMAP.md).

## Quick start

Required tools (versions verified in `mobile/native/SConstruct` and this repo):

- Godot 4.6.x editor (with export templates)
- Android NDK **29.0.14206865**, under `$ANDROID_HOME/ndk/29.0.14206865` (or the `ANDROID_NDK_ROOT` environment variable)
- Android SDK (build-tools + platform-tools), set in Godot Editor Settings under `export/android/android_sdk_path`
- JDK 17+ (JDK 21 verified) — needed for both `apksigner` and Gradle, set in Editor Settings under `export/android/java_sdk_path` and as `JAVA_HOME` in your shell for the Gradle steps below
- Debug signing key — a PKCS12/JKS file at the `keystore/debug` path in Editor Settings or `mobile/export_presets.cfg` (can be generated with `openssl req`+`openssl pkcs12 -export`, `keytool` is not required)
- SCons (`pip install scons`)

Building requires **Gradle** now (M6's JNI bootstrap Android plugin needs it — see `docs/ARCHITECTURE.md`'s "JNI bootstrap exception"), which means one manual, editor-only step first:

```bash
# 1. One-time, GUI-only (Godot doesn't expose this headlessly): open mobile/ in the
#    Godot editor once, then Project -> Install Android Build Template...
#    This populates mobile/android/build/ (gitignored, regenerable) with a version
#    marker the export pipeline checks for -- reconstructing it by hand (e.g.
#    unzipping the export templates' android_source.zip yourself) does NOT work,
#    the editor's install action does something beyond a plain unzip.

# 2. Vendor the ARCore C API (gitignored -- see the script's own header comment
#    for why it isn't committed):
pwsh mobile/thirdparty/fetch_arcore_sdk.ps1

# 3. Build the JNI bootstrap plugin's own Gradle module and drop the .aar where
#    Godot's export pipeline expects it (mobile/addons/jni_bootstrap/bin/debug/):
cd mobile/android/plugins/jni_bootstrap
JAVA_HOME=/path/to/jdk-17-or-21 ./gradlew assembleDebug
cp build/outputs/aar/jni_bootstrap-debug.aar ../../../addons/jni_bootstrap/bin/debug/
cd ../../..

# 4. Build the native (GDExtension) library -- Android
cd native
ANDROID_HOME=/path/to/Android/Sdk scons platform=android arch=arm64 target=template_debug \
    ndk_version=29.0.14206865 android_api_level=26
cd ..

# the editor's own platform must also be built (otherwise Godot can't load the
# GDExtension when opening the project, which blocks export):
cd native && scons platform=windows target=template_debug && cd ..   # or linux/macos, depending on your host

# 5. headless export (without ever opening the Godot editor, once step 1 is done)
JAVA_HOME=/path/to/jdk-17-or-21 godot --headless --path mobile --export-debug "Android" ../build/arstream-debug.apk

# 6. install on device
adb install -r build/arstream-debug.apk
```

Steps 2–4 only need repeating when their respective sources change (ARCore SDK version, the Kotlin plugin, or the C++). To force a specific capture backend for testing rather than letting `CaptureController` auto-decide: `adb shell setprop debug.arstream.capture_backend arcore` (or `camera2`) before launching the app.

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

---

🤖 Built with [Claude Code](https://claude.com/claude-code) — from architecture decisions down to on-device debugging.
