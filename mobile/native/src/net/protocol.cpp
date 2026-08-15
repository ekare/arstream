#include "protocol.h"

#include <cstring>

namespace arstream::protocol {

namespace {

// Big-endian yazma/okuma yardimcilari -- OS soket basliklari (htonl vb.)
// KASITLI kullanilmiyor, boylece bu dosya host'ta soket kutuphanesi
// baglamadan da derlenir.

void put_u8(std::vector<uint8_t> &buf, uint8_t v) {
	buf.push_back(v);
}

void put_u16be(std::vector<uint8_t> &buf, uint16_t v) {
	buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
	buf.push_back(static_cast<uint8_t>(v & 0xFF));
}

void put_u32be(std::vector<uint8_t> &buf, uint32_t v) {
	buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
	buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
	buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
	buf.push_back(static_cast<uint8_t>(v & 0xFF));
}

void put_i64be(std::vector<uint8_t> &buf, int64_t v) {
	uint64_t u = static_cast<uint64_t>(v);
	for (int i = 7; i >= 0; --i) {
		buf.push_back(static_cast<uint8_t>((u >> (i * 8)) & 0xFF));
	}
}

void put_f32be(std::vector<uint8_t> &buf, float v) {
	uint32_t bits;
	memcpy(&bits, &v, sizeof(bits));
	put_u32be(buf, bits);
}

void put_bytes(std::vector<uint8_t> &buf, const uint8_t *data, size_t size) {
	buf.insert(buf.end(), data, data + size);
}

void put_bytes(std::vector<uint8_t> &buf, const std::string &s) {
	buf.insert(buf.end(), s.begin(), s.end());
}

bool get_u16be(const uint8_t *p, size_t size, size_t offset, uint16_t &out) {
	if (offset + 2 > size) {
		return false;
	}
	out = (static_cast<uint16_t>(p[offset]) << 8) | p[offset + 1];
	return true;
}

bool get_u32be(const uint8_t *p, size_t size, size_t offset, uint32_t &out) {
	if (offset + 4 > size) {
		return false;
	}
	out = (static_cast<uint32_t>(p[offset]) << 24) | (static_cast<uint32_t>(p[offset + 1]) << 16) | (static_cast<uint32_t>(p[offset + 2]) << 8) | p[offset + 3];
	return true;
}

bool get_i64be(const uint8_t *p, size_t size, size_t offset, int64_t &out) {
	if (offset + 8 > size) {
		return false;
	}
	uint64_t u = 0;
	for (int i = 0; i < 8; i++) {
		u = (u << 8) | p[offset + i];
	}
	out = static_cast<int64_t>(u);
	return true;
}

bool get_f32be(const uint8_t *p, size_t size, size_t offset, float &out) {
	uint32_t bits;
	if (!get_u32be(p, size, offset, bits)) {
		return false;
	}
	memcpy(&out, &bits, sizeof(out));
	return true;
}

// Header'i yazip ardindan payload'i ekleyen ortak govde.
std::vector<uint8_t> wrap(MsgType type, uint32_t seq, const std::vector<uint8_t> &payload) {
	Header h;
	h.payload_length = static_cast<uint32_t>(payload.size());
	h.msg_type = static_cast<uint8_t>(type);
	h.flags = 0;
	h.protocol_version = kProtocolVersion1;
	h.sequence_number = seq;

	std::vector<uint8_t> out = encode_header(h);
	out.insert(out.end(), payload.begin(), payload.end());
	return out;
}

} // namespace

std::vector<uint8_t> encode_header(const Header &h) {
	std::vector<uint8_t> buf;
	buf.reserve(kHeaderSize);
	put_u32be(buf, h.payload_length);
	put_u8(buf, h.msg_type);
	put_u8(buf, h.flags);
	put_u16be(buf, h.protocol_version);
	put_u32be(buf, h.sequence_number);
	return buf;
}

bool decode_header(const uint8_t *data, size_t size, Header &out) {
	if (size < kHeaderSize) {
		return false;
	}
	uint32_t payload_length, sequence_number;
	uint16_t protocol_version;
	if (!get_u32be(data, size, 0, payload_length)) {
		return false;
	}
	out.payload_length = payload_length;
	out.msg_type = data[4];
	out.flags = data[5];
	if (!get_u16be(data, size, 6, protocol_version)) {
		return false;
	}
	out.protocol_version = protocol_version;
	if (!get_u32be(data, size, 8, sequence_number)) {
		return false;
	}
	out.sequence_number = sequence_number;
	return true;
}

bool next_message(const uint8_t *buffer, size_t size, MessageView &out, size_t &bytes_consumed) {
	Header h;
	if (!decode_header(buffer, size, h)) {
		return false;
	}
	size_t total = kHeaderSize + h.payload_length;
	if (size < total) {
		return false; // henuz tam mesaj gelmemis
	}
	out.header = h;
	out.payload = buffer + kHeaderSize;
	out.payload_size = h.payload_length;
	bytes_consumed = total;
	return true;
}

std::vector<uint8_t> encode_hello(uint32_t seq, const std::string &json_body) {
	std::vector<uint8_t> payload;
	put_bytes(payload, json_body);
	return wrap(MsgType::Hello, seq, payload);
}

std::vector<uint8_t> encode_hello_ack(uint32_t seq, const std::string &json_body) {
	std::vector<uint8_t> payload;
	put_bytes(payload, json_body);
	return wrap(MsgType::HelloAck, seq, payload);
}

std::vector<uint8_t> encode_clock_sync_request(uint32_t seq, int64_t client_send_time_ns) {
	std::vector<uint8_t> payload;
	put_i64be(payload, client_send_time_ns);
	return wrap(MsgType::ClockSyncRequest, seq, payload);
}

std::vector<uint8_t> encode_clock_sync_response(uint32_t seq, int64_t client_send_time_ns, int64_t server_recv_time_ns, int64_t server_send_time_ns) {
	std::vector<uint8_t> payload;
	put_i64be(payload, client_send_time_ns);
	put_i64be(payload, server_recv_time_ns);
	put_i64be(payload, server_send_time_ns);
	return wrap(MsgType::ClockSyncResponse, seq, payload);
}

std::vector<uint8_t> encode_video_config(uint32_t seq, const std::string &json_body, const std::vector<uint8_t> &sps_pps_annexb) {
	std::vector<uint8_t> payload;
	put_u16be(payload, static_cast<uint16_t>(json_body.size()));
	put_bytes(payload, json_body);
	put_bytes(payload, sps_pps_annexb.data(), sps_pps_annexb.size());
	return wrap(MsgType::VideoConfig, seq, payload);
}

std::vector<uint8_t> encode_video_chunk(uint32_t seq, int64_t capture_timestamp_ns, bool is_keyframe, const uint8_t *nal_data, size_t nal_size) {
	std::vector<uint8_t> payload;
	put_i64be(payload, capture_timestamp_ns);
	put_u8(payload, is_keyframe ? 0x01 : 0x00);
	put_bytes(payload, nal_data, nal_size);
	return wrap(MsgType::VideoChunk, seq, payload);
}

std::vector<uint8_t> encode_imu_batch(uint32_t seq, const std::vector<ImuSample> &samples) {
	std::vector<uint8_t> payload;
	put_u16be(payload, static_cast<uint16_t>(samples.size()));
	for (const ImuSample &s : samples) {
		put_u8(payload, s.sensor_type);
		put_i64be(payload, s.timestamp_ns);
		put_f32be(payload, s.x);
		put_f32be(payload, s.y);
		put_f32be(payload, s.z);
	}
	return wrap(MsgType::ImuBatch, seq, payload);
}

std::vector<uint8_t> encode_pose_sample(uint32_t seq, int64_t timestamp_ns, uint8_t tracking_state, float x, float y, float z, float qx, float qy, float qz, float qw) {
	std::vector<uint8_t> payload;
	put_i64be(payload, timestamp_ns);
	put_u8(payload, tracking_state);
	put_f32be(payload, x);
	put_f32be(payload, y);
	put_f32be(payload, z);
	put_f32be(payload, qx);
	put_f32be(payload, qy);
	put_f32be(payload, qz);
	put_f32be(payload, qw);
	return wrap(MsgType::PoseSample, seq, payload);
}

std::vector<uint8_t> encode_point_cloud(uint32_t seq, int64_t timestamp_ns, const std::vector<Point> &points) {
	std::vector<uint8_t> payload;
	put_i64be(payload, timestamp_ns);
	put_u32be(payload, static_cast<uint32_t>(points.size()));
	for (const Point &pt : points) {
		put_f32be(payload, pt.x);
		put_f32be(payload, pt.y);
		put_f32be(payload, pt.z);
		put_f32be(payload, pt.confidence);
	}
	return wrap(MsgType::PointCloud, seq, payload);
}

std::vector<uint8_t> encode_camera_intrinsics(uint32_t seq, float fx, float fy, float cx, float cy, uint32_t width, uint32_t height) {
	std::vector<uint8_t> payload;
	put_f32be(payload, fx);
	put_f32be(payload, fy);
	put_f32be(payload, cx);
	put_f32be(payload, cy);
	put_u32be(payload, width);
	put_u32be(payload, height);
	return wrap(MsgType::CameraIntrinsics, seq, payload);
}

std::vector<uint8_t> encode_status(uint32_t seq, const std::string &json_body) {
	std::vector<uint8_t> payload;
	put_bytes(payload, json_body);
	return wrap(MsgType::Status, seq, payload);
}

std::vector<uint8_t> encode_goodbye(uint32_t seq, const std::string &json_body) {
	std::vector<uint8_t> payload;
	put_bytes(payload, json_body);
	return wrap(MsgType::Goodbye, seq, payload);
}

bool decode_clock_sync_request(const uint8_t *payload, size_t size, int64_t &out_client_send_time_ns) {
	return get_i64be(payload, size, 0, out_client_send_time_ns);
}

bool decode_clock_sync_response(const uint8_t *payload, size_t size, int64_t &out_client_send_time_ns, int64_t &out_server_recv_time_ns, int64_t &out_server_send_time_ns) {
	return get_i64be(payload, size, 0, out_client_send_time_ns) &&
			get_i64be(payload, size, 8, out_server_recv_time_ns) &&
			get_i64be(payload, size, 16, out_server_send_time_ns);
}

bool decode_imu_batch(const uint8_t *payload, size_t size, std::vector<ImuSample> &out_samples) {
	uint16_t count;
	if (!get_u16be(payload, size, 0, count)) {
		return false;
	}
	size_t offset = 2;
	out_samples.clear();
	out_samples.reserve(count);
	for (uint16_t i = 0; i < count; i++) {
		if (offset + 21 > size) {
			return false;
		}
		ImuSample s;
		s.sensor_type = payload[offset];
		if (!get_i64be(payload, size, offset + 1, s.timestamp_ns)) {
			return false;
		}
		if (!get_f32be(payload, size, offset + 9, s.x)) {
			return false;
		}
		if (!get_f32be(payload, size, offset + 13, s.y)) {
			return false;
		}
		if (!get_f32be(payload, size, offset + 17, s.z)) {
			return false;
		}
		out_samples.push_back(s);
		offset += 21;
	}
	return true;
}

bool decode_pose_sample(const uint8_t *payload, size_t size, int64_t &out_timestamp_ns, uint8_t &out_tracking_state, float &x, float &y, float &z, float &qx, float &qy, float &qz, float &qw) {
	if (size < 37) {
		return false;
	}
	if (!get_i64be(payload, size, 0, out_timestamp_ns)) {
		return false;
	}
	out_tracking_state = payload[8];
	return get_f32be(payload, size, 9, x) &&
			get_f32be(payload, size, 13, y) &&
			get_f32be(payload, size, 17, z) &&
			get_f32be(payload, size, 21, qx) &&
			get_f32be(payload, size, 25, qy) &&
			get_f32be(payload, size, 29, qz) &&
			get_f32be(payload, size, 33, qw);
}

bool decode_point_cloud(const uint8_t *payload, size_t size, int64_t &out_timestamp_ns, std::vector<Point> &out_points) {
	uint32_t count;
	if (!get_i64be(payload, size, 0, out_timestamp_ns)) {
		return false;
	}
	if (!get_u32be(payload, size, 8, count)) {
		return false;
	}
	size_t offset = 12;
	out_points.clear();
	out_points.reserve(count);
	for (uint32_t i = 0; i < count; i++) {
		if (offset + 16 > size) {
			return false;
		}
		Point p;
		if (!get_f32be(payload, size, offset, p.x) ||
				!get_f32be(payload, size, offset + 4, p.y) ||
				!get_f32be(payload, size, offset + 8, p.z) ||
				!get_f32be(payload, size, offset + 12, p.confidence)) {
			return false;
		}
		out_points.push_back(p);
		offset += 16;
	}
	return true;
}

bool decode_camera_intrinsics(const uint8_t *payload, size_t size, float &fx, float &fy, float &cx, float &cy, uint32_t &width, uint32_t &height) {
	if (size < 24) {
		return false;
	}
	return get_f32be(payload, size, 0, fx) &&
			get_f32be(payload, size, 4, fy) &&
			get_f32be(payload, size, 8, cx) &&
			get_f32be(payload, size, 12, cy) &&
			get_u32be(payload, size, 16, width) &&
			get_u32be(payload, size, 20, height);
}

} // namespace arstream::protocol
