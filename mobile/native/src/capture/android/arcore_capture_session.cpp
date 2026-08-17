#include "arcore_capture_session.h"

#include "../../platform/android/android_context.h"

#include <android/log.h>

#include <cmath>

namespace arstream {

ArCoreCaptureSession::~ArCoreCaptureSession() {
	stop();
}

bool ArCoreCaptureSession::start(ANativeWindow *encoder_surface, int32_t width, int32_t height,
		int32_t preview_width, int32_t preview_height,
		ErrorCallback on_error, PreviewFrameCallback on_preview_frame,
		PoseCallback on_pose, PointCloudCallback on_point_cloud, IntrinsicsCallback on_intrinsics,
		std::string &out_error) {
	JNIEnv *env = android_context::get_env();
	jobject activity = android_context::get_activity();
	if (env == nullptr || activity == nullptr) {
		out_error = "JNIEnv*/Activity not set yet (JNI bootstrap hasn't run)";
		return false;
	}

	if (ArSession_create(env, activity, &session_) != AR_SUCCESS || session_ == nullptr) {
		out_error = "ArSession_create failed";
		return false;
	}

	ArConfig *config = nullptr;
	ArConfig_create(session_, &config);
	// Fixed focus -- matches Camera2CaptureSession's own AF/OIS/EIS-off
	// policy (see this class's header comment).
	ArConfig_setFocusMode(session_, config, AR_FOCUS_MODE_FIXED);
	ArStatus configure_status = ArSession_configure(session_, config);
	ArConfig_destroy(config);
	if (configure_status != AR_SUCCESS) {
		out_error = "ArSession_configure failed";
		ArSession_destroy(session_);
		session_ = nullptr;
		return false;
	}

	encoder_surface_ = encoder_surface;
	width_ = width;
	height_ = height;

	// Must run before ArSession_resume() -- ArSession_setCameraConfig
	// returns AR_ERROR_SESSION_NOT_PAUSED otherwise (see its doc comment).
	select_matching_camera_config();

	ArFrame_create(session_, &frame_);
	preview_width_ = preview_width;
	preview_height_ = preview_height;
	on_error_ = std::move(on_error);
	on_preview_frame_ = std::move(on_preview_frame);
	on_pose_ = std::move(on_pose);
	on_point_cloud_ = std::move(on_point_cloud);
	on_intrinsics_ = std::move(on_intrinsics);
	intrinsics_reported_ = false;

	// GL/EGL setup + ArSession_setCameraTextureName + ArSession_resume all
	// happen INSIDE render_loop(), on render_thread_ -- see the member
	// comment in the header for why. init_result carries the outcome back
	// here synchronously, so start()'s caller sees the same success/failure
	// contract as if everything had run inline.
	std::promise<std::string> init_promise;
	std::future<std::string> init_future = init_promise.get_future();
	running_ = true;
	render_thread_ = std::thread(&ArCoreCaptureSession::render_loop, this, std::move(init_promise));

	std::string init_error = init_future.get();
	if (!init_error.empty()) {
		out_error = init_error;
		running_ = false;
		if (render_thread_.joinable()) {
			render_thread_.join();
		}
		ArFrame_destroy(frame_);
		frame_ = nullptr;
		ArSession_destroy(session_);
		session_ = nullptr;
		return false;
	}
	return true;
}

void ArCoreCaptureSession::stop() {
	running_ = false;
	if (render_thread_.joinable()) {
		render_thread_.join();
	}
	if (session_ != nullptr) {
		ArSession_pause(session_);
	}
	if (frame_ != nullptr) {
		ArFrame_destroy(frame_);
		frame_ = nullptr;
	}
	renderer_.unbind_window();
	if (session_ != nullptr) {
		ArSession_destroy(session_);
		session_ = nullptr;
	}
}

void ArCoreCaptureSession::render_loop(std::promise<std::string> init_result) {
	std::string gl_error;
	if (!renderer_.init(gl_error)) {
		init_result.set_value("GlBlitRenderer::init failed: " + gl_error);
		return;
	}
	camera_texture_ = renderer_.create_camera_texture();
	ArSession_setCameraTextureName(session_, camera_texture_);

	if (!renderer_.bind_window(encoder_surface_, gl_error)) {
		init_result.set_value("GlBlitRenderer::bind_window failed: " + gl_error);
		return;
	}

	// rotation=0 (Surface.ROTATION_0): this project locks the window to
	// portrait (see mobile/project.godot's window/handheld/orientation)
	// and phones' natural orientation is portrait, so ROTATION_0 is the
	// correct constant for the only orientation this app ever runs in --
	// not queried dynamically since there's nothing to react to.
	ArSession_setDisplayGeometry(session_, 0, width_, height_);

	if (ArSession_resume(session_) != AR_SUCCESS) {
		init_result.set_value("ArSession_resume failed (camera permission missing, or camera in use?)");
		return;
	}

	init_result.set_value(""); // success -- start() unblocks here

	while (running_) {
		// AR_UPDATE_MODE_BLOCKING is ArCore's default (not explicitly set
		// above) -- this call itself paces the loop to the camera's frame
		// rate (waits up to a built-in 66ms timeout for a new image), no
		// manual sleep needed.
		if (ArSession_update(session_, frame_) != AR_SUCCESS) {
			continue;
		}

		int64_t timestamp_ns = 0;
		ArFrame_getTimestamp(session_, frame_, &timestamp_ns);
		if (timestamp_ns == 0) {
			// Startup: camera hasn't produced a real image yet (see
			// ArSession_update's doc comment) -- nothing to draw/extract.
			continue;
		}

		renderer_.blit_to_window(camera_texture_);

		if (preview_enabled_ && on_preview_frame_) {
			std::vector<uint8_t> rgba = renderer_.blit_to_rgba_buffer(camera_texture_, preview_width_, preview_height_);
			if (!rgba.empty()) {
				// Convert to single-channel luma so CaptureController's
				// PreviewFrameCallback shape stays IDENTICAL across
				// backends (see this class's header comment) -- Camera2
				// already delivers Y8 natively, this is the ARCore-side
				// equivalent conversion.
				std::vector<uint8_t> luma(static_cast<size_t>(preview_width_) * preview_height_);
				for (size_t i = 0; i < luma.size(); i++) {
					const uint8_t *px = &rgba[i * 4];
					luma[i] = static_cast<uint8_t>((static_cast<uint32_t>(px[0]) * 77 + static_cast<uint32_t>(px[1]) * 150 + static_cast<uint32_t>(px[2]) * 29) >> 8);
				}
				on_preview_frame_(luma.data(), preview_width_, preview_height_, preview_width_);
			}
		}

		extract_pose(timestamp_ns);
		extract_point_cloud(timestamp_ns);
		if (!intrinsics_reported_) {
			extract_intrinsics();
		}
	}
}

void ArCoreCaptureSession::extract_pose(int64_t timestamp_ns) {
	if (!on_pose_) {
		return;
	}
	ArCamera *camera = nullptr;
	ArFrame_acquireCamera(session_, frame_, &camera);
	if (camera == nullptr) {
		return;
	}

	ArTrackingState tracking_state = AR_TRACKING_STATE_STOPPED;
	ArCamera_getTrackingState(session_, camera, &tracking_state);

	ArPose *pose = nullptr;
	ArPose_create(session_, nullptr, &pose);
	ArCamera_getPose(session_, camera, pose);
	float raw[7]; // qx, qy, qz, qw, tx, ty, tz -- see ArPose_create's doc comment
	ArPose_getPoseRaw(session_, pose, raw);
	ArPose_destroy(pose);
	ArCamera_release(camera);

	on_pose_(timestamp_ns, static_cast<uint8_t>(tracking_state),
			raw[4], raw[5], raw[6], raw[0], raw[1], raw[2], raw[3]);
}

void ArCoreCaptureSession::extract_point_cloud(int64_t timestamp_ns) {
	if (!on_point_cloud_) {
		return;
	}
	ArPointCloud *point_cloud = nullptr;
	if (ArFrame_acquirePointCloud(session_, frame_, &point_cloud) != AR_SUCCESS || point_cloud == nullptr) {
		return;
	}

	int32_t count = 0;
	ArPointCloud_getNumberOfPoints(session_, point_cloud, &count);
	if (count > 0) {
		const float *data = nullptr; // 4 floats/point: x, y, z, confidence
		ArPointCloud_getData(session_, point_cloud, &data);
		std::vector<protocol::Point> points;
		points.reserve(static_cast<size_t>(count));
		for (int32_t i = 0; i < count; i++) {
			protocol::Point p;
			p.x = data[i * 4 + 0];
			p.y = data[i * 4 + 1];
			p.z = data[i * 4 + 2];
			p.confidence = data[i * 4 + 3];
			points.push_back(p);
		}
		on_point_cloud_(timestamp_ns, points);
	}
	ArPointCloud_release(point_cloud);
}

void ArCoreCaptureSession::extract_intrinsics() {
	if (!on_intrinsics_) {
		return;
	}
	ArCamera *camera = nullptr;
	ArFrame_acquireCamera(session_, frame_, &camera);
	if (camera == nullptr) {
		return;
	}

	ArCameraIntrinsics *intrinsics = nullptr;
	ArCameraIntrinsics_create(session_, &intrinsics);
	ArCamera_getImageIntrinsics(session_, camera, intrinsics);

	float fx = 0, fy = 0, cx = 0, cy = 0;
	int32_t img_width = 0, img_height = 0;
	ArCameraIntrinsics_getFocalLength(session_, intrinsics, &fx, &fy);
	ArCameraIntrinsics_getPrincipalPoint(session_, intrinsics, &cx, &cy);
	ArCameraIntrinsics_getImageDimensions(session_, intrinsics, &img_width, &img_height);

	ArCameraIntrinsics_destroy(intrinsics);
	ArCamera_release(camera);

	if (img_width > 0 && img_height > 0) {
		on_intrinsics_(fx, fy, cx, cy, static_cast<uint32_t>(img_width), static_cast<uint32_t>(img_height));
		intrinsics_reported_ = true;
	}
}

void ArCoreCaptureSession::select_matching_camera_config() {
	if (width_ <= 0 || height_ <= 0) {
		return;
	}

	ArCameraConfigFilter *filter = nullptr;
	ArCameraConfigFilter_create(session_, &filter);
	ArCameraConfigList *list = nullptr;
	ArCameraConfigList_create(session_, &list);
	ArSession_getSupportedCameraConfigsWithFilter(session_, filter, list);

	int32_t count = 0;
	ArCameraConfigList_getSize(session_, list, &count);
	if (count <= 0) {
		ArCameraConfigList_destroy(list);
		ArCameraConfigFilter_destroy(filter);
		return;
	}

	// ARCore's own default camera config isn't guaranteed to match the
	// encoder's requested aspect ratio (e.g. it picked 640x480/4:3 against
	// a 1280x720/16:9 encoder target on-device) -- blit_to_window() just
	// fills the destination viewport, so a mismatch stretches the image.
	// Closest aspect ratio wins; closest pixel count breaks ties.
	float target_aspect = static_cast<float>(width_) / static_cast<float>(height_);
	float best_score = -1.0f;
	int32_t best_index = -1;

	ArCameraConfig *scratch = nullptr;
	ArCameraConfig_create(session_, &scratch);
	for (int32_t i = 0; i < count; i++) {
		ArCameraConfigList_getItem(session_, list, i, scratch);
		int32_t w = 0, h = 0;
		ArCameraConfig_getImageDimensions(session_, scratch, &w, &h);
		if (w <= 0 || h <= 0) {
			continue;
		}
		float aspect_diff = std::fabs(static_cast<float>(w) / static_cast<float>(h) - target_aspect);
		float area_diff = std::fabs(static_cast<float>(w) * h - static_cast<float>(width_) * height_) / 1000000.0f;
		float score = aspect_diff * 1000.0f + area_diff;
		if (best_index == -1 || score < best_score) {
			best_score = score;
			best_index = i;
		}
	}

	if (best_index != -1) {
		ArCameraConfigList_getItem(session_, list, best_index, scratch);
		if (ArSession_setCameraConfig(session_, scratch) != AR_SUCCESS) {
			__android_log_print(ANDROID_LOG_WARN, "arstream", "ArSession_setCameraConfig failed, using ArCore's default");
		}
	}
	ArCameraConfig_destroy(scratch);
	ArCameraConfigList_destroy(list);
	ArCameraConfigFilter_destroy(filter);
}

} // namespace arstream
