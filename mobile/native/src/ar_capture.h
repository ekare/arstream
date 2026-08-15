#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/binder_common.hpp>

namespace godot {

// GDScript'e acilan tek yuzey: ArCapture. M1'de yalnizca kablolamayi kanitlayan
// bir ping/pong var; start_capture/stop_capture/get_capabilities M2+'de eklenir.
// Sicak veri yolu (yakalama/encode/ag) burada, native tarafta kalir -- bkz.
// docs/ARCHITECTURE.md "Mimari karar" bolumu.
class ArCapture : public RefCounted {
	GDCLASS(ArCapture, RefCounted)

private:
	static ArCapture *singleton;

protected:
	static void _bind_methods();

public:
	static ArCapture *get_singleton();

	ArCapture();
	~ArCapture();

	// M1 duman testi: GDScript -> native -> sinyal round-trip'i kanitlar.
	void ping(const String &p_message);
};

} // namespace godot
