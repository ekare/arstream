#include "ar_capture.h"

#include <godot_cpp/core/class_db.hpp>

using namespace godot;

ArCapture *ArCapture::singleton = nullptr;

void ArCapture::_bind_methods() {
	ClassDB::bind_method(D_METHOD("ping", "message"), &ArCapture::ping);
	ADD_SIGNAL(MethodInfo("pong", PropertyInfo(Variant::STRING, "message")));
}

ArCapture *ArCapture::get_singleton() {
	return singleton;
}

ArCapture::ArCapture() {
	ERR_FAIL_COND_MSG(singleton != nullptr, "ArCapture zaten var -- tek singleton olmali.");
	singleton = this;
}

ArCapture::~ArCapture() {
	if (singleton == this) {
		singleton = nullptr;
	}
}

void ArCapture::ping(const String &p_message) {
	emit_signal("pong", "arcapture native: " + p_message);
}
