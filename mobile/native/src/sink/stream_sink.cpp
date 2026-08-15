#include "stream_sink.h"

#include "../net/protocol.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace arstream {

StreamSink::StreamSink(std::string spool_path, size_t max_memory_bytes) :
		spool_path_(std::move(spool_path)), max_memory_bytes_(max_memory_bytes) {
}

StreamSink::~StreamSink() {
	close();
}

bool StreamSink::open(const std::string &destination, std::string &out_error) {
	size_t colon = destination.find_last_of(':');
	if (colon == std::string::npos || colon == 0 || colon == destination.size() - 1) {
		out_error = "Gecersiz stream hedefi (\"host:port\" bekleniyor): " + destination;
		return false;
	}
	host_ = destination.substr(0, colon);
	// std::stoi degil -- Godot/Android derlemesinde istisnalar kapali.
	std::string port_str = destination.substr(colon + 1);
	char *end = nullptr;
	long port_val = std::strtol(port_str.c_str(), &end, 10);
	if (end == port_str.c_str() || *end != '\0' || port_val <= 0 || port_val > 65535) {
		out_error = "Gecersiz port: " + destination;
		return false;
	}
	port_ = static_cast<uint16_t>(port_val);

	// Onceki oturumdan kalmis olabilecek spool dosyasini temizle -- bu
	// oturumun tasma gecmisi bastan baslar (capture'lar arasi kalicilik
	// hedeflenmiyor, bkz. docs/ROADMAP.md).
	std::remove(spool_path_.c_str());
	overflowing_ = false;
	memory_bytes_ = 0;
	memory_queue_.clear();
	spool_read_pos_ = 0;
	spool_write_pos_ = 0;
	video_config_seq_ = 1;
	video_chunk_seq_ = 1;

	running_ = true;
	sender_thread_ = std::thread(&StreamSink::sender_loop, this);
	// Baglanti async kurulur -- sunucu su an ayakta olmasa bile open() basarili
	// doner, veri kuyrukta birikir (bkz. sinif basi aciklamasi).
	return true;
}

void StreamSink::write_video_config(const uint8_t *sps_pps_annexb, size_t size, int32_t rotation_degrees) {
	std::vector<uint8_t> sps_pps(sps_pps_annexb, sps_pps_annexb + size);
	// TODO(M5+): gercek codec/genislik/yukseklik/fps burada ArCapture'dan
	// gelmeli. rotation, ACAMERA_SENSOR_ORIENTATION'dan geliyor (bkz.
	// output_sink.h notu) -- alici kareyi bu kadar saat yonunde dondurmeli.
	std::string json = "{\"codec\":\"h264\",\"rotation\":" + std::to_string(rotation_degrees) + "}";
	auto message = protocol::encode_video_config(video_config_seq_++, json, sps_pps);
	// FIFO kuyruguna girmiyor -- sender_loop her (yeniden) baglantida bunu
	// en basta ayrica gonderir (bkz. header'daki not).
	{
		std::lock_guard<std::mutex> lock(mutex_);
		cached_video_config_ = std::move(message);
	}
	cv_.notify_one();
}

void StreamSink::write_chunk(const uint8_t *data, size_t size, int64_t timestamp_ns, bool is_keyframe) {
	enqueue(protocol::encode_video_chunk(video_chunk_seq_++, timestamp_ns, is_keyframe, data, size));
}

void StreamSink::enqueue(std::vector<uint8_t> message) {
	std::lock_guard<std::mutex> lock(mutex_);
	if (!overflowing_ && memory_bytes_ + message.size() <= max_memory_bytes_) {
		memory_bytes_ += message.size();
		memory_queue_.push_back(std::move(message));
	} else {
		// Bellek dolu (ya da zaten tasma modundayiz) -- sirayi bozmamak icin
		// yeni veri de diske gider, kuyruk tamamen bosalana kadar.
		overflowing_ = true;
		if (spool_write_file_ == nullptr) {
			spool_write_file_ = fopen(spool_path_.c_str(), "wb");
			spool_read_file_ = fopen(spool_path_.c_str(), "rb");
			spool_read_pos_ = 0;
			spool_write_pos_ = 0;
		}
		if (spool_write_file_ != nullptr) {
			fwrite(message.data(), 1, message.size(), spool_write_file_);
			fflush(spool_write_file_);
			spool_write_pos_ += static_cast<int64_t>(message.size());
		}
	}
	cv_.notify_one();
}

void StreamSink::requeue_front(std::vector<uint8_t> message) {
	std::lock_guard<std::mutex> lock(mutex_);
	memory_bytes_ += message.size();
	memory_queue_.push_front(std::move(message));
	cv_.notify_one();
}

