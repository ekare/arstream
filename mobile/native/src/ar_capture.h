#pragma once

#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>

#ifdef ANDROID_ENABLED
#include "capture/android/camera2_capture_session.h"
#include "encode/h264_encoder_android.h"
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

#ifdef ANDROID_ENABLED
private:
	std::unique_ptr<arstream::Camera2CaptureSession> capture_session_;
	std::unique_ptr<arstream::H264EncoderAndroid> encoder_;
	std::unique_ptr<arstream::OutputSink> sink_;
	bool capturing_ = false;

	int64_t stat_frames_ = 0;
	int64_t stat_bytes_ = 0;
	std::chrono::steady_clock::time_point stat_start_time_;

	// ACAMERA_SENSOR_ORIENTATION -- the camera sensor is physically mounted
	// rotated relative to the device's natural orientation (90 degrees on
	// most phones). Queried in start_capture() BEFORE the encoder (see the
	// "query_back_camera_sensor_orientation" note in camera2_capture_
	// session.h) and used both to rotate the preview correctly and to fill
	// in the VIDEO_CONFIG metadata.
	int32_t sensor_orientation_ = 0;

	void on_encoder_config(const uint8_t *sps_pps, size_t size);
	void on_encoded_chunk(const uint8_t *data, size_t size, int64_t timestamp_ns, bool is_keyframe);
	void on_capture_error(const std::string &message);
	void on_preview_frame(const uint8_t *y_plane, int32_t width, int32_t height, int32_t row_stride);
#endif
};

} // namespace godot
