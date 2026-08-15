#pragma once

// arstream wire protocol v1 -- a faithful C++ mirror of docs/PROTOCOL.md.
//
// DELIBERATELY HAS NO GODOT-CPP DEPENDENCY: this file is compiled both
// inside the GDExtension (Android/Windows) and in a standalone host test
// executable (mobile/native/tests/) -- it builds without SCons/godot-cpp,
// without a device/emulator. stdlib only.
//
// Scope note: for JSON-bodied messages (HELLO, VIDEO_CONFIG's json part,
// STATUS, GOODBYE), the JSON itself is not produced/parsed here -- the
// caller supplies/receives an already-built JSON string. This is a
// deliberate boundary since there's no dependency-free JSON library in
// C++; the server/protocol.py side does full encode/decode (using Python's
// stdlib json). What's tested here is the wire FRAMING (header + payload
// boundaries) -- that's the protocol's actual "do the two independent
// implementations drift apart" risk.

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace arstream::protocol {

constexpr uint16_t kProtocolVersion1 = 0x0001;
constexpr size_t kHeaderSize = 12;

enum class MsgType : uint8_t {
	Hello = 0x01,
	HelloAck = 0x02,
	ClockSyncRequest = 0x03,
	ClockSyncResponse = 0x04,
	VideoConfig = 0x05,
	VideoChunk = 0x10,
	ImuBatch = 0x20,
	PoseSample = 0x30,
	PointCloud = 0x31,
	CameraIntrinsics = 0x32,
	Status = 0x40,
	Goodbye = 0xF0,
};

struct Header {
	uint32_t payload_length = 0;
	uint8_t msg_type = 0;
	uint8_t flags = 0;
	uint16_t protocol_version = kProtocolVersion1;
	uint32_t sequence_number = 0;
};

struct ImuSample {
	uint8_t sensor_type = 0; // 0=accelerometer, 1=gyroscope
	int64_t timestamp_ns = 0;
	float x = 0, y = 0, z = 0;
};

struct Point {
	float x = 0, y = 0, z = 0, confidence = 0;
};

// -- Encode: header + full message body together, ready-to-send bytes --

std::vector<uint8_t> encode_header(const Header &h);

std::vector<uint8_t> encode_hello(uint32_t seq, const std::string &json_body);
std::vector<uint8_t> encode_hello_ack(uint32_t seq, const std::string &json_body);
std::vector<uint8_t> encode_clock_sync_request(uint32_t seq, int64_t client_send_time_ns);
std::vector<uint8_t> encode_clock_sync_response(uint32_t seq, int64_t client_send_time_ns, int64_t server_recv_time_ns, int64_t server_send_time_ns);
std::vector<uint8_t> encode_video_config(uint32_t seq, const std::string &json_body, const std::vector<uint8_t> &sps_pps_annexb);
std::vector<uint8_t> encode_video_chunk(uint32_t seq, int64_t capture_timestamp_ns, bool is_keyframe, const uint8_t *nal_data, size_t nal_size);
std::vector<uint8_t> encode_imu_batch(uint32_t seq, const std::vector<ImuSample> &samples);
std::vector<uint8_t> encode_pose_sample(uint32_t seq, int64_t timestamp_ns, uint8_t tracking_state, float x, float y, float z, float qx, float qy, float qz, float qw);
std::vector<uint8_t> encode_point_cloud(uint32_t seq, int64_t timestamp_ns, const std::vector<Point> &points);
std::vector<uint8_t> encode_camera_intrinsics(uint32_t seq, float fx, float fy, float cx, float cy, uint32_t width, uint32_t height);
std::vector<uint8_t> encode_status(uint32_t seq, const std::string &json_body);
std::vector<uint8_t> encode_goodbye(uint32_t seq, const std::string &json_body);

// -- Decode --

// Decodes the header. Returns false if `size` is less than 12 bytes.
bool decode_header(const uint8_t *data, size_t size, Header &out);

// For safely skipping unknown msg_types and moving on to the next message:
// reads the header, returns a pointer+length to the payload, and writes the
// total number of bytes consumed. Returns false if `buffer` doesn't contain
// a complete message yet (the caller should wait for more bytes).
struct MessageView {
	Header header;
	const uint8_t *payload = nullptr;
	size_t payload_size = 0;
};
bool next_message(const uint8_t *buffer, size_t size, MessageView &out, size_t &bytes_consumed);

// Direct decoders for fixed-layout bodies -- called with the payload
// pointer/size returned by next_message()'s MessageView.
bool decode_clock_sync_request(const uint8_t *payload, size_t size, int64_t &out_client_send_time_ns);
bool decode_clock_sync_response(const uint8_t *payload, size_t size, int64_t &out_client_send_time_ns, int64_t &out_server_recv_time_ns, int64_t &out_server_send_time_ns);
bool decode_imu_batch(const uint8_t *payload, size_t size, std::vector<ImuSample> &out_samples);
bool decode_pose_sample(const uint8_t *payload, size_t size, int64_t &out_timestamp_ns, uint8_t &out_tracking_state, float &x, float &y, float &z, float &qx, float &qy, float &qz, float &qw);
bool decode_point_cloud(const uint8_t *payload, size_t size, int64_t &out_timestamp_ns, std::vector<Point> &out_points);
bool decode_camera_intrinsics(const uint8_t *payload, size_t size, float &fx, float &fy, float &cx, float &cy, uint32_t &width, uint32_t &height);

} // namespace arstream::protocol
