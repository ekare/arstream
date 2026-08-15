#pragma once

#include "output_sink.h"

#include <cstdio>

namespace arstream {

// Writes the Annex-B stream to a file as-is (SPS/PPS + frames in order) --
// a raw H.264 elementary stream that can be played directly with ffprobe/ffplay.
class FileSink : public OutputSink {
public:
	~FileSink() override;

	bool open(const std::string &destination, std::string &out_error) override;
	void write_video_config(const uint8_t *sps_pps_annexb, size_t size, int32_t rotation_degrees) override;
	void write_chunk(const uint8_t *data, size_t size, int64_t timestamp_ns, bool is_keyframe) override;
	void close() override;

	int64_t bytes_written() const { return bytes_written_; }

private:
	FILE *file_ = nullptr;
	int64_t bytes_written_ = 0;
};

} // namespace arstream
