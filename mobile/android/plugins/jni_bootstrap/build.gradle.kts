plugins {
    id("com.android.library") version "8.6.1"
    id("org.jetbrains.kotlin.android") version "2.1.20"
}

android {
    namespace = "com.arstream.jnibootstrap"
    compileSdk = 35

    defaultConfig {
        minSdk = 26
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = "17"
    }
}

dependencies {
    // The Godot Android library, hosted on Maven Central -- provides GodotPlugin
    // and @UsedByGodot. Latest published version is 4.4.1.stable (no 4.6.x
    // published yet as of this writing); the Android plugin API surface has
    // historically stayed stable across minor versions -- being verified on
    // device as part of Phase B, see docs/ROADMAP.md.
    compileOnly("org.godotengine:godot:4.4.1.stable")

    // NOTE: this Kotlin module does not itself call any
    // com.google.ar.core.* Java API (ARCore is only ever touched from
    // C++, via arcore_c_api.h -- see arcore_availability.cpp), so no
    // ARCore dependency is declared here. It's still required in the
    // final APK (ArCoreApk_checkAvailabilityAsync calls back into
    // ARCore's own Java classes via JNI internally and aborts under
    // -Xcheck:jni without them, confirmed on-device) -- but a raw local
    // .aar built by this module has no pom.xml for Gradle to resolve
    // transitive dependencies from, so it's declared instead where
    // Godot's own Gradle build can see it:
    // mobile/addons/jni_bootstrap/export_plugin.gd's
    // _get_android_dependencies().
}
