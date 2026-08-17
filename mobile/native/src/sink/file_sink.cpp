#include "file_sink.h"

namespace arstream {

FileSink::~FileSink() {
	close();
}

bool FileSink::open(const std::string &destination, std::string &out_error) {
	file_ = fopen(destination.c_str(), "wb");
	if (file_ == nullptr) {
		out_error = "Could not open file: " + destination;
		return false;
	}
	bytes_written_ = 0;
	// Best-effort sibling files -- if any fails to open, that data is just
	// silently dropped (each write_* no-ops on its file == nullptr); the
	// video recording itself must not be blocked by this.
	imu_file_ = fopen((destination + ".imu.jsonl").c_str(), "w");
	poses_file_ = fopen((destination + ".poses.jsonl").c_str(), "w");
	points_file_ = fopen((destination + ".points.jsonl").c_str(), "w");
	intrinsics_path_ = destination + ".intrinsics.json";
	return true;
}

void FileSink::write_video_config(const uint8_t *sps_pps_annexb, size_t size, int32_t rotation_degrees) {
	// In the Annex-B stream, SPS/PPS arrives in order just like frames --
	// no separate body is needed, writing it to the same file in the same
	// order is enough. rotation_degrees is unused here -- raw Annex-B has
	// no metadata field (see the note in output_sink.h); anyone working
	// with the file has to know by hand that the recording needs rotating by SENSOR_ORIENTATION.
	(void)rotation_degrees;
	if (file_ == nullptr || sps_pps_annexb == nullptr || size == 0) {
		return;
	}
	fwrite(sps_pps_annexb, 1, size, file_);
	bytes_written_ += static_cast<int64_t>(size);
}

void FileSink::write_chunk(const uint8_t *data, size_t size, int64_t timestamp_ns, bool is_keyframe) {
	(void)timestamp_ns;
	(void)is_keyframe;
	if (file_ == nullptr || data == nullptr || size == 0) {
		return;
	}
	fwrite(data, 1, size, file_);
	bytes_written_ += static_cast<int64_t>(size);
}

void FileSink::write_imu_batch(const std::vector<protocol::ImuSample> &samples) {
	if (imu_file_ == nullptr) {
		return;
	}
	for (const protocol::ImuSample &s : samples) {
		std::string line = "{\"sensor_type\":" + std::to_string(s.sensor_type) +
				",\"timestamp_ns\":" + std::to_string(s.timestamp_ns) +
				",\"x\":" + std::to_string(s.x) +
				",\"y\":" + std::to_string(s.y) +
				",\"z\":" + std::to_string(s.z) + "}\n";
		fwrite(line.data(), 1, line.size(), imu_file_);
	}
	fflush(imu_file_);
}

void FileSink::write_pose_sample(int64_t timestamp_ns, uint8_t tracking_state, float x, float y, float z, float qx, float qy, float qz, float qw) {
	if (poses_file_ == nullptr) {
		return;
	}
	std::string line = "{\"timestamp_ns\":" + std::to_string(timestamp_ns) +
			",\"tracking_state\":" + std::to_string(tracking_state) +
			",\"x\":" + std::to_string(x) + ",\"y\":" + std::to_string(y) + ",\"z\":" + std::to_string(z) +
			",\"qx\":" + std::to_string(qx) + ",\"qy\":" + std::to_string(qy) +
			",\"qz\":" + std::to_string(qz) + ",\"qw\":" + std::to_string(qw) + "}\n";
	fwrite(line.data(), 1, line.size(), poses_file_);
	fflush(poses_file_);
}

void FileSink::write_point_cloud(int64_t timestamp_ns, const std::vector<protocol::Point> &points) {
	if (points_file_ == nullptr) {
		return;
	}
	std::string line = "{\"timestamp_ns\":" + std::to_string(timestamp_ns) + ",\"points\":[";
	for (size_t i = 0; i < points.size(); i++) {
		if (i > 0) {
			line += ",";
		}
		const protocol::Point &p = points[i];
		line += "[" + std::to_string(p.x) + "," + std::to_string(p.y) + "," + std::to_string(p.z) + "," + std::to_string(p.confidence) + "]";
	}
	line += "]}\n";
	fwrite(line.data(), 1, line.size(), points_file_);
	fflush(points_file_);
}

void FileSink::write_camera_intrinsics(float fx, float fy, float cx, float cy, uint32_t width, uint32_t height) {
	// Snapshot, not append -- overwrites on every call, matching
	// server/plugins/recorder.py's on_camera_intrinsics (fixed focus means
	// this rarely changes, but if it ever does, the latest value wins).
	FILE *f = fopen(intrinsics_path_.c_str(), "w");
	if (f == nullptr) {
		return;
	}
	std::string json = "{\"fx\":" + std::to_string(fx) + ",\"fy\":" + std::to_string(fy) +
			",\"cx\":" + std::to_string(cx) + ",\"cy\":" + std::to_string(cy) +
			",\"width\":" + std::to_string(width) + ",\"height\":" + std::to_string(height) + "}";
	fwrite(json.data(), 1, json.size(), f);
	fclose(f);
}

void FileSink::close() {
	if (file_ != nullptr) {
		fflush(file_);
		fclose(file_);
		file_ = nullptr;
	}
	if (imu_file_ != nullptr) {
		fflush(imu_file_);
		fclose(imu_file_);
		imu_file_ = nullptr;
	}
	if (poses_file_ != nullptr) {
		fflush(poses_file_);
		fclose(poses_file_);
		poses_file_ = nullptr;
	}
	if (points_file_ != nullptr) {
		fflush(points_file_);
		fclose(points_file_);
		points_file_ = nullptr;
	}
}

} // namespace arstream
