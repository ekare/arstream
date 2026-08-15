#pragma once

#include <android/native_window.h>
#include <media/NdkMediaCodec.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

namespace arstream {

// NDK AMediaCodec sarmalayicisi. Girisi bir ANativeWindow (Surface) --
// Camera2CaptureSession dogrudan bu surface'e kare yazar, CPU tarafinda
// YUV kopyalama/format donusumu yok (donanim hizlandirmali, sifir-kopya).
// Cikis Annex-B NAL birimleri; AMediaCodec'in video/avc encoder'lari bunu
// dogal olarak uretir, ekstra donusum gerekmez.
class H264EncoderAndroid {
public:
	struct Config {
		int32_t width = 1280;
		int32_t height = 720;
		int32_t fps = 30;
		int32_t bitrate_bps = 4000000;
	};

	using ChunkCallback = std::function<void(const uint8_t *data, size_t size, int64_t timestamp_ns, bool is_keyframe)>;
	// SPS/PPS (AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) icin AYRI callback --
	// ChunkCallback'ten farkli olarak sirf govde tasir, keyframe kavrami yok.
	using ConfigCallback = std::function<void(const uint8_t *sps_pps_annexb, size_t size)>;

	~H264EncoderAndroid();

	// Basarili olursa get_input_surface() dolu doner; Camera2CaptureSession
	// bunu capture hedefi olarak kullanir.
	bool start(const Config &cfg, ConfigCallback on_config, ChunkCallback on_chunk, std::string &out_error);
	void stop();

	ANativeWindow *get_input_surface() const { return input_surface_; }

private:
	AMediaCodec *codec_ = nullptr;
	ANativeWindow *input_surface_ = nullptr;
	std::thread drain_thread_;
	std::atomic<bool> running_{ false };

	void drain_loop(ConfigCallback on_config, ChunkCallback on_chunk);
};

} // namespace arstream
