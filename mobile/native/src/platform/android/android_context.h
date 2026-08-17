#pragma once

#include <jni.h>

namespace arstream::android_context {

// Called once, from JniBootstrapPlugin.kt's nativeSetContext (see
// mobile/android/plugins/jni_bootstrap/), at startup. Stores a global ref
// to the Activity and this process's JavaVM for later use by ARCore, which
// needs a JNIEnv*/jobject context to check availability and create a
// session (see arcore_c_api.h). This is the ONLY JNI entry point in the
// project -- everything past this handoff is plain C/C++ against NDK and
// ARCore's C API, no further Kotlin involved.
void set_context(JNIEnv *env, jobject activity);

// Returns a JNIEnv* valid on the CALLING thread, attaching it to the JVM
// first if necessary -- any native background thread (this project's
// capture/sensor threads) is not attached to the JVM by default. Returns
// nullptr if set_context() has not been called yet, or attach fails.
JNIEnv *get_env();

// Global ref to the Activity passed to set_context(). Returns nullptr if
// set_context() has not been called yet.
jobject get_activity();

} // namespace arstream::android_context
