#pragma once

#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>

#ifdef ANDROID_ENABLED
#include "capture/android/arcore_availability.h"
#include "capture/android/capture_controller.h"
#include "encode/h264_encoder_android.h"
#include "sensors/android/sensor_sampler.h"
#include "sink/output_sink.h"
#include <chrono>
#include <memory>
#endif

namespace godot {

// The single surface exposed to GDScript: ArCapture. The hot data path
// (capture/encode/write-to-file) lives here, on the native side -- see the
// "Architectural decisions" section of docs/ARCHITECTURE.md. M2/M3: Camera2
// fallback + AMediaCodec H.264 encode + "save" (file) / "stream" (stub,
// M4/M5) selectable output + preview that can be toggled on/off (async)
// independently of recording.
class ArCapture : public RefCounted {
	GDCLASS(ArCapture, RefCounted)

private:
	static ArCapture *singleton;

	Ref<ImageTexture> preview_texture_;
	int preview_width_ = 640;
	int preview_height_ = 360;

protected:
	static void _bind_methods();

public:
	static ArCapture *get_singleton();

	ArCapture();
	~ArCapture();

	// M1 smoke test: proves the GDScript -> native -> signal round trip.
	void ping(const String &p_message);

	// M2/M3/M5: capture + encode + record/stream.
	// config: {mode: "save"|"stream",
	//          output_path: String,              -- save only
	//          host: String, port: int,           -- stream only
	//          spool_path: String,                -- stream only: disk file used once memory fills up
	//          width/height/fps/bitrate_bps: int}
	void start_capture(const Dictionary &p_config);
	void stop_capture();

	// Preview, toggleable independently of recording (async) -- GDScript
	// first assigns an empty texture to the TextureRect via
	// get_preview_texture(), then turns it on/off whenever it wants.
	// WHILE capture (recording/streaming) is running, this just flips a flag
	// on the same (encoder-backed) session. WHILE capture is NOT running, it
	// opens/closes its own encoder-less camera session -- so preview also
	// works before recording starts and after it stops (see ar_capture.cpp).
	Ref<ImageTexture> get_preview_texture();
	void set_preview_enabled(bool p_enabled);
	// call_deferred target -- must not be called off the main thread.
	void _update_preview_texture(PackedByteArray p_data, int p_width, int p_height);

	// Faz B gate: asks ARCore whether it's supported/installed on this
	// device. Async -- result arrives via the arcore_availability_checked
	// signal, e.g. "SUPPORTED_INSTALLED"/"SUPPORTED_NOT_INSTALLED"/
	// "UNSUPPORTED_DEVICE_NOT_CAPABLE" (see arcore_availability.h). Only
	// "SUPPORTED_INSTALLED" means CaptureController may pick the ARCore
	// backend once Faz C wires that decision in -- every other result
	// means Camera2 fallback, same as a platform with no ARCore at all.
	//
	// NOT bound to GDScript (see ar_capture.cpp's _bind_methods): ARCore's
	// async check must run on Android's UI thread or it aborts (confirmed
	// on-device, SIGABRT inside libarcore_sdk_c.so) -- neither GDScript's
	// calling thread nor onGodotMainLoopStarted() itself is that thread
	// (both run on Godot's own engine thread). Only ever called from the
	// nativeCheckArcoreAvailability JNI trampoline, which
	// JniBootstrapPlugin.kt invokes from inside an Activity.runOnUiThread{}
	// block to land on the real UI thread.
	void check_arcore_availability();

#ifdef ANDROID_ENABLED
private:
	std::unique_ptr<arstream::CaptureController> capture_controller_;
	std::unique_ptr<arstream::H264EncoderAndroid> encoder_;
	std::unique_ptr<arstream::OutputSink> sink_;
	// Runs UNCONDITIONALLY whenever capturing_, independent of which camera
	// backend is active -- sensors are an independent subsystem, and the
	// project always sends raw telemetry alongside whatever the camera
	// backend additionally provides (see sensor_sampler.h).
	std::unique_ptr<arstream::SensorSampler> sensor_sampler_;
	bool capturing_ = false;

	int64_t stat_frames_ = 0;
	int64_t stat_bytes_ = 0;
	std::chrono::steady_clock::time_point stat_start_time_;

	// ACAMERA_SENSOR_ORIENTATION -- the camera sensor is physically mounted
	// rotated relative to the device's natural orientation (90 degrees on
	// most phones). Queried in start_capture() BEFORE the encoder (see the
	// "query_back_camera_sensor_orientation" note in capture_controller.h /
	// camera2_capture_session.h) and used both to rotate the preview
	// correctly and to fill in the VIDEO_CONFIG metadata.
	//
	// This value is a Camera2-specific query and only applies when
	// CaptureController ends up choosing the Camera2 backend -- when it
	// picks ArCore instead, this is forced to 0 right after
	// capture_controller_->start() returns (see start_capture()): ARCore's
	// GL texture is already correctly oriented via
	// ArSession_setDisplayGeometry (see arcore_capture_session.cpp), so
	// applying Camera2's rotation math on top of it would double-rotate
	// the image.
	int32_t sensor_orientation_ = 0;

	void on_encoder_config(const uint8_t *sps_pps, size_t size);
	void on_encoded_chunk(const uint8_t *data, size_t size, int64_t timestamp_ns, bool is_keyframe);
	void on_capture_error(const std::string &message);
	void on_preview_frame(const uint8_t *y_plane, int32_t width, int32_t height, int32_t row_stride);
	void on_sensor_batch(const std::vector<arstream::protocol::ImuSample> &samples);
	// ArCore-only (see CaptureController/ArCoreCaptureSession) -- these
	// simply never fire under the Camera2 backend.
	void on_pose_sample(int64_t timestamp_ns, uint8_t tracking_state, float x, float y, float z, float qx, float qy, float qz, float qw);
	void on_point_cloud(int64_t timestamp_ns, const std::vector<arstream::protocol::Point> &points);
	void on_camera_intrinsics(float fx, float fy, float cx, float cy, uint32_t width, uint32_t height);
#endif
};

} // namespace godot
