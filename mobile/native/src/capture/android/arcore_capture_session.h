#pragma once

#include "gl_blit_renderer.h"
#include "../../net/protocol.h"

#include <android/native_window.h>
#include <arcore_c_api.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <future>
#include <string>
#include <thread>
#include <vector>

namespace arstream {

// The ARCore-backed capture path -- CaptureController's other option
// besides Camera2CaptureSession, chosen when ARCore reports
// SUPPORTED_INSTALLED (see arcore_availability.h). Owns the ArSession and
// a dedicated render thread that drives ArSession_update() -> extracts
// pose/point-cloud/intrinsics -> blits the camera texture to the
// encoder's Surface (via GlBlitRenderer). This one thread is both "the
// camera capture loop" and "the video frame producer" at once, unlike
// Camera2CaptureSession where those are separate (NDK camera callback
// thread vs nothing, since Camera2 writes to the encoder Surface with
// zero CPU involvement).
//
// Camera2CaptureSession and this class are mutually exclusive at runtime
// -- Android cannot open the same physical camera from two independent
// clients simultaneously (see docs/ARCHITECTURE.md), so CaptureController
// owns at most one of the two at a time.
//
// AR_FOCUS_MODE_FIXED (see start()) matches Camera2CaptureSession's own
// AF/OIS/EIS-off policy (see its header comment) -- a moving/refocusing
// lens breaks the fixed-intrinsics assumption downstream (VIO/SLAM-style)
// consumers depend on.
class ArCoreCaptureSession {
public:
	using ErrorCallback = std::function<void(const std::string &message)>;
	// y_plane here is luma (single channel, 8-bit) even though ARCore's
	// native output is an RGBA GL texture -- converted before this fires,
	// so CaptureController exposes ONE PreviewFrameCallback shape
	// regardless of backend (see capture_controller.h) and ar_capture.cpp's
	// on_preview_frame() doesn't need to know which backend produced the data.
	using PreviewFrameCallback = std::function<void(const uint8_t *y_plane, int32_t width, int32_t height, int32_t row_stride)>;
	// tracking_state matches ArTrackingState (0=TRACKING, 1=PAUSED,
	// 2=STOPPED), the same byte protocol::encode_pose_sample expects.
	using PoseCallback = std::function<void(int64_t timestamp_ns, uint8_t tracking_state, float x, float y, float z, float qx, float qy, float qz, float qw)>;
	using PointCloudCallback = std::function<void(int64_t timestamp_ns, const std::vector<protocol::Point> &points)>;
	using IntrinsicsCallback = std::function<void(float fx, float fy, float cx, float cy, uint32_t width, uint32_t height)>;

	~ArCoreCaptureSession();

	bool start(ANativeWindow *encoder_surface, int32_t width, int32_t height,
			int32_t preview_width, int32_t preview_height,
			ErrorCallback on_error, PreviewFrameCallback on_preview_frame,
			PoseCallback on_pose, PointCloudCallback on_point_cloud, IntrinsicsCallback on_intrinsics,
			std::string &out_error);
	void stop();

	void set_preview_enabled(bool enabled) { preview_enabled_ = enabled; }
	bool is_preview_enabled() const { return preview_enabled_; }

private:
	ArSession *session_ = nullptr;
	ArFrame *frame_ = nullptr;
	GlBlitRenderer renderer_;
	uint32_t camera_texture_ = 0;

	// Handed from start() to render_loop() -- EGL contexts are per-thread,
	// so ALL GL/EGL setup (renderer_.init/create_camera_texture/bind_window)
	// AND ArSession_setCameraTextureName/ArSession_resume (which need a
	// current GL context on the SAME thread ArSession_update() will later
	// run on) happen inside render_loop(), on render_thread_ -- not in
	// start() itself, which runs on whatever thread called it. Confirmed
	// on-device: doing GL setup on the calling thread and then calling
	// ArSession_update() from render_thread_ produced a tight
	// AR_ERROR_MISSING_GL_CONTEXT retry loop (no context current on that thread).
	ANativeWindow *encoder_surface_ = nullptr;
	int32_t width_ = 0;
	int32_t height_ = 0;
	int32_t preview_width_ = 0;
	int32_t preview_height_ = 0;

	ErrorCallback on_error_;
	PreviewFrameCallback on_preview_frame_;
	PoseCallback on_pose_;
	PointCloudCallback on_point_cloud_;
	IntrinsicsCallback on_intrinsics_;

	std::thread render_thread_;
	std::atomic<bool> running_{ false };
	std::atomic<bool> preview_enabled_{ false };
	// Intrinsics don't change frame to frame (fixed focus, fixed camera
	// config) -- reported once, not every frame, matching
	// docs/PROTOCOL.md's CAMERA_INTRINSICS being a snapshot rather than a
	// per-frame message (see server/plugins/recorder.py's on_camera_intrinsics).
	bool intrinsics_reported_ = false;

	// init_result: empty string means the thread's GL/ArSession setup
	// succeeded, non-empty is the error message -- start() blocks on this
	// (via the future) to report success/failure synchronously to its
	// caller, the same as if all of it had run inline.
	void render_loop(std::promise<std::string> init_result);
	void extract_pose(int64_t timestamp_ns);
	void extract_point_cloud(int64_t timestamp_ns);
	void extract_intrinsics();
	// Picks the supported ArCameraConfig whose aspect ratio (then pixel
	// count) is closest to width_/height_ and applies it via
	// ArSession_setCameraConfig, BEFORE ArSession_resume (required --
	// AR_ERROR_SESSION_NOT_PAUSED otherwise). Non-fatal on failure: this
	// only affects output quality (ARCore's own default config might not
	// match the encoder's aspect ratio, stretching the image when blitted
	// -- confirmed on-device, see docs/DEVICE_COMPATIBILITY.md), so a
	// failure here just falls back to ARCore's default rather than
	// blocking capture entirely.
	void select_matching_camera_config();
};

} // namespace arstream
