#pragma once

// arstream kablo protokolu v1 -- docs/PROTOCOL.md'nin birebir C++ yansimasi.
//
// KASITLI OLARAK GODOT-CPP'YE BAGIMLI DEGIL: bu dosya hem GDExtension'in
// icinde (Android/Windows) hem de bagimsiz bir host test executable'inda
// (mobile/native/tests/) derlenir -- SCons/godot-cpp gerekmeden, cihaz/
// emulator gerekmeden calisir. Yalniz stdlib.
//
// Kapsam notu: JSON govdeli mesajlarda (HELLO, VIDEO_CONFIG'in json kismi,
// STATUS, GOODBYE) JSON'un kendisi burada uretilmiyor/ayristirilmiyor --
// cagiran taraf hazir bir JSON string'i verir/alir. C++'ta bagimliliksiz bir
// JSON kutuphanesi olmadigi icin bu bilincli bir sinir; server/protocol.py
// tarafinda (Python stdlib json ile) tam encode/decode var. Burada test
// edilen sey kablo CERCEVELEMESI (header + payload sinirlari) -- protokolun
// asil "iki bagimsiz implementasyon ayrisir mi" riski budur.

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

// -- Encode: header + tam mesaj govdesi birlikte, gonderilmeye hazir byte'lar --

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

// Header'i cozer. false donerse `size` 12 byte'tan az demektir.
bool decode_header(const uint8_t *data, size_t size, Header &out);

// Bilinmeyen msg_type'lari da guvenle atlayarak bir sonraki mesaja gecmek
// icin: header'i okur, payload'a isaretci+uzunluk doner, toplam tuketilen
// byte sayisini yazar. `buffer` icinde tam bir mesaj yoksa false doner
// (cagiran taraf daha fazla byte gelene kadar beklemeli).
struct MessageView {
	Header header;
	const uint8_t *payload = nullptr;
	size_t payload_size = 0;
};
bool next_message(const uint8_t *buffer, size_t size, MessageView &out, size_t &bytes_consumed);

// Sabit-duzenli govdeler icin dogrudan cozucu -- payload, next_message()'in
// dondurdugu MessageView.payload/payload_size ile cagrilir.
bool decode_clock_sync_request(const uint8_t *payload, size_t size, int64_t &out_client_send_time_ns);
bool decode_clock_sync_response(const uint8_t *payload, size_t size, int64_t &out_client_send_time_ns, int64_t &out_server_recv_time_ns, int64_t &out_server_send_time_ns);
bool decode_imu_batch(const uint8_t *payload, size_t size, std::vector<ImuSample> &out_samples);
bool decode_pose_sample(const uint8_t *payload, size_t size, int64_t &out_timestamp_ns, uint8_t &out_tracking_state, float &x, float &y, float &z, float &qx, float &qy, float &qz, float &qw);
bool decode_point_cloud(const uint8_t *payload, size_t size, int64_t &out_timestamp_ns, std::vector<Point> &out_points);
bool decode_camera_intrinsics(const uint8_t *payload, size_t size, float &fx, float &fy, float &cx, float &cy, uint32_t &width, uint32_t &height);

} // namespace arstream::protocol
