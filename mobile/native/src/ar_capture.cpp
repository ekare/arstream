#include "ar_capture.h"

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/core/class_db.hpp>

#ifdef ANDROID_ENABLED
#include "sink/file_sink.h"
#include "sink/stream_sink.h"
#endif

using namespace godot;

ArCapture *ArCapture::singleton = nullptr;

void ArCapture::_bind_methods() {
	ClassDB::bind_method(D_METHOD("ping", "message"), &ArCapture::ping);
	ADD_SIGNAL(MethodInfo("pong", PropertyInfo(Variant::STRING, "message")));

	ClassDB::bind_method(D_METHOD("start_capture", "config"), &ArCapture::start_capture);
	ClassDB::bind_method(D_METHOD("stop_capture"), &ArCapture::stop_capture);

	ClassDB::bind_method(D_METHOD("get_preview_texture"), &ArCapture::get_preview_texture);
	ClassDB::bind_method(D_METHOD("set_preview_enabled", "enabled"), &ArCapture::set_preview_enabled);
	ClassDB::bind_method(D_METHOD("_update_preview_texture", "data", "width", "height"), &ArCapture::_update_preview_texture);

	ADD_SIGNAL(MethodInfo("capture_started"));
	ADD_SIGNAL(MethodInfo("capture_stopped", PropertyInfo(Variant::STRING, "reason")));
	ADD_SIGNAL(MethodInfo("stats_updated", PropertyInfo(Variant::INT, "frames_encoded"), PropertyInfo(Variant::INT, "bytes_written"), PropertyInfo(Variant::FLOAT, "fps")));
	ADD_SIGNAL(MethodInfo("capture_error", PropertyInfo(Variant::STRING, "message")));
}

ArCapture *ArCapture::get_singleton() {
	return singleton;
}

ArCapture::ArCapture() {
	ERR_FAIL_COND_MSG(singleton != nullptr, "ArCapture already exists -- must be a single singleton.");
	singleton = this;
}

ArCapture::~ArCapture() {
#ifdef ANDROID_ENABLED
	stop_capture();
#endif
	if (singleton == this) {
		singleton = nullptr;
	}
}

void ArCapture::ping(const String &p_message) {
	emit_signal("pong", "arcapture native: " + p_message);
}

// Platform-independent: Image/ImageTexture are Godot's own classes, no
// Android needed. GDScript can get an empty texture even before capture starts.
Ref<ImageTexture> ArCapture::get_preview_texture() {
	if (preview_texture_.is_null()) {
		Ref<Image> img = Image::create_empty(preview_width_, preview_height_, false, Image::FORMAT_L8);
		preview_texture_ = ImageTexture::create_from_image(img);
	}
	return preview_texture_;
}

void ArCapture::_update_preview_texture(PackedByteArray p_data, int p_width, int p_height) {
	if (preview_texture_.is_null()) {
		get_preview_texture();
	}
	Ref<Image> img = Image::create_from_data(p_width, p_height, false, Image::FORMAT_L8, p_data);
	if (img.is_null()) {
		return;
	}
	if (p_width == preview_texture_->get_width() && p_height == preview_texture_->get_height()) {
		preview_texture_->update(img);
	} else {
		preview_texture_->set_image(img);
	}
}

#ifdef ANDROID_ENABLED

void ArCapture::on_encoder_config(const uint8_t *sps_pps, size_t size) {
	if (sink_) {
		sink_->write_video_config(sps_pps, size, sensor_orientation_);
	}
}

void ArCapture::on_encoded_chunk(const uint8_t *data, size_t size, int64_t timestamp_ns, bool is_keyframe) {
	if (sink_) {
		sink_->write_chunk(data, size, timestamp_ns, is_keyframe);
	}
	stat_frames_ += 1;
	stat_bytes_ += static_cast<int64_t>(size);
	// Publish stats every ~15 frames, not every frame -- fps is computed from
	// frame count / actual elapsed time (a measurement, not an assumed target).
	if (stat_frames_ % 15 == 0) {
		double elapsed_sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - stat_start_time_).count();
		double fps = elapsed_sec > 0.0 ? double(stat_frames_) / elapsed_sec : 0.0;
		call_deferred("emit_signal", "stats_updated", stat_frames_, stat_bytes_, fps);
	}
}

void ArCapture::on_capture_error(const std::string &message) {
	call_deferred("emit_signal", "capture_error", String(message.c_str()));
}

