#include "capture_controller.h"

#include "arcore_availability.h"

#include <android/log.h>
#include <sys/system_properties.h>

namespace arstream {

int32_t CaptureController::query_back_camera_sensor_orientation() {
	return Camera2CaptureSession::query_back_camera_sensor_orientation();
}

std::string CaptureController::read_backend_override() {
	char value[PROP_VALUE_MAX] = { 0 };
	__system_property_get("debug.arstream.capture_backend", value);
	return std::string(value);
}

bool CaptureController::start(ANativeWindow *encoder_surface, int32_t width, int32_t height,
		int32_t preview_width, int32_t preview_height,
		ErrorCallback on_error, PreviewFrameCallback on_preview_frame,
		PoseCallback on_pose, PointCloudCallback on_point_cloud, IntrinsicsCallback on_intrinsics,
		std::string &out_error) {
	std::string override_value = read_backend_override();
	bool forced_arcore = override_value == "arcore";
	bool forced_camera2 = override_value == "camera2";

	bool want_arcore = forced_arcore ||
			(!forced_camera2 && arcore_availability::get_cached_result() == "SUPPORTED_INSTALLED");

	if (want_arcore) {
		arcore_session_ = std::make_unique<ArCoreCaptureSession>();
		if (arcore_session_->start(encoder_surface, width, height, preview_width, preview_height,
					on_error, on_preview_frame, on_pose, on_point_cloud, on_intrinsics, out_error)) {
			active_backend_ = Backend::kArCore;
			return true;
		}
		__android_log_print(ANDROID_LOG_WARN, "arstream", "ArCore session start failed: %s", out_error.c_str());
		arcore_session_.reset();
		if (forced_arcore) {
			// No silent fallback when the backend was explicitly forced --
			// the caller asked for ArCore specifically and it's not
			// available, that's a real error to surface, not paper over.
			return false;
		}
		// "auto": fall through to Camera2 below, same as if ArCore had
		// never been considered.
	}

	camera2_session_ = std::make_unique<Camera2CaptureSession>();
	if (!camera2_session_->start(encoder_surface, width, height, preview_width, preview_height,
				on_error, on_preview_frame, out_error)) {
		camera2_session_.reset();
		return false;
	}
	active_backend_ = Backend::kCamera2;
	return true;
}

void CaptureController::stop() {
	if (arcore_session_) {
		arcore_session_->stop();
		arcore_session_.reset();
	}
	if (camera2_session_) {
		camera2_session_->stop();
		camera2_session_.reset();
	}
}

void CaptureController::set_preview_enabled(bool enabled) {
	if (arcore_session_) {
		arcore_session_->set_preview_enabled(enabled);
	}
	if (camera2_session_) {
		camera2_session_->set_preview_enabled(enabled);
	}
}

bool CaptureController::is_preview_enabled() const {
	if (arcore_session_) {
		return arcore_session_->is_preview_enabled();
	}
	if (camera2_session_) {
		return camera2_session_->is_preview_enabled();
	}
	return false;
}

} // namespace arstream
