#pragma once

#include "../net/protocol.h"

#include <cstdint>
#include <string>
#include <vector>

namespace arstream {

// Where encoded Annex-B H.264 data (and, alongside it, sensor telemetry)
// goes. "save" and "stream" modes are two implementations of this
// interface -- ArCapture picks which one to use based on the "mode" field
// in the config.
class OutputSink {
public:
	virtual ~OutputSink() = default;

	// destination: a file path in save mode; "host:port" in stream mode.
	virtual bool open(const std::string &destination, std::string &out_error) = 0;

	// The encoder's SPS/PPS (AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) output --
	// kept SEPARATE from write_chunk() because it has its own message type
	// in the protocol (VIDEO_CONFIG, 0x05) and must not be mixed in with
	// regular frames.
	//
	// rotation_degrees: the camera sensor's ACAMERA_SENSOR_ORIENTATION value
	// (0/90/180/270, clockwise) -- the frame data ITSELF is never rotated
	// (so the zero-copy encode path stays intact), this value is carried as
	// metadata only; the receiving side (server/protocol.py, or another
	// adapter) must rotate the frame by this amount before displaying it.
	// Exactly what standard video containers (MP4 "rotation matrix" etc.)
	// do -- rotate the metadata, not the pixels.
	virtual void write_video_config(const uint8_t *sps_pps_annexb, size_t size, int32_t rotation_degrees) = 0;
	virtual void write_chunk(const uint8_t *data, size_t size, int64_t timestamp_ns, bool is_keyframe) = 0;

	// A batch of sensor readings (see SensorSampler) -- an independent
	// stream from the video, correlated only by sharing the same device
	// clock domain (docs/PROTOCOL.md §0), never muxed into the H264 data.
	virtual void write_imu_batch(const std::vector<protocol::ImuSample> &samples) = 0;

	// ARCore-only (see ArCoreCaptureSession) -- no-op source when the
	// Camera2 backend is active, these virtuals just never get called.
	// tracking_state is ArCore's ArTrackingState (0=TRACKING, 1=PAUSED,
	// 2=STOPPED); qx/qy/qz/qw + x/y/z match ArPose_getPoseRaw's order.
	virtual void write_pose_sample(int64_t timestamp_ns, uint8_t tracking_state, float x, float y, float z, float qx, float qy, float qz, float qw) = 0;
	virtual void write_point_cloud(int64_t timestamp_ns, const std::vector<protocol::Point> &points) = 0;
	// Reported once per session (fixed focus, fixed config -- intrinsics
	// don't change frame to frame), not per-frame like the other writes.
	virtual void write_camera_intrinsics(float fx, float fy, float cx, float cy, uint32_t width, uint32_t height) = 0;

	virtual void close() = 0;
};

} // namespace arstream
