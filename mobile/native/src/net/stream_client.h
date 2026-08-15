#pragma once

#include <cstdint>
#include <string>

namespace arstream {

// A thin TCP socket wrapper -- does NOT carry reconnect/buffering logic,
// that's StreamSink's job (see stream_sink.h). This is just "connected or
// not, send data, close".
class StreamClient {
public:
	~StreamClient();

	bool connect_to(const std::string &host, uint16_t port, int timeout_ms, std::string &out_error);
	// Completes partial sends internally; returns false on a socket error
	// (StreamSink interprets this as "connection dropped" and retries).
	bool send_all(const uint8_t *data, size_t size);
	void disconnect();
	bool is_connected() const { return connected_; }

private:
	int socket_fd_ = -1;
	bool connected_ = false;
};

} // namespace arstream
