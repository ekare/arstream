# TODO

Actionable items not yet done. For the milestone history and what's already
shipped, see [`docs/ROADMAP.md`](docs/ROADMAP.md).

## Before/at the actual GitHub push

- [ ] Create the GitHub repo and add it as a remote (`git remote add origin ...`), then push. Not done yet -- needs the target repo URL/visibility (public/private) decided.
- [ ] Double-check no local-only paths, IPs, or credentials are hardcoded anywhere (`192.168.1.100:9999` in `mobile/scenes/Main.tscn` is a placeholder default, not a secret -- fine to ship).
- [ ] Add a repo description/topics on GitHub once created (Godot, GDExtension, ARCore, Android, H.264, streaming).

## M6 -- ARCore capture path

- [ ] `capture/android/arcore_capture_session.cpp/.h` -- `ArSession`/`ArFrame` via `arcore_c_api.h`.
- [ ] The Kotlin JNI-bootstrap shim (`mobile/android/plugins/jni_bootstrap/JniBootstrapPlugin.kt`) -- approved in design, not yet built. Needed only for `JNIEnv*`/`Activity` handoff to `ArSession_create`; zero ARCore logic in the Kotlin file itself (see `docs/ARCHITECTURE.md` "JNI bootstrap exception").
- [ ] `POSE_SAMPLE`/`POINT_CLOUD`/`CAMERA_INTRINSICS` messages wired end-to-end from ARCore into the protocol (encode/decode already exist on both sides, only the producer is missing).
- [ ] Real-device verification once ARCore-supported hardware is confirmed (see `docs/DEVICE_COMPATIBILITY.md` -- the A30s looks promising but `ArCoreApk_checkAvailability()` hasn't actually been called yet).

## M7 -- GitHub-quality polish + CI

- [ ] `.github/workflows/server-python.yml` -- Python 3.11/3.12 matrix, `pytest`, maybe `ruff`/`mypy`.
- [ ] `.github/workflows/native-build.yml` -- `scons platform=android arch=arm64 target=template_debug` build check + host-native `protocol_test` build+run (no device/emulator needed).
- [ ] Tag `v0.1.0` once CI is green on a clean clone.

## Protocol / server gaps

- [ ] `ingest.py` doesn't implement the `HELLO`/`HELLO_ACK` handshake yet -- it only decodes `VIDEO_CONFIG`/`VIDEO_CHUNK` (matches what the mobile client currently sends, but `docs/PROTOCOL.md` §3.1/§4 describe the full handshake).
- [ ] External plugins (loaded via `--plugins-dir`) don't automatically receive an `EventBus` -- see the note in `server/README.md#writing-a-plugin`. Would need a small `cli.py` change (or a different injection mechanism) if this is wanted.
- [ ] `CLOCK_SYNC_REQUEST`/`RESPONSE` are implemented in `protocol.py`/`protocol.cpp` but nothing calls them yet on either side.

## Known gaps (documented, not urgent)

- [ ] Android `minSdk` mismatch: the encoder needs API 26+, but the current non-Gradle export path can't declare that in the manifest (see `docs/DEVICE_COMPATIBILITY.md`). Not an issue for the A30s (API 30); needs `gradle_build/use_gradle_build=true` or a ByteBuffer fallback encoder before shipping to older devices.
- [ ] iOS support -- blocked on acquiring a Mac. Full handover doc ready at `docs/IOS_HANDOVER.md`.
- [ ] v2 transport (UDP + lightweight encryption) -- not designed yet, see `docs/ROADMAP.md`.