void ArCapture::on_preview_frame(const uint8_t *y_plane, int32_t width, int32_t height, int32_t row_stride) {
	// This runs on the camera's own callback thread -- touching Godot's
	// Image/Texture APIs from here isn't safe, so we copy the data right away
	// (accounting for row stride correctly) and defer the rest to the main thread.
	//
	// The clockwise rotation by sensor_orientation_ is applied here -- the
	// camera sensor is physically mounted rotated (see the note in
	// ar_capture.h), otherwise the image would appear sideways in the
	// TextureRect. At 90/270 the dimensions swap (landscape sensor -> portrait preview).
	int32_t out_width = width;
	int32_t out_height = height;
	if (sensor_orientation_ == 90 || sensor_orientation_ == 270) {
		out_width = height;
		out_height = width;
	}

	PackedByteArray buf;
	buf.resize(out_width * out_height);
	uint8_t *dst = buf.ptrw();

	if (sensor_orientation_ == 90) {
		for (int32_t y = 0; y < out_height; y++) {
			for (int32_t x = 0; x < out_width; x++) {
				dst[y * out_width + x] = y_plane[(height - 1 - x) * row_stride + y];
			}
		}
	} else if (sensor_orientation_ == 270) {
		for (int32_t y = 0; y < out_height; y++) {
			for (int32_t x = 0; x < out_width; x++) {
				dst[y * out_width + x] = y_plane[x * row_stride + (width - 1 - y)];
			}
		}
	} else if (sensor_orientation_ == 180) {
		for (int32_t y = 0; y < out_height; y++) {
			for (int32_t x = 0; x < out_width; x++) {
				dst[y * out_width + x] = y_plane[(height - 1 - y) * row_stride + (width - 1 - x)];
			}
		}
	} else {
		for (int32_t row = 0; row < height; row++) {
			memcpy(dst + row * width, y_plane + row * row_stride, width);
		}
	}

	call_deferred("_update_preview_texture", buf, out_width, out_height);
}

void ArCapture::set_preview_enabled(bool p_enabled) {
	if (capturing_) {
		// While recording/streaming: preview already shares the encoder's
		// session, just flip the flag -- no need to reopen the camera.
		if (capture_session_) {
			capture_session_->set_preview_enabled(p_enabled);
		}
		return;
	}

	// While NOT recording/streaming: preview opens/closes its own
	// encoder-less camera session. This lets the user watch the live feed
	// before starting a recording, or after stopping one.
	if (!p_enabled) {
		if (capture_session_) {
			capture_session_->stop();
			capture_session_.reset();
		}
		return;
	}

	if (capture_session_) {
		// A preview-only session is already open -- just flip the flag on.
		capture_session_->set_preview_enabled(true);
		return;
	}

	sensor_orientation_ = arstream::Camera2CaptureSession::query_back_camera_sensor_orientation();

	std::string err;
	capture_session_ = std::make_unique<arstream::Camera2CaptureSession>();
	auto error_cb = [this](const std::string &message) {
		on_capture_error(message);
	};
	auto preview_cb = [this](const uint8_t *y, int32_t w, int32_t h, int32_t stride) {
		on_preview_frame(y, w, h, stride);
	};
	if (!capture_session_->start(nullptr, 0, 0, preview_width_, preview_height_, error_cb, preview_cb, err)) {
		emit_signal("capture_error", String("Could not start preview: ") + String(err.c_str()));
		capture_session_.reset();
		return;
	}
	capture_session_->set_preview_enabled(true);
}

