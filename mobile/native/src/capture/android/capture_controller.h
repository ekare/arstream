#pragma once

#include "arcore_capture_session.h"
#include "camera2_capture_session.h"

#include <memory>
#include <string>

namespace arstream {

// Runtime ARCore-vs-Camera2 dispatch layer -- see docs/ARCHITECTURE.md's
// "Key decisions" table. Exactly one of the two backend sessions is ever
// alive at a time -- Android cannot open the same physical camera from
// two independent clients simultaneously, so this isn't a preference,
// it's a hard constraint (see docs/ARCHITECTURE.md).
//
// Decision source: arcore_availability's CACHED result from the one check
// that ran at app startup (see ar_capture.cpp's
// nativeCheckArcoreAvailability) -- NOT re-queried per start() call,
// since ArCoreApk_checkAvailabilityAsync must run on Android's UI thread
// (confirmed on-device, SIGABRT otherwise) and start() is called from
// GDScript, which isn't that thread.
//
// Debug override: the Android system property
// "debug.arstream.capture_backend" ("camera2"/"arcore"/unset) forces a
// specific backend for on-device testing -- persistent across app
// restarts (unlike an in-memory GDScript-set flag), set via
// `adb shell setprop debug.arstream.capture_backend arcore`. In "auto"
// (unset/anything else), a failing ArCore session falls back to Camera2
// silently; forced "arcore" fails loudly instead (no silent fallback --
// see start()).
class CaptureController {
public:
	enum class Backend { kCamera2, kArCore };

	using ErrorCallback = Camera2CaptureSession::ErrorCallback;
	using PreviewFrameCallback = Camera2CaptureSession::PreviewFrameCallback;
	using PoseCallback = ArCoreCaptureSession::PoseCallback;
	using PointCloudCallback = ArCoreCaptureSession::PointCloudCallback;
	using IntrinsicsCallback = ArCoreCaptureSession::IntrinsicsCallback;

	static int32_t query_back_camera_sensor_orientation();

	// on_pose/on_point_cloud/on_intrinsics only ever fire when the ArCore
	// backend ends up active -- harmless to always pass, they just never
	// get called under Camera2.
	bool start(ANativeWindow *encoder_surface, int32_t width, int32_t height,
			int32_t preview_width, int32_t preview_height,
			ErrorCallback on_error, PreviewFrameCallback on_preview_frame,
			PoseCallback on_pose, PointCloudCallback on_point_cloud, IntrinsicsCallback on_intrinsics,
			std::string &out_error);
	void stop();

	void set_preview_enabled(bool enabled);
	bool is_preview_enabled() const;

	// Only meaningful after a successful start() -- callers (ar_capture.cpp)
	// use this to decide whether the ArCore-side rotation/intrinsics
	// handling applies (see the note in ar_capture.cpp's start_capture()).
	Backend active_backend() const { return active_backend_; }

private:
	std::unique_ptr<Camera2CaptureSession> camera2_session_;
	std::unique_ptr<ArCoreCaptureSession> arcore_session_;
	Backend active_backend_ = Backend::kCamera2;

	// "camera2" | "arcore" | "" (unset/anything else = auto).
	static std::string read_backend_override();
};

} // namespace arstream
