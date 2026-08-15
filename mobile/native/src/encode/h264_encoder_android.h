#pragma once

#include <android/native_window.h>
#include <media/NdkMediaCodec.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

namespace arstream {

// Wrapper around the NDK AMediaCodec. Its input is an ANativeWindow
// (Surface) -- Camera2CaptureSession writes frames directly to this
// surface, no CPU-side YUV copy/format conversion (hardware-accelerated,
// zero-copy). Output is Annex-B NAL units; AMediaCodec's video/avc encoders
// produce this natively, no extra conversion needed.
class H264EncoderAndroid {
public:
	struct Config {
		int32_t width = 1280;
		int32_t height = 720;
		int32_t fps = 30;
		int32_t bitrate_bps = 4000000;
	};

	using ChunkCallback = std::function<void(const uint8_t *data, size_t size, int64_t timestamp_ns, bool is_keyframe)>;
	// SEPARATE callback for SPS/PPS (AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) --
	// unlike ChunkCallback, this just carries a body, there's no keyframe concept here.
	using ConfigCallback = std::function<void(const uint8_t *sps_pps_annexb, size_t size)>;

	~H264EncoderAndroid();

	// On success, get_input_surface() returns a valid surface;
	// Camera2CaptureSession uses it as its capture target.
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
