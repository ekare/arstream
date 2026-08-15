#pragma once

#include <camera/NdkCameraCaptureSession.h>
#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraManager.h>
#include <camera/NdkCaptureRequest.h>
#include <media/NdkImageReader.h>
#include <android/native_window.h>

#include <atomic>
#include <functional>
#include <string>

namespace arstream {

// The fallback path used when ARCore is unavailable (or disabled): raw
// Camera2 NDK capture. While recording/streaming, the same capture request
// writes to TWO targets at once -- the encoder's input surface (zero-copy,
// always active) and a preview AImageReader (small resolution, only
// processed on CPU while preview_enabled=true). This means toggling preview
// on/off never interrupts recording -- same session, same repeating
// request, the only difference is whether the frame gets processed. Outside
// of recording/streaming it can also be opened for preview ONLY, with
// encoder_surface=nullptr (see ArCapture::set_preview_enabled) -- single
// target, TEMPLATE_PREVIEW.
class Camera2CaptureSession {
public:
	using ErrorCallback = std::function<void(const std::string &message)>;
	// y_plane data becomes invalid once the callback returns -- the caller must copy it synchronously.
	using PreviewFrameCallback = std::function<void(const uint8_t *y_plane, int32_t width, int32_t height, int32_t row_stride)>;

	~Camera2CaptureSession();

	// Works INDEPENDENTLY of opening the camera (starting a session) --
	// opens/closes its own temporary ACameraManager. ArCapture calls this
	// BEFORE starting the encoder, because the encoder's first SPS/PPS
	// output (which will carry the rotation info into VIDEO_CONFIG) may
	// arrive before the camera session does.
	//
	// ACAMERA_SENSOR_ORIENTATION: how many degrees CLOCKWISE the sensor's
	// raw output needs to be rotated to reach the device's natural
	// (portrait) orientation -- 0/90/180/270. It's 90 for the back camera on
	// most phones, but this isn't assumed fixed -- it's read from the
	// device. Returns 0 on failure (assumes no rotation -- the
	// display/recording may look skewed, but it won't crash or produce the
	// wrong size).
	static int32_t query_back_camera_sensor_orientation();

	// encoder_surface may be nullptr -- in that case only the preview target
	// is added to the capture request (TEMPLATE_PREVIEW). Used by
	// ArCapture::set_preview_enabled() for preview-only mode outside of recording/streaming.
	bool start(ANativeWindow *encoder_surface, int32_t width, int32_t height,
			int32_t preview_width, int32_t preview_height,
			ErrorCallback on_error, PreviewFrameCallback on_preview_frame,
			std::string &out_error);
	void stop();

	void set_preview_enabled(bool enabled) { preview_enabled_ = enabled; }
	bool is_preview_enabled() const { return preview_enabled_; }

	// Public so the C callbacks can reach it -- no real logic here, just routing.
	void notify_error(const std::string &message);
	void notify_preview_frame(const uint8_t *y_plane, int32_t width, int32_t height, int32_t row_stride);
	AImageReader *preview_reader() const { return preview_reader_; }

private:
	ACameraManager *manager_ = nullptr;
	ACameraDevice *device_ = nullptr;
	ACameraCaptureSession *session_ = nullptr;
	ACaptureSessionOutputContainer *output_container_ = nullptr;
	ACaptureSessionOutput *encoder_output_ = nullptr;
	ACameraOutputTarget *encoder_target_ = nullptr;
	ACaptureSessionOutput *preview_output_ = nullptr;
	ACameraOutputTarget *preview_target_ = nullptr;
	AImageReader *preview_reader_ = nullptr;
	ACaptureRequest *request_ = nullptr;
	ErrorCallback on_error_;
	PreviewFrameCallback on_preview_frame_;
	std::atomic<bool> preview_enabled_{ false };

	std::string find_back_camera_id();
};

} // namespace arstream
