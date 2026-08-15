#pragma once

#include "output_sink.h"

#include <cstdio>

namespace arstream {

// Annex-B akisini oldugu gibi bir dosyaya yazar (SPS/PPS + kareler sirayla) --
// ffprobe/ffplay ile dogrudan oynatilabilir ham H.264 elementary stream.
class FileSink : public OutputSink {
public:
	~FileSink() override;

	bool open(const std::string &destination, std::string &out_error) override;
	void write_chunk(const uint8_t *data, size_t size, int64_t timestamp_ns, bool is_keyframe) override;
	void close() override;

	int64_t bytes_written() const { return bytes_written_; }

private:
	FILE *file_ = nullptr;
	int64_t bytes_written_ = 0;
};

} // namespace arstream
