// Host-native tests for mobile/native/src/net/protocol.cpp -- has no
// dependency on the Godot/SCons/godot-cpp chain, no device/emulator needed.
// The same fixtures/*.bin files are also used by
// server/tests/test_protocol.py; if the two drift apart, this test breaks.
//
// Run: mobile/native/tests/protocol_test(.exe)  (from the repo root)

#include "../src/net/protocol.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

using namespace arstream::protocol;

namespace {

int g_failures = 0;
int g_checks = 0;

std::vector<uint8_t> read_fixture(const std::string &name) {
	// Assumes this is run from mobile/native/tests/; goes up 3 levels to the
	// repo root then down into fixtures/.
	std::ifstream f("../../../fixtures/" + name, std::ios::binary);
	if (!f) {
		std::cerr << "WARNING: could not open fixture: " << name << " (is the working directory the repo root?)\n";
		return {};
	}
	return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

void check(bool cond, const char *what) {
	g_checks++;
	if (!cond) {
		g_failures++;
		std::cerr << "FAIL: " << what << "\n";
	}
}

void check_bytes_equal(const std::vector<uint8_t> &actual, const std::vector<uint8_t> &expected, const char *what) {
	g_checks++;
	if (actual != expected) {
		g_failures++;
		std::cerr << "FAIL: " << what << " -- sizes: actual=" << actual.size() << " expected=" << expected.size() << "\n";
	}
}

void test_clock_sync_request_matches_fixture() {
	auto encoded = encode_clock_sync_request(1, 1234567890123LL);
	check_bytes_equal(encoded, read_fixture("clock_sync_request.bin"), "clock_sync_request matches fixture");
}

void test_clock_sync_response_matches_fixture() {
	auto encoded = encode_clock_sync_response(2, 1000, 1050, 1060);
	check_bytes_equal(encoded, read_fixture("clock_sync_response.bin"), "clock_sync_response matches fixture");
}

void test_imu_batch_matches_fixture() {
	std::vector<ImuSample> samples = {
		{ 0, 100, 1.5f, -2.5f, 9.8f },
		{ 1, 200, 0.1f, 0.2f, 0.3f },
	};
	auto encoded = encode_imu_batch(3, samples);
	check_bytes_equal(encoded, read_fixture("imu_batch.bin"), "imu_batch matches fixture");
}

void test_pose_sample_matches_fixture() {
	auto encoded = encode_pose_sample(4, 5000, 2, 1.0f, 2.0f, 3.0f, 0.0f, 0.0f, 0.0f, 1.0f);
	check_bytes_equal(encoded, read_fixture("pose_sample.bin"), "pose_sample matches fixture");
}

void test_camera_intrinsics_matches_fixture() {
	auto encoded = encode_camera_intrinsics(5, 600.5f, 601.5f, 320.0f, 240.0f, 640, 480);
	check_bytes_equal(encoded, read_fixture("camera_intrinsics.bin"), "camera_intrinsics matches fixture");
}

void test_point_cloud_matches_fixture() {
	std::vector<Point> points = { { 1.0f, 2.0f, 3.0f, 0.9f }, { 4.0f, 5.0f, 6.0f, 0.8f } };
	auto encoded = encode_point_cloud(6, 7000, points);
	check_bytes_equal(encoded, read_fixture("point_cloud.bin"), "point_cloud matches fixture");
}

void test_header_round_trip() {
	Header h;
	h.payload_length = 42;
	h.msg_type = 0x20;
	h.flags = 0;
	h.protocol_version = kProtocolVersion1;
	h.sequence_number = 7;
	auto encoded = encode_header(h);
	Header decoded;
	check(decode_header(encoded.data(), encoded.size(), decoded), "header decode succeeds");
	check(decoded.payload_length == 42 && decoded.msg_type == 0x20 && decoded.sequence_number == 7, "header round-trip values match");
}

void test_clock_sync_response_round_trip() {
	auto encoded = encode_clock_sync_response(1, 10, 20, 30);
	MessageView msg;
	size_t consumed = 0;
	check(next_message(encoded.data(), encoded.size(), msg, consumed), "clock_sync_response next_message succeeds");
	check(consumed == encoded.size(), "clock_sync_response consumed the whole buffer");
	check(msg.header.msg_type == static_cast<uint8_t>(MsgType::ClockSyncResponse), "clock_sync_response msg_type is correct");
	int64_t a, b, c;
	check(decode_clock_sync_response(msg.payload, msg.payload_size, a, b, c), "clock_sync_response decode succeeds");
	check(a == 10 && b == 20 && c == 30, "clock_sync_response values are correct");
}

void test_imu_batch_round_trip() {
	std::vector<ImuSample> samples = { { 0, 1, 1.0f, 2.0f, 3.0f }, { 1, 2, 4.0f, 5.0f, 6.0f } };
	auto encoded = encode_imu_batch(1, samples);
	MessageView msg;
	size_t consumed = 0;
	check(next_message(encoded.data(), encoded.size(), msg, consumed), "imu_batch next_message succeeds");
	std::vector<ImuSample> decoded;
	check(decode_imu_batch(msg.payload, msg.payload_size, decoded), "imu_batch decode succeeds");
	check(decoded.size() == 2, "imu_batch has 2 samples");
	check(decoded[0].sensor_type == 0 && decoded[0].timestamp_ns == 1 && decoded[0].x == 1.0f, "imu_batch first sample is correct");
	check(decoded[1].sensor_type == 1 && decoded[1].z == 6.0f, "imu_batch second sample is correct");
}

void test_point_cloud_round_trip() {
	std::vector<Point> points = { { 1.0f, 2.0f, 3.0f, 0.5f } };
	auto encoded = encode_point_cloud(1, 99, points);
	MessageView msg;
	size_t consumed = 0;
	check(next_message(encoded.data(), encoded.size(), msg, consumed), "point_cloud next_message succeeds");
	int64_t ts;
	std::vector<Point> decoded;
	check(decode_point_cloud(msg.payload, msg.payload_size, ts, decoded), "point_cloud decode succeeds");
	check(ts == 99 && decoded.size() == 1 && decoded[0].confidence == 0.5f, "point_cloud values are correct");
}

void test_video_chunk_round_trip() {
	std::vector<uint8_t> nal = { 0x00, 0x00, 0x00, 0x01, 0x65, 'x', 'y', 'z' };
	auto encoded = encode_video_chunk(1, 123456, true, nal.data(), nal.size());
	MessageView msg;
	size_t consumed = 0;
	check(next_message(encoded.data(), encoded.size(), msg, consumed), "video_chunk next_message succeeds");
	check(msg.payload_size == 9 + nal.size(), "video_chunk payload size is correct");
	check((msg.payload[8] & 0x01) != 0, "video_chunk keyframe bit is set");
	check(memcmp(msg.payload + 9, nal.data(), nal.size()) == 0, "video_chunk NAL data is correct");
}

void test_hello_frames_json_opaque() {
	// The C++ side doesn't parse JSON (see the note in protocol.h) -- only
	// framing (header + exact payload boundaries) is tested here.
	std::string json = R"({"device_id":"abc-123"})";
	auto encoded = encode_hello(1, json);
	MessageView msg;
	size_t consumed = 0;
	check(next_message(encoded.data(), encoded.size(), msg, consumed), "hello next_message succeeds");
	check(msg.header.msg_type == static_cast<uint8_t>(MsgType::Hello), "hello msg_type is correct");
	std::string decoded(reinterpret_cast<const char *>(msg.payload), msg.payload_size);
	check(decoded == json, "hello JSON body arrived unchanged");
}

void test_unknown_msg_type_is_skippable() {
	// Forward compatibility: an unknown msg_type can be safely skipped thanks to payload_length.
	Header unknown_header;
	unknown_header.payload_length = 4;
	unknown_header.msg_type = 0x99;
	unknown_header.sequence_number = 1;
	auto buffer = encode_header(unknown_header);
	buffer.insert(buffer.end(), { 0xDE, 0xAD, 0xBE, 0xEF });

	auto known = encode_clock_sync_request(2, 42);
	buffer.insert(buffer.end(), known.begin(), known.end());

	MessageView msg1;
	size_t consumed1 = 0;
	check(next_message(buffer.data(), buffer.size(), msg1, consumed1), "unknown message next_message succeeds");
	check(msg1.header.msg_type == 0x99, "unknown message type is preserved");

	MessageView msg2;
	size_t consumed2 = 0;
	check(next_message(buffer.data() + consumed1, buffer.size() - consumed1, msg2, consumed2), "second message is found after the skip");
	int64_t t;
	check(decode_clock_sync_request(msg2.payload, msg2.payload_size, t) && t == 42, "second message decodes correctly after the skip");
}

void test_incomplete_buffer_returns_false() {
	auto encoded = encode_clock_sync_request(1, 1);
	MessageView msg;
	size_t consumed = 0;
	check(!next_message(encoded.data(), encoded.size() - 1, msg, consumed), "incomplete buffer returns false");
	check(!next_message(encoded.data(), 5, msg, consumed), "incomplete header returns false");
}

} // namespace

int main() {
	test_clock_sync_request_matches_fixture();
	test_clock_sync_response_matches_fixture();
	test_imu_batch_matches_fixture();
	test_pose_sample_matches_fixture();
	test_camera_intrinsics_matches_fixture();
	test_point_cloud_matches_fixture();
	test_header_round_trip();
	test_clock_sync_response_round_trip();
	test_imu_batch_round_trip();
	test_point_cloud_round_trip();
	test_video_chunk_round_trip();
	test_hello_frames_json_opaque();
	test_unknown_msg_type_is_skippable();
	test_incomplete_buffer_returns_false();

	std::cout << g_checks << " checks, " << g_failures << " failed\n";
	return g_failures == 0 ? 0 : 1;
}
