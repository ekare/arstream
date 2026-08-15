#include "h264_encoder_android.h"

#include <media/NdkMediaFormat.h>

namespace arstream {

namespace {
// MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface -- NDK'da isimli sabiti
// yok, sayisal degeri sabit ve kararli.
constexpr int32_t COLOR_FORMAT_SURFACE = 0x7f000789;
constexpr int64_t DEQUEUE_TIMEOUT_US = 10000; // 10ms
constexpr uint8_t kH264NalTypeIdrSlice = 5;

// AMEDIACODEC_BUFFER_FLAG_KEY_FRAME NDK basligina gore API 34'ten once
// guvenilir degil (bkz. NdkMediaCodec.h yorumu) -- A30s API 30, o yuzden bu
// bayraga guvenmek yerine Annex-B NAL tipini (5=IDR) dogrudan kendimiz
// okuyoruz. Platform/API'den bagimsiz, her zaman dogru.
bool starts_with_idr_nal(const uint8_t *data, size_t size) {
	size_t start = 0;
	if (size >= 4 && data[0] == 0 && data[1] == 0 && data[2] == 0 && data[3] == 1) {
		start = 4;
	} else if (size >= 3 && data[0] == 0 && data[1] == 0 && data[2] == 1) {
		start = 3;
	} else {
		return false;
	}
	if (start >= size) {
		return false;
	}
	return (data[start] & 0x1F) == kH264NalTypeIdrSlice;
}
} // namespace

H264EncoderAndroid::~H264EncoderAndroid() {
	stop();
}

bool H264EncoderAndroid::start(const Config &cfg, ConfigCallback on_config, ChunkCallback on_chunk, std::string &out_error) {
	codec_ = AMediaCodec_createEncoderByType("video/avc");
	if (codec_ == nullptr) {
		out_error = "video/avc encoder olusturulamadi";
		return false;
	}

	AMediaFormat *format = AMediaFormat_new();
	AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, "video/avc");
	AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, cfg.width);
	AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, cfg.height);
	AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_COLOR_FORMAT, COLOR_FORMAT_SURFACE);
	AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_BIT_RATE, cfg.bitrate_bps);
	AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_FRAME_RATE, cfg.fps);
	AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_I_FRAME_INTERVAL, 2);

	media_status_t cfg_status = AMediaCodec_configure(codec_, format, nullptr, nullptr, AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
	AMediaFormat_delete(format);
	if (cfg_status != AMEDIA_OK) {
		out_error = "AMediaCodec_configure basarisiz (kod " + std::to_string(cfg_status) + ")";
		AMediaCodec_delete(codec_);
		codec_ = nullptr;
		return false;
	}

	media_status_t surface_status = AMediaCodec_createInputSurface(codec_, &input_surface_);
	if (surface_status != AMEDIA_OK || input_surface_ == nullptr) {
		out_error = "Encoder giris surface'i olusturulamadi (kod " + std::to_string(surface_status) + ")";
		AMediaCodec_delete(codec_);
		codec_ = nullptr;
		return false;
	}

	if (AMediaCodec_start(codec_) != AMEDIA_OK) {
		out_error = "AMediaCodec baslatilamadi";
		ANativeWindow_release(input_surface_);
		input_surface_ = nullptr;
		AMediaCodec_delete(codec_);
		codec_ = nullptr;
		return false;
	}

	running_ = true;
	drain_thread_ = std::thread(&H264EncoderAndroid::drain_loop, this, on_config, on_chunk);
	return true;
}

void H264EncoderAndroid::drain_loop(ConfigCallback on_config, ChunkCallback on_chunk) {
	while (running_) {
		AMediaCodecBufferInfo info;
		ssize_t idx = AMediaCodec_dequeueOutputBuffer(codec_, &info, DEQUEUE_TIMEOUT_US);
		if (idx >= 0) {
			size_t out_size = 0;
			uint8_t *buf = AMediaCodec_getOutputBuffer(codec_, static_cast<size_t>(idx), &out_size);
			if (buf != nullptr && info.size > 0) {
				const uint8_t *data = buf + info.offset;
				size_t size = static_cast<size_t>(info.size);
				if ((info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) != 0) {
					if (on_config) {
						on_config(data, size);
					}
				} else if (on_chunk) {
					on_chunk(data, size, info.presentationTimeUs * 1000, starts_with_idr_nal(data, size));
				}
			}
			AMediaCodec_releaseOutputBuffer(codec_, static_cast<size_t>(idx), false);
			if ((info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0) {
				break;
			}
		}
		// idx < 0: AMEDIACODEC_INFO_TRY_AGAIN_LATER / OUTPUT_FORMAT_CHANGED / OUTPUT_BUFFERS_CHANGED --
		// hepsinde sadece bir sonraki turda tekrar denenir.
	}
}

void H264EncoderAndroid::stop() {
	if (!running_) {
		return;
	}
	if (codec_ != nullptr) {
		AMediaCodec_signalEndOfInputStream(codec_);
	}
	running_ = false;
	if (drain_thread_.joinable()) {
		drain_thread_.join();
	}
	if (codec_ != nullptr) {
		AMediaCodec_stop(codec_);
		AMediaCodec_delete(codec_);
		codec_ = nullptr;
	}
	if (input_surface_ != nullptr) {
		ANativeWindow_release(input_surface_);
		input_surface_ = nullptr;
	}
}

} // namespace arstream
