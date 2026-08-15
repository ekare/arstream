#pragma once

#include <cstdint>
#include <string>

namespace arstream {

// Ince bir TCP soket sarmalayicisi -- yeniden baglanma/tampon mantigi
// TASIMAZ, bu StreamSink'in isi (bkz. stream_sink.h). Burasi yalniz "bagli
// mi, veri gonder, kapat".
class StreamClient {
public:
	~StreamClient();

	bool connect_to(const std::string &host, uint16_t port, int timeout_ms, std::string &out_error);
	// Kismi gonderimi kendi icinde tamamlar; soket hatasinda false doner
	// (StreamSink bunu "baglanti koptu" olarak yorumlayip yeniden dener).
	bool send_all(const uint8_t *data, size_t size);
	void disconnect();
	bool is_connected() const { return connected_; }

private:
	int socket_fd_ = -1;
	bool connected_ = false;
};

} // namespace arstream
