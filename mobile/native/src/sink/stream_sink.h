#pragma once

#include "output_sink.h"

namespace arstream {

// M4/M5'te docs/PROTOCOL.md'deki kablo protokolunu konusacak. Su an bilerek
// bir kuk (stub): "stream" modu secilebilir ama open() acikca basarisiz olur --
// sessizce hicbir sey yapmiyormus gibi davranmak yerine, henuz var olmayan bir
// ozelligi var gibi gostermemek icin.
class StreamSink : public OutputSink {
public:
	bool open(const std::string &destination, std::string &out_error) override;
	void write_chunk(const uint8_t *data, size_t size, int64_t timestamp_ns, bool is_keyframe) override;
	void close() override;
};

} // namespace arstream
