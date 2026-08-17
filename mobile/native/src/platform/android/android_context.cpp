#include "android_context.h"

#include <android/log.h>

namespace {
JavaVM *g_vm = nullptr;
jobject g_activity = nullptr; // global ref, owned
} // namespace

namespace arstream::android_context {

void set_context(JNIEnv *env, jobject activity) {
	if (g_activity != nullptr) {
		// Re-entry (e.g. the Activity was recreated and Godot's main loop
		// started again) -- drop the stale global ref before replacing it.
		env->DeleteGlobalRef(g_activity);
		g_activity = nullptr;
	}
	env->GetJavaVM(&g_vm);
	g_activity = env->NewGlobalRef(activity);
	__android_log_print(ANDROID_LOG_INFO, "arstream", "android_context: JNIEnv*/Activity received");
}

JNIEnv *get_env() {
	if (g_vm == nullptr) {
		return nullptr;
	}
	JNIEnv *env = nullptr;
	jint status = g_vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6);
	if (status == JNI_EDETACHED) {
		if (g_vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
			return nullptr;
		}
	} else if (status != JNI_OK) {
		return nullptr;
	}
	return env;
}

jobject get_activity() {
	return g_activity;
}

} // namespace arstream::android_context

// JNI entry point called from JniBootstrapPlugin.kt's `external fun
// nativeSetContext`. Name is JNI's standard mangling of
// com.arstream.jnibootstrap.JniBootstrapPlugin#nativeSetContext(Activity).
extern "C" JNIEXPORT void JNICALL
Java_com_arstream_jnibootstrap_JniBootstrapPlugin_nativeSetContext(JNIEnv *env, jobject /*thiz*/, jobject activity) {
	arstream::android_context::set_context(env, activity);
}
