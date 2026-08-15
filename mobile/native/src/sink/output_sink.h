#pragma once

#include <cstdint>
#include <string>

namespace arstream {

// Encode edilmis Annex-B H.264 verisinin gidecegi yer. "save" ve "stream"
// modlari bu arayuzun iki implementasyonu -- ArCapture hangisini kullanacagini
// config'teki "mode" alanina gore secer.
class OutputSink {
public:
	virtual ~OutputSink() = default;

	// destination: save modunda dosya yolu; stream modunda "host:port".
	virtual bool open(const std::string &destination, std::string &out_error) = 0;
	virtual void write_chunk(const uint8_t *data, size_t size, int64_t timestamp_ns, bool is_keyframe) = 0;
	virtual void close() = 0;
};

} // namespace arstream
