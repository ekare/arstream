#include "stream_sink.h"

namespace arstream {

bool StreamSink::open(const std::string &destination, std::string &out_error) {
	(void)destination;
	out_error = "stream modu henuz uygulanmadi (bkz. docs/ROADMAP.md M4/M5) -- simdilik 'save' kullanin.";
	return false;
}

void StreamSink::write_chunk(const uint8_t *data, size_t size, int64_t timestamp_ns, bool is_keyframe) {
	(void)data;
	(void)size;
	(void)timestamp_ns;
	(void)is_keyframe;
}

void StreamSink::close() {
}

} // namespace arstream
