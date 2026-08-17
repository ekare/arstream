#pragma once

#include "output_sink.h"

#include <cstdio>
#include <string>

namespace arstream {

// Writes the Annex-B stream to a file as-is (SPS/PPS + frames in order) --
// a raw H.264 elementary stream that can be played directly with ffprobe/ffplay.
// Sensor batches go to a sibling "<destination>.imu.jsonl" file (same line
// shape the server writes, see server/README.md#recorded-files) -- this
// lets "save" mode be inspected/debugged entirely on-device, no server needed.
// ARCore pose/point-cloud/intrinsics (when that backend is active) go to
// their own sibling files the same way, mirroring server/plugins/recorder.py's
// poses.jsonl/points.jsonl/intrinsics.json.
class FileSink : public OutputSink {
public:
	~FileSink() override;

	bool open(const std::string &destination, std::string &out_error) override;
	void write_video_config(const uint8_t *sps_pps_annexb, size_t size, int32_t rotation_degrees) override;
	void write_chunk(const uint8_t *data, size_t size, int64_t timestamp_ns, bool is_keyframe) override;
	void write_imu_batch(const std::vector<protocol::ImuSample> &samples) override;
	void write_pose_sample(int64_t timestamp_ns, uint8_t tracking_state, float x, float y, float z, float qx, float qy, float qz, float qw) override;
	void write_point_cloud(int64_t timestamp_ns, const std::vector<protocol::Point> &points) override;
	void write_camera_intrinsics(float fx, float fy, float cx, float cy, uint32_t width, uint32_t height) override;
	void close() override;

	int64_t bytes_written() const { return bytes_written_; }

private:
	FILE *file_ = nullptr;
	FILE *imu_file_ = nullptr;
	FILE *poses_file_ = nullptr;
	FILE *points_file_ = nullptr;
	std::string intrinsics_path_;
	int64_t bytes_written_ = 0;
};

} // namespace arstream
