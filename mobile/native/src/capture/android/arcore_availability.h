#pragma once

#include <functional>
#include <string>

namespace arstream::arcore_availability {

// Human-readable form of arcore_c_api.h's ArAvailability enum, e.g.
// "SUPPORTED_INSTALLED", "SUPPORTED_NOT_INSTALLED", "UNSUPPORTED_DEVICE_NOT_CAPABLE".
using ResultCallback = std::function<void(const std::string &availability)>;

// Kicks off ArCoreApk_checkAvailabilityAsync using the JNIEnv*/Activity
// handed over via platform/android/android_context.h. If set_context() has
// not run yet (JniBootstrapPlugin.kt hasn't fired), immediately reports
// "UNKNOWN_ERROR" via callback instead of calling into ARCore.
//
// The callback fires on ARCore's own Main-thread dispatch, per
// arcore_c_api.h -- NOT necessarily the thread check_async() was called
// from. Callers that touch Godot APIs from inside the callback must still
// call_deferred (matches this codebase's existing background-thread ->
// Godot-main-thread convention, see sensor_sampler.h/ar_capture.cpp).
void check_async(ResultCallback callback);

// The result of the last check_async() call, or "" if none has completed
// yet. CaptureController::decide_backend() reads this synchronously at
// start_capture() time -- it does NOT re-run check_async() itself, since
// ArCoreApk_checkAvailabilityAsync must run on Android's UI thread
// (confirmed on-device, SIGABRT otherwise, see ar_capture.cpp) and
// start_capture() is called from GDScript, which isn't that thread. The
// one real check that matters runs once at app startup (see
// ar_capture.cpp's nativeCheckArcoreAvailability), triggered from
// JniBootstrapPlugin.kt's onGodotMainLoopStarted() by way of
// Activity.runOnUiThread{}.
std::string get_cached_result();

} // namespace arstream::arcore_availability
