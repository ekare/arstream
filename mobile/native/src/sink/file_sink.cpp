#include "file_sink.h"

namespace arstream {

FileSink::~FileSink() {
	close();
}

bool FileSink::open(const std::string &destination, std::string &out_error) {
	file_ = fopen(destination.c_str(), "wb");
	if (file_ == nullptr) {
		out_error = "Could not open file: " + destination;
		return false;
	}
	bytes_written_ = 0;
	return true;
}

void FileSink::write_video_config(const uint8_t *sps_pps_annexb, size_t size, int32_t rotation_degrees) {
	// In the Annex-B stream, SPS/PPS arrives in order just like frames --
	// no separate body is needed, writing it to the same file in the same
	// order is enough. rotation_degrees is unused here -- raw Annex-B has
	// no metadata field (see the note in output_sink.h); anyone working
	// with the file has to know by hand that the recording needs rotating by SENSOR_ORIENTATION.
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
