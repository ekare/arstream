#pragma once

#include "../../net/protocol.h"

#include <android/sensor.h>

#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace arstream {

// Samples EVERY continuous/on-change sensor the device reports via the NDK
// ASensorManager -- not just accelerometer/gyroscope (see docs/PROTOCOL.md
// §3.4). Runs on its own background std::thread with its own ALooper (this
// is a separate native NDK construct from Java's Looper/Handler --
// ASensorManager_createEventQueue requires one, but it's fully self-
// contained to this thread, no Godot main-thread/JNI interaction needed).
// Batches samples and flushes every kBatchIntervalMs, matching
// docs/PROTOCOL.md §3.4's 50-100ms window.
//
// Runs UNCONDITIONALLY regardless of which capture backend (Camera2 or,
// later, ArCore) is active -- sensors are an independent subsystem from
// the camera, and the project sends raw sensor telemetry alongside
// whatever the camera backend additionally provides.
class SensorSampler {
public:
	using BatchCallback = std::function<void(const std::vector<protocol::ImuSample> &)>;

	~SensorSampler();

	// on_batch fires from this class's OWN background thread, not the
	// caller's -- unlike preview frames (which touch Godot Image/Texture
	// APIs and need call_deferred), sensor batches go straight into
	// OutputSink::write_imu_batch(), which is already safe to call from any
	// thread (StreamSink::enqueue() is mutex-guarded, FileSink's file
	// writes are confined to this one caller thread per session).
	bool start(BatchCallback on_batch, std::string &out_error);
	void stop();

private:
	ASensorManager *manager_ = nullptr; // not owned -- ASensorManager has no destroy function
	ASensorEventQueue *queue_ = nullptr;
	std::vector<ASensor const *> sensors_;
	std::thread thread_;
	std::atomic<bool> running_{ false };

	void run(BatchCallback on_batch);
};

} // namespace arstream
