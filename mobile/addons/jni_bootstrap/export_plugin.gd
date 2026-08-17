@tool
extends EditorPlugin

# JniBootstrap Android eklentisi icin export-zamani kayit. Bu betik hicbir
# ARCore/is-mantigi icermez -- yalniz derlenmis .aar'i export edilen APK'ya
# dahil eder. Bkz. docs/ARCHITECTURE.md "JNI bootstrap istisnasi".

var export_plugin: AndroidExportPlugin


func _enter_tree() -> void:
	export_plugin = AndroidExportPlugin.new()
	add_export_plugin(export_plugin)


func _exit_tree() -> void:
	remove_export_plugin(export_plugin)
	export_plugin = null


class AndroidExportPlugin extends EditorExportPlugin:
	func _supports_platform(platform: EditorExportPlatform) -> bool:
		return platform is EditorExportPlatformAndroid

	func _get_android_libraries(platform: EditorExportPlatform, debug: bool) -> PackedStringArray:
		if debug:
			return PackedStringArray(["jni_bootstrap/bin/debug/jni_bootstrap-debug.aar"])
		else:
			return PackedStringArray(["jni_bootstrap/bin/release/jni_bootstrap-release.aar"])

	# ARCore's C API is a thin wrapper that calls back into ARCore's own
	# Java classes via JNI (ArCoreApk_checkAvailabilityAsync aborts under
	# -Xcheck:jni without them, confirmed on-device). A raw local .aar
	# (from _get_android_libraries above) has no pom.xml, so Gradle can't
	# see jni_bootstrap's own "com.google.ar:core" build.gradle.kts
	# dependency transitively -- Godot's own Gradle build (which knows
	# nothing about that separate module) needs it declared here instead.
	func _get_android_dependencies(platform: EditorExportPlatform, debug: bool) -> PackedStringArray:
		return PackedStringArray(["com.google.ar:core:1.54.0"])

	func _get_name() -> String:
		return "JniBootstrap"
