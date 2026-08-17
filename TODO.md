# TODO

Actionable items not yet done. For the milestone history and what's already
shipped, see [`docs/ROADMAP.md`](docs/ROADMAP.md).

## Before/at the actual GitHub push

- [ ] Create the GitHub repo and add it as a remote (`git remote add origin ...`), then push. Not done yet -- needs the target repo URL/visibility (public/private) decided.
- [ ] Double-check no local-only paths, IPs, or credentials are hardcoded anywhere (`192.168.1.100:9999` in `mobile/scenes/Main.tscn` is a placeholder default, not a secret -- fine to ship).
- [ ] Add a repo description/topics on GitHub once created (Godot, GDExtension, ARCore, Android, H.264, streaming).

## M6 follow-ups -- ARCore capture path (core path done, see `docs/ROADMAP.md`)

- [ ] Real hand-held recording (phone actually moved by a person, not just ADB automation) to confirm: (a) `points.jsonl` populates once there's camera parallax for ARCore's sparse point cloud to track (came back empty in the stationary-phone device test -- expected given no motion, but not distinguished from a bug), (b) frame orientation is visually correct (the automated test's camera was pointed at a dim, texture-poor surface, inconclusive).
- [ ] `mobile/addons/jni_bootstrap/bin/release/*.aar` -- built once (`./gradlew assembleRelease` in `mobile/android/plugins/jni_bootstrap/`) but never exercised through a `--export-release` build; only the debug path has been verified end-to-end on-device.
- [ ] `ArCoreCaptureSession` doesn't yet read the encoder's target fps into `ArCameraConfigFilter_setTargetFps` -- currently accepts whatever fps ARCore's matched camera config reports, not confirmed to equal the encoder's requested fps.

## M7 -- GitHub-quality polish + CI

- [ ] `.github/workflows/server-python.yml` -- Python 3.11/3.12 matrix, `pytest`, maybe `ruff`/`mypy`.
- [ ] `.github/workflows/native-build.yml` -- `scons platform=android arch=arm64 target=template_debug` build check + host-native `protocol_test` build+run (no device/emulator needed).
- [ ] Tag `v0.1.0` once CI is green on a clean clone.

## Protocol / server gaps

- [ ] `ingest.py` doesn't implement the `HELLO`/`HELLO_ACK` handshake yet -- it only decodes `VIDEO_CONFIG`/`VIDEO_CHUNK` (matches what the mobile client currently sends, but `docs/PROTOCOL.md` §3.1/§4 describe the full handshake).
- [ ] External plugins (loaded via `--plugins-dir`) don't automatically receive an `EventBus` -- see the note in `server/README.md#writing-a-plugin`. Would need a small `cli.py` change (or a different injection mechanism) if this is wanted.
- [ ] `CLOCK_SYNC_REQUEST`/`RESPONSE` are implemented in `protocol.py`/`protocol.cpp` but nothing calls them yet on either side.

## Known gaps (documented, not urgent)

- [ ] iOS support -- blocked on acquiring a Mac. Full handover doc ready at `docs/IOS_HANDOVER.md`.
- [ ] v2 transport (UDP + lightweight encryption) -- not designed yet, see `docs/ROADMAP.md`.
