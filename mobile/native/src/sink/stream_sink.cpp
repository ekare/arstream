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
		out_error = "Invalid stream destination (expected \"host:port\"): " + destination;
		return false;
	}
	host_ = destination.substr(0, colon);
	// Not std::stoi -- exceptions are disabled in the Godot/Android build.
	std::string port_str = destination.substr(colon + 1);
	char *end = nullptr;
	long port_val = std::strtol(port_str.c_str(), &end, 10);
	if (end == port_str.c_str() || *end != '\0' || port_val <= 0 || port_val > 65535) {
		out_error = "Invalid port: " + destination;
		return false;
	}
	port_ = static_cast<uint16_t>(port_val);

	// Clear any spool file left over from a previous session -- this
	// session's overflow history starts fresh (no persistence across
	// captures is intended, see docs/ROADMAP.md).
	std::remove(spool_path_.c_str());
	overflowing_ = false;
	memory_bytes_ = 0;
	memory_queue_.clear();
	spool_read_pos_ = 0;
	spool_write_pos_ = 0;
	video_config_seq_ = 1;
	video_chunk_seq_ = 1;
	imu_batch_seq_ = 1;
	pose_sample_seq_ = 1;
	point_cloud_seq_ = 1;
	camera_intrinsics_seq_ = 1;

	running_ = true;
	sender_thread_ = std::thread(&StreamSink::sender_loop, this);
	// The connection is established asynchronously -- open() returns
	// success even if the server isn't up right now, data just piles up in
	// the queue (see the class-level comment).
	return true;
}

void StreamSink::write_video_config(const uint8_t *sps_pps_annexb, size_t size, int32_t rotation_degrees) {
	std::vector<uint8_t> sps_pps(sps_pps_annexb, sps_pps_annexb + size);
	// TODO(M5+): the real codec/width/height/fps should come from ArCapture
	// here. rotation comes from ACAMERA_SENSOR_ORIENTATION (see the note in
	// output_sink.h) -- the receiver must rotate the frame clockwise by this amount.
	std::string json = "{\"codec\":\"h264\",\"rotation\":" + std::to_string(rotation_degrees) + "}";
	auto message = protocol::encode_video_config(video_config_seq_++, json, sps_pps);
	// Doesn't enter the FIFO queue -- sender_loop sends this separately, at
	// the start of every (re)connection (see the note in the header).
	{
		std::lock_guard<std::mutex> lock(mutex_);
		cached_video_config_ = std::move(message);
	}
	cv_.notify_one();
}

void StreamSink::write_chunk(const uint8_t *data, size_t size, int64_t timestamp_ns, bool is_keyframe) {
	enqueue(protocol::encode_video_chunk(video_chunk_seq_++, timestamp_ns, is_keyframe, data, size));
}

void StreamSink::write_imu_batch(const std::vector<protocol::ImuSample> &samples) {
	// Same FIFO queue as video chunks -- no separate queueing logic needed,
	// enqueue() already handles memory-first/disk-overflow generically for
	// any encoded message.
	enqueue(protocol::encode_imu_batch(imu_batch_seq_++, samples));
}

void StreamSink::write_pose_sample(int64_t timestamp_ns, uint8_t tracking_state, float x, float y, float z, float qx, float qy, float qz, float qw) {
	enqueue(protocol::encode_pose_sample(pose_sample_seq_++, timestamp_ns, tracking_state, x, y, z, qx, qy, qz, qw));
}

void StreamSink::write_point_cloud(int64_t timestamp_ns, const std::vector<protocol::Point> &points) {
	enqueue(protocol::encode_point_cloud(point_cloud_seq_++, timestamp_ns, points));
}

void StreamSink::write_camera_intrinsics(float fx, float fy, float cx, float cy, uint32_t width, uint32_t height) {
	enqueue(protocol::encode_camera_intrinsics(camera_intrinsics_seq_++, fx, fy, cx, cy, width, height));
}

void StreamSink::enqueue(std::vector<uint8_t> message) {
	std::lock_guard<std::mutex> lock(mutex_);
	if (!overflowing_ && memory_bytes_ + message.size() <= max_memory_bytes_) {
		memory_bytes_ += message.size();
		memory_queue_.push_back(std::move(message));
	} else {
		// Memory is full (or we're already in overflow mode) -- to avoid
		// breaking ordering, new data also goes to disk, until the queue drains completely.
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
			return false; // the writer side may not be done yet, will be retried later
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
	// Has video config been sent to THIS connection? Resets to false on
	// every new connection. The encoder's first SPS/PPS may arrive AFTER
	// connecting -- so a one-time "cache at connect time" isn't enough, this
	// is checked every loop iteration (see below).
	bool config_sent_to_current_connection = false;

	while (true) {
		bool shutting_down = !running_;
		if (shutting_down) {
			if (!has_queued_data()) {
				break; // everything has been sent
			}
			if (std::chrono::steady_clock::now() >= shutdown_deadline_) {
				break; // time's up -- this is as far as we can go, the rest is accepted as lost
			}
		}

		if (!client.is_connected()) {
			if (shutting_down) {
				break; // shutting down and not connected -- nothing more to do
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

		// docs/PROTOCOL.md: VIDEO_CONFIG is sent first on every
		// (re)connection. The encoder's SPS/PPS may arrive after the moment
		// of connecting (a race) -- so this is re-checked every iteration,
		// not just tied to the "connect just succeeded" moment.
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
				// Connection dropped -- put the message BACK at the front of
				// the queue (so it isn't lost), retry connecting.
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
