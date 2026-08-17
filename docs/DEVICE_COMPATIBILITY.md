# Device Compatibility Findings

This document tracks which devices the ARCore/Camera2/IMU capture paths have been tested on and what was found. `CaptureController`'s ARCore-vs-fallback decision is based on these findings.

---

## Samsung Galaxy A30s (SM-A307FN)

**Date:** 2026-08-15 · **Method:** passive diagnostics via ADB (no app code written yet — the code-free part of the M0 spike)

| Field | Value |
|---|---|
| Model | SM-A307FN |
| Android version | 11 (API 30) |
| CPU ABI | arm64-v8a (also compatible with armeabi-v7a, armeabi) |
| Chipset | Exynos 7904 (`universal7904`) |

### ARCore status — **`SUPPORTED_INSTALLED`, confirmed at runtime (M6)**

Project planning started from the assumption, based on community sources, that "the A30s is probably not on ARCore's certified-device list" (the plain A30 was reported as listed, the A30s was not). Passive inspection (M0, before any app code existed) already pointed the other way — `com.google.ar.core` installed (`1.54.260890083`), Play Services current, Play Store present — and the **actual runtime call settles it**:

```
ArCoreApk_checkAvailabilityAsync() → SUPPORTED_INSTALLED
```

Called from `arcore_availability.cpp`, triggered once at app startup via the JNI bootstrap plugin (see `ARCHITECTURE.md`'s "JNI bootstrap exception"). The community assumption was wrong for this specific unit — the A30s runs the full ARCore path, not just the Camera2 fallback.

**End-to-end capture verified with the ARCore backend forced** (`adb shell setprop debug.arstream.capture_backend arcore`): 900 frames @ 29.4fps real-time H.264 through `ArCoreCaptureSession`'s GL blit pipeline, `poses.jsonl` showing a realistic `PAUSED`→`TRACKING` transition, `intrinsics.json` reporting sane values after `ArSession_setCameraConfig` was added to match the encoder's aspect ratio (ARCore's own default camera config was 640×480/4:3 against a 1280×720/16:9 encoder target — stretched the image until fixed, see `ROADMAP.md`'s M6 section). **Not yet verified by a human physically holding/moving the phone** — the test above was driven entirely over ADB with the phone stationary on a desk, which is consistent with (but doesn't prove) two open items: `points.jsonl` came back empty (ARCore's sparse feature point cloud needs camera parallax to populate, which a stationary phone can't provide), and frame orientation wasn't conclusively confirmed visually (the camera was pointed at a dim, texture-poor surface). A real hand-held recording is the next verification step for these two specifically.

### Hardware H.264 (AVC) encoder — **verified**

In `/vendor/etc/media_codecs.xml`:
```
<MediaCodec name="OMX.Exynos.AVC.Encoder" type="video/avc" >
<MediaCodec name="OMX.Exynos.AVC.Encoder.secure" type="video/avc-wfd" >
```
Actually accessing this encoder via `AMediaCodec` (NDK) will be verified in M3, but its presence is confirmed.

### Known inconsistency — declared minSdk vs. actual requirement — **resolved in M6**

`H264EncoderAndroid` uses `AMediaCodec_createInputSurface`/`AMediaCodec_signalEndOfInputStream` for the zero-copy (Camera→Surface→Encoder) path — these require **API 26+** (the build uses `android_api_level=26`). The original (non-Gradle) export path couldn't declare this in the manifest at all; M6's move to `gradle_build/use_gradle_build=true` (required anyway, for the JNI bootstrap Android plugin) came with `gradle_build/min_sdk="26"`, closing this gap as a side effect. No longer an open item.

### Sensors — **verified end-to-end (SensorSampler, all sensor types, both save and stream modes)**

`dumpsys sensorservice` output (STM LSM6DSL package) for the core IMU pair:

| Sensor | Driver | Mode | Rate range |
|---|---|---|---|
| Accelerometer | LSM6DSL | continuous, non-wakeUp, no batching | 5.00–100.00 Hz |
| Gyroscope | LSM6DSL | continuous, non-wakeUp, no batching | 5.00–100.00 Hz |
| Gyroscope (uncalibrated) | LSM6DSL | same | same |

**Note:** A max of 100Hz is lower than the 200–400Hz gyro on some higher-end devices, but usable for VIO-style purposes. `SENSOR_DELAY_GAME` corresponds to ~100Hz on this device. `SensorSampler` reads the actual rate per-sensor via `ASensor_getMinDelay()` rather than assuming a fixed target — consistent with the no-magic-numbers rule.

**Full sensor enumeration (real device test, `SensorSampler`, both `save`-mode `.imu.jsonl` and `stream`-mode against `server/`):** this device reports **16 distinct sensor types**, not just accelerometer/gyroscope: `1` accelerometer, `2` magnetic field, `3` orientation (legacy), `4` gyroscope, `5` light, `8` proximity, `9` gravity, `10` linear acceleration, `11` rotation vector, `14` magnetic field (uncalibrated), `15` game rotation vector, `16` gyroscope (uncalibrated), `24` (vendor/likely pose-related, only fires a few times), `51`/`65`/`80` (Exynos/Samsung vendor-private sensor IDs, outside the standard `ASENSOR_TYPE_*` range but still captured generically — no allowlist needed). No dedicated ambient-temperature sensor on this particular device (hardware fact, not a code gap). Scalar sensors (light, proximity) correctly report only `x`, with `y=z=0` as documented in `docs/PROTOCOL.md` §3.4. At rest: gyroscope ≈0 rad/s on all axes (confirms no spurious motion), accelerometer magnitude ≈9.5 m/s² (device held at a slight tilt, not flat — consistent with 9.8 m/s² at 0° tilt).

**Clock domain — verified, `REALTIME`:** `adb shell dumpsys media.camera` reports `android.sensor.info.timestampSource: REALTIME` for the back camera. Combined with the API 26+ CDD guarantee that `ASensorEvent.timestamp` is `SystemClock.elapsedRealtimeNanos()`-based, this confirms `VIDEO_CHUNK`'s `capture_timestamp_ns` and `IMU_BATCH`'s per-sample `timestamp_ns` are on the **same clock domain** on this device — `docs/PROTOCOL.md` §0's "same device clock domain" design assumption holds here. (On a device reporting `UNKNOWN` instead, this would need empirical calibration — e.g. correlating a deliberate sharp shake's peak in both streams — rather than being assumed.)

---

## Template — when adding a new device

```markdown
## <Brand Model> (<codename>)

**Date:** · **Method:**

| Field | Value |
|---|---|
| Android version | |
| CPU ABI | |
| Chipset | |

### ARCore status
### Hardware H.264 encoder
### IMU
```
