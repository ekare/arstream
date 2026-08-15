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

// GDScript'e acilan tek yuzey: ArCapture. Sicak veri yolu (yakalama/encode/
// dosyaya-yazma) burada, native tarafta kalir -- bkz. docs/ARCHITECTURE.md
// "Mimari karar" bolumu. M2/M3: Camera2 geri-dusus + AMediaCodec H.264 encode +
// "save" (dosya) / "stream" (kuk, M4/M5) secilebilir cikis + kayittan bagimsiz
// acilip kapanabilen (async) onizleme.
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

	// M1 duman testi: GDScript -> native -> sinyal round-trip'i kanitlar.
	void ping(const String &p_message);

	// M2/M3: yakalama + encode + kayit/stream.
	// config: {mode: "save"|"stream", output_path: String, width/height/fps/bitrate_bps: int}
	void start_capture(const Dictionary &p_config);
	void stop_capture();

	// Kayittan bagimsiz (async) onizleme -- GDScript once get_preview_texture()
	// ile bos bir dokuyu TextureRect'e atar, sonra istedigi an ac/kapat.
	Ref<ImageTexture> get_preview_texture();
	void set_preview_enabled(bool p_enabled);
	// call_deferred hedefi -- ana thread disinda cagirilmamali.
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

	void on_encoded_chunk(const uint8_t *data, size_t size, int64_t timestamp_ns, bool is_keyframe);
	void on_capture_error(const std::string &message);
	void on_preview_frame(const uint8_t *y_plane, int32_t width, int32_t height, int32_t row_stride);
#endif
};

} // namespace godot