bool StreamSink::pop_next_to_send(std::vector<uint8_t> &out) {
	std::lock_guard<std::mutex> lock(mutex_);
	if (!memory_queue_.empty()) {
		out = std::move(memory_queue_.front());
		memory_queue_.pop_front();
		memory_bytes_ -= out.size();
		return true;
	}
	if (overflowing_ && spool_read_file_ != nullptr && spool_read_pos_ < spool_write_pos_) {
		uint8_t header_buf[protocol::kHeaderSize];
		if (fread(header_buf, 1, protocol::kHeaderSize, spool_read_file_) != protocol::kHeaderSize) {
			return false; // yazan taraf henuz tamamlamamis olabilir, sonra tekrar denenir
		}
		protocol::Header h;
		if (!protocol::decode_header(header_buf, protocol::kHeaderSize, h)) {
			return false;
		}
		out.resize(protocol::kHeaderSize + h.payload_length);
		memcpy(out.data(), header_buf, protocol::kHeaderSize);
		if (h.payload_length > 0) {
			fread(out.data() + protocol::kHeaderSize, 1, h.payload_length, spool_read_file_);
		}
		spool_read_pos_ += static_cast<int64_t>(out.size());
		if (spool_read_pos_ >= spool_write_pos_) {
			fclose(spool_read_file_);
			spool_read_file_ = nullptr;
			fclose(spool_write_file_);
			spool_write_file_ = nullptr;
			std::remove(spool_path_.c_str());
			spool_read_pos_ = 0;
			spool_write_pos_ = 0;
			overflowing_ = false;
		}
		return true;
	}
	return false;
}

bool StreamSink::has_queued_data() const {
	std::lock_guard<std::mutex> lock(mutex_);
	return !memory_queue_.empty() || overflowing_;
}

int64_t StreamSink::queued_bytes() const {
	std::lock_guard<std::mutex> lock(mutex_);
	int64_t disk_backlog = overflowing_ ? (spool_write_pos_ - spool_read_pos_) : 0;
	return static_cast<int64_t>(memory_bytes_) + disk_backlog;
}

void StreamSink::sender_loop() {
	StreamClient client;
	int backoff_ms = 500;
	constexpr int kMaxBackoffMs = 8000;
	// Bu BAGLANTIYA video config gonderildi mi? Her yeni baglantida false'a
	// doner. Encoder'in ilk SPS/PPS'i baglanmadan SONRA gelmis olabilir --
	// bu yuzden tek seferlik "connect anindaki cache" yeterli degil, her
	// tur kontrol edilir (bkz. asagisi).
	bool config_sent_to_current_connection = false;

	while (true) {
		bool shutting_down = !running_;
		if (shutting_down) {
			if (!has_queued_data()) {
				break; // her sey gonderildi
			}
			if (std::chrono::steady_clock::now() >= shutdown_deadline_) {
				break; // sure doldu -- elden gelen bu kadar, kalan kabul edilen kayip
			}
		}

		if (!client.is_connected()) {
			if (shutting_down) {
				break; // kapaniyoruz ve bagli degiliz -- elden gelen kalmadi
			}
			std::string err;
			if (client.connect_to(host_, port_, /*timeout_ms=*/3000, err)) {
				connected_ = true;
				backoff_ms = 500;
				config_sent_to_current_connection = false;
			} else {
				connected_ = false;
				std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
				backoff_ms = std::min(backoff_ms * 2, kMaxBackoffMs);
				continue;
			}
		}

		// docs/PROTOCOL.md: VIDEO_CONFIG her (yeniden) baglantida en basta
		// gonderilir. Encoder'in SPS/PPS'i baglanti anindan sonra gelmis
		// olabilir (yarisi durumu) -- bu yuzden her turda tekrar kontrol
		// edilir, yalniz "connect basarili oldu" anina bagli kalinmaz.
		if (!config_sent_to_current_connection) {
			std::vector<uint8_t> config_copy;
			{
				std::lock_guard<std::mutex> lock(mutex_);
				config_copy = cached_video_config_;
			}
			if (!config_copy.empty()) {
				if (!client.send_all(config_copy.data(), config_copy.size())) {
					connected_ = false;
					client.disconnect();
					continue;
				}
				config_sent_to_current_connection = true;
			}
		}

		std::vector<uint8_t> message;
		if (pop_next_to_send(message)) {
			if (!client.send_all(message.data(), message.size())) {
				// Baglanti koptu -- mesaji GERI kuyruga koy (kayip olmasin),
				// yeniden baglanmayi dene.
				connected_ = false;
				client.disconnect();
				requeue_front(std::move(message));
				continue;
			}
		} else {
			if (shutting_down) {
				break;
			}
			std::unique_lock<std::mutex> lock(mutex_);
			cv_.wait_for(lock, std::chrono::milliseconds(500));
		}
	}
	client.disconnect();
	connected_ = false;
}

void StreamSink::close() {
	constexpr int kShutdownDrainMs = 5000;
	shutdown_deadline_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(kShutdownDrainMs);
	running_ = false;
	cv_.notify_all();
	if (sender_thread_.joinable()) {
		sender_thread_.join();
	}
}

} // namespace arstream
