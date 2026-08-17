#include "arcore_availability.h"

#include "../../platform/android/android_context.h"

#include <arcore_c_api.h>

#include <android/log.h>

#include <mutex>

namespace {

std::mutex g_cache_mutex;
std::string g_cached_result;

std::string to_string(ArAvailability availability) {
	switch (availability) {
		case AR_AVAILABILITY_UNKNOWN_ERROR:
			return "UNKNOWN_ERROR";
		case AR_AVAILABILITY_UNKNOWN_CHECKING:
			return "UNKNOWN_CHECKING";
		case AR_AVAILABILITY_UNKNOWN_TIMED_OUT:
			return "UNKNOWN_TIMED_OUT";
		case AR_AVAILABILITY_UNSUPPORTED_DEVICE_NOT_CAPABLE:
			return "UNSUPPORTED_DEVICE_NOT_CAPABLE";
		case AR_AVAILABILITY_SUPPORTED_NOT_INSTALLED:
			return "SUPPORTED_NOT_INSTALLED";
		case AR_AVAILABILITY_SUPPORTED_APK_TOO_OLD:
			return "SUPPORTED_APK_TOO_OLD";
		case AR_AVAILABILITY_SUPPORTED_INSTALLED:
			return "SUPPORTED_INSTALLED";
	}
	return "UNKNOWN_ERROR";
}

// ArCoreApk_checkAvailabilityAsync's callback is a plain C function
// pointer with a void* context -- the caller's std::function is heap-
// allocated in check_async() and reclaimed here, matching the "free
// callback_context at the end of the callback" guidance in arcore_c_api.h.
void on_availability_result(void *callback_context, ArAvailability availability) {
	auto *callback = static_cast<arstream::arcore_availability::ResultCallback *>(callback_context);
	(*callback)(to_string(availability));
	delete callback;
}

} // namespace

namespace arstream::arcore_availability {

// MUST be called from Android's main/UI thread (the thread with a prepared
// Looper) -- confirmed on-device: calling ArCoreApk_checkAvailabilityAsync
// from an arbitrary background std::thread aborts inside
// libarcore_sdk_c.so (SIGABRT, ArCoreApk_checkAvailabilityAsync+272; ARCore
// apparently asserts on this internally). This is why
// JniBootstrapPlugin.kt's onGodotMainLoopStarted() -- which already runs
// on the Activity's UI thread -- triggers this directly (via
// nativeCheckArcoreAvailability in ar_capture.cpp) immediately after
// nativeSetContext(), rather than GDScript calling it from Godot's own
// separate render/game thread.
void check_async(ResultCallback callback) {
	JNIEnv *env = android_context::get_env();
	jobject activity = android_context::get_activity();
	if (env == nullptr || activity == nullptr) {
		__android_log_print(ANDROID_LOG_WARN, "arstream", "arcore_availability: JNIEnv*/Activity not set yet");
		callback("UNKNOWN_ERROR");
		return;
	}
	auto *heap_callback = new ResultCallback([callback = std::move(callback)](const std::string &result) {
		{
			std::lock_guard<std::mutex> lock(g_cache_mutex);
			g_cached_result = result;
		}
		callback(result);
	});
	ArCoreApk_checkAvailabilityAsync(env, activity, heap_callback, on_availability_result);
}

std::string get_cached_result() {
	std::lock_guard<std::mutex> lock(g_cache_mutex);
	return g_cached_result;
}

} // namespace arstream::arcore_availability
