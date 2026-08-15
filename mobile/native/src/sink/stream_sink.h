#pragma once

#include "output_sink.h"
#include "../net/stream_client.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace arstream {

// docs/PROTOCOL.md'yi konusan, TCP kopmalarina karsi dayanikli gonderici.
//
// Kural: write_chunk()/write_video_config() (encoder thread'inden cagirilir)
// HICBIR ZAMAN aga dokunmaz, HICBIR ZAMAN bloklamaz -- yalniz bir kuyruga
// (bellek, dolarsa disk) yazar. Ayri bir sender thread'i baglantiyi yonetir
// (baglan/kopunca yeniden dene, us tel geri-cekilme ile) ve kuyrugu bosaltir.
//
// Bellek-once, disk-tasma: kuyruk dolana kadar her sey bellekte (deque).
// Doldugunda "tasma modu"na girilir -- bu moddayken YENI gelen veri de
// (bellekte yer acilmis olsa bile) diske yazilir, ta ki disk kuyrugu
// TAMAMEN gonderilip bellek+disk bosalana kadar. Boylece bellek her zaman
// kuyrugun EN ESKI (once gonderilecek) ucunu, disk ise arkasini tutar --
// bellek yer actikca yeni verinin araya girip sirayi bozmasi engellenir.
class StreamSink : public OutputSink {
public:
	// spool_path: tasma disk dosyasi (bir onceki oturumdan kalmis olabilir,
	// acilista temizlenir). max_memory_bytes: bellek kuyrugunun tavani.
	explicit StreamSink(std::string spool_path, size_t max_memory_bytes = 8 * 1024 * 1024);
	~StreamSink() override;

	bool open(const std::string &destination, std::string &out_error) override; // destination = "host:port"
	void write_video_config(const uint8_t *sps_pps_annexb, size_t size, int32_t rotation_degrees) override;
	void write_chunk(const uint8_t *data, size_t size, int64_t timestamp_ns, bool is_keyframe) override;
	void close() override;

	// Tanilama -- ileride ArCapture uzerinden GDScript'e aktarilabilir.
	int64_t queued_bytes() const;
	bool is_connected() const { return connected_; }

private:
	std::string spool_path_;
	size_t max_memory_bytes_;

	mutable std::mutex mutex_;
	std::condition_variable cv_;
	std::deque<std::vector<uint8_t>> memory_queue_;
	size_t memory_bytes_ = 0;
	bool overflowing_ = false;
	FILE *spool_write_file_ = nullptr;
	FILE *spool_read_file_ = nullptr;
	int64_t spool_read_pos_ = 0;
	int64_t spool_write_pos_ = 0;

	std::string host_;
	uint16_t port_ = 0;
	uint32_t video_config_seq_ = 1;
	uint32_t video_chunk_seq_ = 1;
	// VIDEO_CONFIG (SPS/PPS) FIFO kuyruguna girmez -- docs/PROTOCOL.md'nin
	// istedigi gibi HER (yeniden) baglantida en basta tekrar gonderilir,
	// cunku yeni bir sunucu/oturum SPS/PPS'i hic gormemis olabilir.
	std::vector<uint8_t> cached_video_config_;

	std::thread sender_thread_;
	std::atomic<bool> running_{ false };
	std::atomic<bool> connected_{ false };
	// close() cagirildiginda dolar -- kapanista kuyruk bosalana kadar
	// SINIRSIZ beklemek yerine, cagiran thread'i (Godot ana thread'i) makul
	// bir sure sonra serbest birakir; buyuk bir birikim varsa kalan kismi
	// kayip olarak kabul edilir (bkz. sender_loop).
	std::chrono::steady_clock::time_point shutdown_deadline_;

	void enqueue(std::vector<uint8_t> message);
	void requeue_front(std::vector<uint8_t> message);
	bool pop_next_to_send(std::vector<uint8_t> &out);
	bool has_queued_data() const;
	void sender_loop();
};

} // namespace arstream
