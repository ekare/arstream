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

### ARCore status — **more optimistic than expected**

Project planning started from the assumption, based on community sources, that "the A30s is probably not on ARCore's certified-device list" (the plain A30 was reported as listed, the A30s was not). Direct device inspection **doesn't support this**:

- `com.google.ar.core` (Google Play Services for AR) **is installed**, version `1.54.260890083` (`minSdk=29, targetSdk=36`) — recently updated.
- Google Play Services (`com.google.android.gms`) is current: `26.30.32`.
- The Play Store (`com.android.vending`) is present.

**Interpretation:** The package being present and the Play Store actively pushing updates to the device is a strong signal that the device hasn't been entirely filtered out by Google's eligibility checks — but it's **not conclusive proof** (the ARCore package could also be installed manually/sideloaded). **The definitive answer only comes from `ArCoreApk_checkAvailability()`'s actual runtime result** — the first thing to verify once the GDExtension skeleton is up in M1.

**Conclusion:** M0's earlier assumption of "the ARCore path will probably not be testable on this device" has been relaxed — there's a good chance the ARCore path can be tested on this device too. `CaptureController` is still being built to support both paths (ARCore + Camera2 fallback) with a runtime decision; this finding doesn't change the architecture, only the expectation of which path gets verified first.

### Hardware H.264 (AVC) encoder — **verified**

In `/vendor/etc/media_codecs.xml`:
```
<MediaCodec name="OMX.Exynos.AVC.Encoder" type="video/avc" >
<MediaCodec name="OMX.Exynos.AVC.Encoder.secure" type="video/avc-wfd" >
```
Actually accessing this encoder via `AMediaCodec` (NDK) will be verified in M3, but its presence is confirmed.

### Known inconsistency — declared minSdk vs. actual requirement (found in M2/M3)

`H264EncoderAndroid` uses `AMediaCodec_createInputSurface`/`AMediaCodec_signalEndOfInputStream` for the zero-copy (Camera→Surface→Encoder) path — these require **API 26+** (the build uses `android_api_level=26`). But in the current simple (non-Gradle) export path, `export_presets.cfg`'s `gradle_build/min_sdk` field **can't be used** (Godot rejects it while "Use Gradle Build" is off) — meaning the generated APK's manifest still carries Godot's stock template's embedded minSdk (likely 24). This means the `.so` would crash with missing symbols on an API 24-25 device; not an issue for the A30s (API 30), but needs to be resolved before real distribution:
- **Option A:** switch to `gradle_build/use_gradle_build=true` and declare minSdk as 26 (more complex build).
- **Option B:** add an `AImageReader`+ByteBuffer-based fallback encoder path for API 24-25 (more code, not zero-copy).
For now this is a documented known gap; it doesn't affect A30s testing.

### IMU — **verified, rate limit noted**

`dumpsys sensorservice` output (STM LSM6DSL package):

| Sensor | Driver | Mode | Rate range |
|---|---|---|---|
| Accelerometer | LSM6DSL | continuous, non-wakeUp, no batching | 5.00–100.00 Hz |
| Gyroscope | LSM6DSL | continuous, non-wakeUp, no batching | 5.00–100.00 Hz |
| Gyroscope (uncalibrated) | LSM6DSL | same | same |

**Note:** A max of 100Hz is lower than the 200–400Hz gyro on some higher-end devices, but usable for VIO-style purposes. `SENSOR_DELAY_GAME` will in practice correspond to ~100Hz on this device. `ImuSampler` needs to be written to read the rate from the device via `ASensor_getMinDelay()` rather than assuming a fixed target rate — consistent with the no-magic-numbers rule.

**Not yet verified (requires code, to be handled in M1/M2):** epoch/drift consistency between `SensorEvent.timestamp` and `SystemClock.elapsedRealtimeNanos()` — can't be measured via passive ADB inspection, needs to be tested with actual sampling code.

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
