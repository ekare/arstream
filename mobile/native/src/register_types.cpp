#include "register_types.h"

#include <gdextension_interface.h>

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

#include "ar_capture.h"

using namespace godot;

static ArCapture *ar_capture_singleton = nullptr;

void initialize_arcapture_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	GDREGISTER_CLASS(ArCapture);

	ar_capture_singleton = memnew(ArCapture);
	Engine::get_singleton()->register_singleton("ArCapture", ArCapture::get_singleton());
}

void uninitialize_arcapture_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	Engine::get_singleton()->unregister_singleton("ArCapture");
	memdelete(ar_capture_singleton);
	ar_capture_singleton = nullptr;
}

extern "C" {
GDExtensionBool GDE_EXPORT arcapture_library_init(GDExtensionInterfaceGetProcAddress p_get_proc_address, GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization) {
	::godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);

	init_obj.register_initializer(initialize_arcapture_module);
	init_obj.register_terminator(uninitialize_arcapture_module);
	init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);

	return init_obj.init();
}
}
