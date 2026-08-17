#include "sensor_sampler.h"

#include <android/looper.h>

#include <chrono>

namespace arstream {

namespace {

// Must match export_presets.cfg's package/unique_name -- no compile-time
// enforcement, silent-drift risk if the package is ever renamed.
constexpr const char *kPackageName = "com.arstream.mobile";

constexpr int kLooperIdent = 1;
constexpr int kBatchIntervalMs = 75; // midpoint of docs/PROTOCOL.md §3.4's 50-100ms range
constexpr size_t kMaxEventsPerDrain = 32;

// Scalar sensors (as opposed to 3-axis vector ones) only populate
// event.data[0] -- data[1]/data[2] aren't guaranteed zeroed by the platform,
// so callers must not read them. Matches the "y=z=0" contract documented in
// docs/PROTOCOL.md §3.4.
bool is_scalar_sensor_type(int32_t type) {
	switch (type) {
		case ASENSOR_TYPE_LIGHT:
		case ASENSOR_TYPE_PRESSURE:
		case ASENSOR_TYPE_PROXIMITY:
		case ASENSOR_TYPE_RELATIVE_HUMIDITY:
		case ASENSOR_TYPE_AMBIENT_TEMPERATURE:
			return true;
		default:
			return false;
	}
}

} // namespace

SensorSampler::~SensorSampler() {
	stop();
}

bool SensorSampler::start(BatchCallback on_batch, std::string &out_error) {
	if (running_) {
		return true;
	}

	manager_ = ASensorManager_getInstanceForPackage(kPackageName);
	if (manager_ == nullptr) {
		out_error = "Could not get ASensorManager instance";
		return false;
	}

	ASensorList list;
	int count = ASensorManager_getSensorList(manager_, &list);
	sensors_.clear();
	for (int i = 0; i < count; i++) {
		// ONE_SHOT (e.g. significant motion) and SPECIAL_TRIGGER sensors
		// don't behave like a continuous stream -- skip them, stream
		// everything else the device reports.
		int mode = ASensor_getReportingMode(list[i]);
		if (mode == AREPORTING_MODE_CONTINUOUS || mode == AREPORTING_MODE_ON_CHANGE) {
			sensors_.push_back(list[i]);
		}
	}
	if (sensors_.empty()) {
		out_error = "No usable (continuous/on-change) sensors found on this device";
		return false;
	}

	running_ = true;
	thread_ = std::thread(&SensorSampler::run, this, on_batch);
	return true;
}

void SensorSampler::run(BatchCallback on_batch) {
	ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
	queue_ = ASensorManager_createEventQueue(manager_, ALooper_forThread(), kLooperIdent, nullptr, nullptr);
	for (ASensor const *sensor : sensors_) {
		ASensorEventQueue_enableSensor(queue_, sensor);
		int32_t min_delay_us = ASensor_getMinDelay(sensor);
		if (min_delay_us > 0) {
			ASensorEventQueue_setEventRate(queue_, sensor, min_delay_us);
		}
	}

	std::vector<protocol::ImuSample> batch;
	auto last_flush = std::chrono::steady_clock::now();

	while (running_) {
		// ALooper_pollAll is obsoleted in newer NDKs (can miss wakes) --
		// ALooper_pollOnce is the replacement; for a single ident with no
		// callback, one poll per loop iteration is exactly what we want.
		int ident = ALooper_pollOnce(kBatchIntervalMs, nullptr, nullptr, nullptr);
		if (!running_) {
			break;
		}
		if (ident == kLooperIdent) {
			ASensorEvent events[kMaxEventsPerDrain];
			ssize_t n;
			while ((n = ASensorEventQueue_getEvents(queue_, events, kMaxEventsPerDrain)) > 0) {
				for (ssize_t i = 0; i < n; i++) {
					const ASensorEvent &e = events[i];
					bool scalar = is_scalar_sensor_type(e.type);
					protocol::ImuSample sample;
					sample.sensor_type = static_cast<uint8_t>(e.type);
					sample.timestamp_ns = e.timestamp;
					sample.x = e.data[0];
					sample.y = scalar ? 0.0f : e.data[1];
					sample.z = scalar ? 0.0f : e.data[2];
					batch.push_back(sample);
				}
			}
		}

		auto now = std::chrono::steady_clock::now();
		if (!batch.empty() && std::chrono::duration_cast<std::chrono::milliseconds>(now - last_flush).count() >= kBatchIntervalMs) {
			on_batch(batch);
			batch.clear();
			last_flush = now;
		}
	}

	for (ASensor const *sensor : sensors_) {
		ASensorEventQueue_disableSensor(queue_, sensor);
	}
	ASensorManager_destroyEventQueue(manager_, queue_);
	queue_ = nullptr;
}

void SensorSampler::stop() {
	if (!running_) {
		return;
	}
	running_ = false;
	if (thread_.joinable()) {
		thread_.join();
	}
	sensors_.clear();
	manager_ = nullptr; // not owned, no delete
}

} // namespace arstream