void ArCapture::start_capture(const Dictionary &p_config) {
	if (capturing_) {
		emit_signal("capture_error", "Capture is already running -- call stop_capture() first.");
		return;
	}

	String mode = p_config.get("mode", "save");
	String output_path = p_config.get("output_path", "");
	String host = p_config.get("host", "");
	int port = int(p_config.get("port", 0));
	String spool_path = p_config.get("spool_path", "");

	arstream::H264EncoderAndroid::Config enc_cfg;
	enc_cfg.width = int(p_config.get("width", 1280));
	enc_cfg.height = int(p_config.get("height", 720));
	enc_cfg.fps = int(p_config.get("fps", 30));
	enc_cfg.bitrate_bps = int(p_config.get("bitrate_bps", 4000000));

	// Preview may already have opened its own camera session outside of
	// recording (see set_preview_enabled) -- since the same camera device
	// can't be opened twice with ACameraManager_openCamera, this must be
	// closed before opening the real (encoder-backed) session. Whether
	// preview was on is remembered, and turned back on below on the new session.
	bool preview_was_enabled = false;
	if (capture_session_) {
		preview_was_enabled = capture_session_->is_preview_enabled();
		capture_session_->stop();
		capture_session_.reset();
	}

	// Queried BEFORE the encoder -- uses its own temporary ACameraManager,
	// works even while capture_session_ doesn't exist yet (see camera2_capture_session.h).
	sensor_orientation_ = arstream::Camera2CaptureSession::query_back_camera_sensor_orientation();

	preview_width_ = MAX(enc_cfg.width / 2, 160);
	preview_height_ = MAX(enc_cfg.height / 2, 90);
	// NOTE: preview_texture_ is not unref'd/recreated here -- GDScript
	// (Main.gd) calls get_preview_texture() ONCE in _ready() and assigns it
	// to the TextureRect, holding onto that same Ref<ImageTexture> object
	// permanently. Calling unref() here would release ArCapture's own
	// reference but wouldn't change the (now "orphaned") object still held
	// by the TextureRect -- the screen would stay blank/black forever, with
	// data being written to a DIFFERENT object. Size changes are already
	// handled in place, on the SAME object, via set_image() inside _update_preview_texture().

	std::string err;

	encoder_ = std::make_unique<arstream::H264EncoderAndroid>();
	auto config_cb = [this](const uint8_t *data, size_t size) {
		on_encoder_config(data, size);
	};
	auto chunk_cb = [this](const uint8_t *data, size_t size, int64_t ts, bool key) {
		on_encoded_chunk(data, size, ts, key);
	};
	if (!encoder_->start(enc_cfg, config_cb, chunk_cb, err)) {
		emit_signal("capture_error", String("Could not start encoder: ") + String(err.c_str()));
		encoder_.reset();
		return;
	}

	std::string dest;
	if (mode == "save") {
		sink_ = std::make_unique<arstream::FileSink>();
		dest = std::string(output_path.utf8().get_data());
	} else {
		sink_ = std::make_unique<arstream::StreamSink>(std::string(spool_path.utf8().get_data()));
		dest = std::string(host.utf8().get_data()) + ":" + std::to_string(port);
	}

	if (!sink_->open(dest, err)) {
		emit_signal("capture_error", String(err.c_str()));
		encoder_->stop();
		encoder_.reset();
		sink_.reset();
		return;
	}

	capture_session_ = std::make_unique<arstream::Camera2CaptureSession>();
	auto error_cb = [this](const std::string &message) {
		on_capture_error(message);
	};
	auto preview_cb = [this](const uint8_t *y, int32_t w, int32_t h, int32_t stride) {
		on_preview_frame(y, w, h, stride);
	};
	if (!capture_session_->start(encoder_->get_input_surface(), enc_cfg.width, enc_cfg.height,
				preview_width_, preview_height_, error_cb, preview_cb, err)) {
		emit_signal("capture_error", String("Could not start camera: ") + String(err.c_str()));
		sink_->close();
		sink_.reset();
		encoder_->stop();
		encoder_.reset();
		capture_session_.reset();
		return;
	}

	if (preview_was_enabled) {
		capture_session_->set_preview_enabled(true);
	}

	stat_frames_ = 0;
	stat_bytes_ = 0;
	stat_start_time_ = std::chrono::steady_clock::now();
	capturing_ = true;
	emit_signal("capture_started");
}

void ArCapture::stop_capture() {
	if (!capturing_) {
		return;
	}
	capturing_ = false;

	if (capture_session_) {
		capture_session_->stop();
		capture_session_.reset();
	}
	if (encoder_) {
		encoder_->stop();
		encoder_.reset();
	}
	if (sink_) {
		sink_->close();
		sink_.reset();
	}

	emit_signal("capture_stopped", "stopped");
}

#else // !ANDROID_ENABLED

void ArCapture::start_capture(const Dictionary &p_config) {
	(void)p_config;
	emit_signal("capture_error", "start_capture is only supported on Android.");
}

void ArCapture::stop_capture() {
}

void ArCapture::set_preview_enabled(bool p_enabled) {
	(void)p_enabled;
}

#endif
