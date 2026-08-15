#include "file_sink.h"

namespace arstream {

FileSink::~FileSink() {
	close();
}

bool FileSink::open(const std::string &destination, std::string &out_error) {
	file_ = fopen(destination.c_str(), "wb");
	if (file_ == nullptr) {
		out_error = "Dosya acilamadi: " + destination;
		return false;
	}
	bytes_written_ = 0;
	return true;
}

void FileSink::write_video_config(const uint8_t *sps_pps_annexb, size_t size, int32_t rotation_degrees) {
	// Annex-B akisinda SPS/PPS de kareler gibi sirali gelir -- ayri bir
	// govde gerekmiyor, ayni dosyaya, ayni sirayla yazmak yeterli.
	// rotation_degrees burada kullanilmiyor -- ham Annex-B'nin metadata
	// alani yok (bkz. output_sink.h notu); dosyayla calisan biri kaydin
	// SENSOR_ORIENTATION kadar dondurulmesi gerektigini elle bilmeli.
	(void)rotation_degrees;
	if (file_ == nullptr || sps_pps_annexb == nullptr || size == 0) {
		return;
	}
	fwrite(sps_pps_annexb, 1, size, file_);
	bytes_written_ += static_cast<int64_t>(size);
}

void FileSink::write_chunk(const uint8_t *data, size_t size, int64_t timestamp_ns, bool is_keyframe) {
	(void)timestamp_ns;
	(void)is_keyframe;
	if (file_ == nullptr || data == nullptr || size == 0) {
		return;
	}
	fwrite(data, 1, size, file_);
	bytes_written_ += static_cast<int64_t>(size);
}

void FileSink::close() {
	if (file_ != nullptr) {
		fflush(file_);
		fclose(file_);
		file_ = nullptr;
	}
}

} // namespace arstream
