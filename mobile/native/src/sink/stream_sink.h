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

// The sender that speaks docs/PROTOCOL.md, resilient to TCP drops.
//
// Rule: write_chunk()/write_video_config() (called from the encoder thread)
// NEVER touch the network, NEVER block -- they only write into a queue
// (memory, overflowing to disk if it fills up). A separate sender thread
// manages the connection (connects, retries on drop with exponential
// backoff) and drains the queue.
//
// Memory-first, disk-overflow: everything stays in memory (a deque) until
// the queue fills up. Once full, it enters "overflow mode" -- while in this
// mode, NEWLY arriving data also gets written to disk (even if memory has
// freed up room), until the disk queue is FULLY sent and both memory and
// disk drain completely. This way memory always holds the OLDEST (next to
// be sent) end of the queue, and disk holds what's behind it -- preventing
// newer data from jumping the queue as memory frees up space.
class StreamSink : public OutputSink {
public:
	// spool_path: the overflow disk file (may be left over from a previous
	// session, cleared on open). max_memory_bytes: the memory queue's cap.
	explicit StreamSink(std::string spool_path, size_t max_memory_bytes = 8 * 1024 * 1024);
	~StreamSink() override;

	bool open(const std::string &destination, std::string &out_error) override; // destination = "host:port"
	void write_video_config(const uint8_t *sps_pps_annexb, size_t size, int32_t rotation_degrees) override;
	void write_chunk(const uint8_t *data, size_t size, int64_t timestamp_ns, bool is_keyframe) override;
	void close() override;

	// Diagnostics -- could be exposed to GDScript via ArCapture later.
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
	// VIDEO_CONFIG (SPS/PPS) does not enter the FIFO queue -- as
	// docs/PROTOCOL.md requires, it's resent at the very start of EVERY
	// (re)connection, since a new server/session may never have seen the SPS/PPS.
	std::vector<uint8_t> cached_video_config_;

	std::thread sender_thread_;
	std::atomic<bool> running_{ false };
	std::atomic<bool> connected_{ false };
	// Set when close() is called -- instead of waiting UNBOUNDED for the
	// queue to drain on shutdown, this releases the calling thread (Godot's
	// main thread) after a reasonable time; if there's a large backlog, the
	// remainder is accepted as lost (see sender_loop).
	std::chrono::steady_clock::time_point shutdown_deadline_;

	void enqueue(std::vector<uint8_t> message);
	void requeue_front(std::vector<uint8_t> message);
	bool pop_next_to_send(std::vector<uint8_t> &out);
	bool has_queued_data() const;
	void sender_loop();
};

} // namespace arstream
