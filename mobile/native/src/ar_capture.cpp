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
	ERR_FAIL_COND_MSG(singleton != nullptr, "ArCapture zaten var -- tek singleton olmali.");
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

// Platformdan bagimsiz: Image/ImageTexture Godot'un kendi siniflari, Android
// gerektirmez. GDScript capture baslamadan once bile bos bir doku alabilir.
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

void ArCapture::on_encoded_chunk(const uint8_t *data, size_t size, int64_t timestamp_ns, bool is_keyframe) {
	if (sink_) {
		sink_->write_chunk(data, size, timestamp_ns, is_keyframe);
	}
	stat_frames_ += 1;
	stat_bytes_ += static_cast<int64_t>(size);
	// Her kareyi degil, ~15 karede bir istatistik yayinla -- fps, kare
	// sayisi/gercek-gecen-sure'den hesaplanir (varsayilan hedef degil, olcum).
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
	// Bu, kameranin kendi callback thread'inde calisir -- Godot Image/Texture
	// API'lerine burdan dokunmak guvenli degil, bu yuzden veriyi hemen (row
	// stride'i dogru hesaba katarak) kopyalayip ana thread'e erteliyoruz.
	PackedByteArray buf;
	buf.resize(width * height);
	uint8_t *dst = buf.ptrw();
	for (int32_t row = 0; row < height; row++) {
		memcpy(dst + row * width, y_plane + row * row_stride, width);
	}
	call_deferred("_update_preview_texture", buf, width, height);
}

void ArCapture::set_preview_enabled(bool p_enabled) {
	if (capture_session_) {
		capture_session_->set_preview_enabled(p_enabled);
	}
}

void ArCapture::start_capture(const Dictionary &p_config) {
	if (capturing_) {
		emit_signal("capture_error", "Yakalama zaten calisiyor -- once stop_capture() cagirin.");
		return;
	}

	String mode = p_config.get("mode", "save");
	String output_path = p_config.get("output_path", "");

	arstream::H264EncoderAndroid::Config enc_cfg;
	enc_cfg.width = int(p_config.get("width", 1280));
	enc_cfg.height = int(p_config.get("height", 720));
	enc_cfg.fps = int(p_config.get("fps", 30));
	enc_cfg.bitrate_bps = int(p_config.get("bitrate_bps", 4000000));

	preview_width_ = MAX(enc_cfg.width / 2, 160);
	preview_height_ = MAX(enc_cfg.height / 2, 90);
	// Onceki capture'dan farkli boyutta kalmis olabilecek dokuyu birak ki
	// get_preview_texture() yeni boyutla yeniden olustursun.
	preview_texture_.unref();

	std::string err;

	encoder_ = std::make_unique<arstream::H264EncoderAndroid>();
	auto chunk_cb = [this](const uint8_t *data, size_t size, int64_t ts, bool key) {
		on_encoded_chunk(data, size, ts, key);
	};
	if (!encoder_->start(enc_cfg, chunk_cb, err)) {
		emit_signal("capture_error", String("Encoder baslatilamadi: ") + String(err.c_str()));
		encoder_.reset();
		return;
	}

	if (mode == "save") {
		sink_ = std::make_unique<arstream::FileSink>();
	} else {
		sink_ = std::make_unique<arstream::StreamSink>();
	}

	std::string dest = std::string(output_path.utf8().get_data());
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
		emit_signal("capture_error", String("Kamera baslatilamadi: ") + String(err.c_str()));
		sink_->close();
		sink_.reset();
		encoder_->stop();
		encoder_.reset();
		capture_session_.reset();
		return;
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

	emit_signal("capture_stopped", "durduruldu");
}

#else // !ANDROID_ENABLED

void ArCapture::start_capture(const Dictionary &p_config) {
	(void)p_config;
	emit_signal("capture_error", "start_capture yalniz Android'de destekleniyor.");
}

void ArCapture::stop_capture() {
}

void ArCapture::set_preview_enabled(bool p_enabled) {
	(void)p_enabled;
}

#endif
