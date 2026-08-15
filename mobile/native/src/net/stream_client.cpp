#include "stream_client.h"

// This file is included in the build for ALL platforms (see the sink/net
// globs in SConstruct), because the GDExtension also needs to load on the
// editor's own platform (Windows) (see the M1 note in
// docs/ARCHITECTURE.md). The real POSIX socket code is compiled on both
// Android AND iOS (both use the same BSD sockets API, see
// docs/IOS_HANDOVER.md) -- on the others (Windows/macOS editor) it compiles
// but a body that isn't expected to actually run is left in place.
#if defined(ANDROID_ENABLED) || defined(IOS_ENABLED)

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstring>

namespace arstream {

StreamClient::~StreamClient() {
	disconnect();
}

bool StreamClient::connect_to(const std::string &host, uint16_t port, int timeout_ms, std::string &out_error) {
	disconnect();

	struct addrinfo hints {};
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	struct addrinfo *result = nullptr;
	std::string port_str = std::to_string(port);
	int gai_err = getaddrinfo(host.c_str(), port_str.c_str(), &hints, &result);
	if (gai_err != 0 || result == nullptr) {
		out_error = "Could not resolve address: " + host + " (" + gai_strerror(gai_err) + ")";
		return false;
	}

	int fd = -1;
	bool connected = false;
	for (struct addrinfo *rp = result; rp != nullptr; rp = rp->ai_next) {
		fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (fd < 0) {
			continue;
		}

		// Non-blocking connect + select for the timeout -- so a dead/
		// unreachable server can't block the sender thread indefinitely.
		int flags = fcntl(fd, F_GETFL, 0);
		fcntl(fd, F_SETFL, flags | O_NONBLOCK);

		int rc = ::connect(fd, rp->ai_addr, rp->ai_addrlen);
		if (rc == 0) {
			connected = true;
		} else if (errno == EINPROGRESS) {
			fd_set write_fds;
			FD_ZERO(&write_fds);
			FD_SET(fd, &write_fds);
			struct timeval tv;
			tv.tv_sec = timeout_ms / 1000;
			tv.tv_usec = (timeout_ms % 1000) * 1000;
			int sel = select(fd + 1, nullptr, &write_fds, nullptr, &tv);
			if (sel > 0) {
				int so_error = 0;
				socklen_t len = sizeof(so_error);
				getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &len);
				connected = (so_error == 0);
			}
		}

		fcntl(fd, F_SETFL, flags); // back to blocking mode

		if (connected) {
			break;
		}
		close(fd);
		fd = -1;
	}
	freeaddrinfo(result);

	if (!connected) {
		out_error = "Could not connect: " + host + ":" + port_str;
		return false;
	}

	int nodelay = 1;
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

	socket_fd_ = fd;
	connected_ = true;
	return true;
}

bool StreamClient::send_all(const uint8_t *data, size_t size) {
	if (!connected_ || socket_fd_ < 0) {
		return false;
	}
	size_t sent = 0;
	while (sent < size) {
		ssize_t n = ::send(socket_fd_, reinterpret_cast<const char *>(data + sent), size - sent, 0);
		if (n <= 0) {
			connected_ = false;
			return false;
		}
		sent += static_cast<size_t>(n);
	}
	return true;
}

void StreamClient::disconnect() {
	if (socket_fd_ >= 0) {
		close(socket_fd_);
		socket_fd_ = -1;
	}
	connected_ = false;
}

} // namespace arstream

#else // neither ANDROID_ENABLED nor IOS_ENABLED -- compiles on the editor platform (Windows etc.) but is never used.

namespace arstream {

StreamClient::~StreamClient() = default;

bool StreamClient::connect_to(const std::string &host, uint16_t port, int timeout_ms, std::string &out_error) {
	(void)host;
	(void)port;
	(void)timeout_ms;
	out_error = "StreamClient is only supported on Android.";
	return false;
}

bool StreamClient::send_all(const uint8_t *data, size_t size) {
	(void)data;
	(void)size;
	return false;
}

void StreamClient::disconnect() {
}

} // namespace arstream

#endif
