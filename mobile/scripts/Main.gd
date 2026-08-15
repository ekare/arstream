extends Control

# M1: ping/pong smoke test proving the wiring between ArCapture (GDExtension
# singleton) and GDScript.
# M2/M3: Camera2 fallback capture + AMediaCodec H.264 encode; "save" (file)
# / "stream" (not yet implemented, see docs/ROADMAP.md) selectable output.
# Preview: toggleable on/off fully independently of recording/streaming --
# can be turned on any time once permission is granted. While capture is
# running it's the second (small) output of the same capture session; while
# capture is not running, the native side opens its own encoder-less camera
# session (see set_preview_enabled in ar_capture.cpp). A red dot
# (RecordingIndicator) appears in the top-right corner while recording.
# The actual capture/encode/file/preview logic doesn't live here, it lives
# on the native side (mobile/native/src/) -- this script is only UI and the permission flow.

const CAMERA_PERMISSION := "android.permission.CAMERA"

@onready var preview_rect: TextureRect = $PreviewRect
@onready var result_label: Label = $Overlay/ResultLabel
@onready var mode_button: OptionButton = $Overlay/ModeButton
@onready var host_port_edit: LineEdit = $Overlay/HostPortEdit
@onready var capture_button: Button = $Overlay/CaptureButton
@onready var preview_button: CheckButton = $PreviewButton
@onready var status_label: Label = $Overlay/StatusLabel
@onready var recording_indicator: Panel = $RecordingIndicator

var _permission_check_timer: Timer
var _is_capturing := false


func _ready() -> void:
	var ar_capture: Object = Engine.get_singleton("ArCapture")
	if ar_capture == null:
		status_label.text = "ERROR: ArCapture singleton not found"
		return

	preview_rect.texture = ar_capture.get_preview_texture()

	ar_capture.connect("pong", _on_pong)
	ar_capture.connect("capture_started", _on_capture_started)
	ar_capture.connect("capture_stopped", _on_capture_stopped)
	ar_capture.connect("stats_updated", _on_stats_updated)
	ar_capture.connect("capture_error", _on_capture_error)

	mode_button.add_item("Record (to file)", 0)
	mode_button.add_item("Stream (TCP)", 1)

	_ensure_camera_permission()


func _ensure_camera_permission() -> void:
	if not OS.has_feature("android"):
		status_label.text = "This platform isn't supported yet (Android only)."
		return

	var granted: PackedStringArray = OS.get_granted_permissions()
	if granted.has(CAMERA_PERMISSION):
		_on_permission_granted()
		return

	status_label.text = "Requesting camera permission..."
	OS.request_permission(CAMERA_PERMISSION)

	_permission_check_timer = Timer.new()
	_permission_check_timer.wait_time = 0.5
	_permission_check_timer.one_shot = false
	add_child(_permission_check_timer)
	_permission_check_timer.timeout.connect(_check_permission_again)
	_permission_check_timer.start()


func _check_permission_again() -> void:
	var granted: PackedStringArray = OS.get_granted_permissions()
	if granted.has(CAMERA_PERMISSION):
		_permission_check_timer.stop()
		_on_permission_granted()


func _on_permission_granted() -> void:
	status_label.text = "Camera permission granted. Ready."
	capture_button.disabled = false
	# Preview is now independent of recording -- it can be turned on before
	# recording starts too (the native side opens its own encoder-less camera session).
	preview_button.disabled = false


func _on_ping_button_pressed() -> void:
	var ar_capture: Object = Engine.get_singleton("ArCapture")
	ar_capture.ping("hello")


func _on_pong(message: String) -> void:
	result_label.text = message


func _on_capture_button_pressed() -> void:
	var ar_capture: Object = Engine.get_singleton("ArCapture")
	if _is_capturing:
		ar_capture.stop_capture()
		return

	var mode := "save" if mode_button.selected == 0 else "stream"

	var config := {
		"mode": mode,
		"width": 1280,
		"height": 720,
		"fps": 30,
		"bitrate_bps": 4000000,
	}

	if mode == "save":
		config["output_path"] = OS.get_user_data_dir().path_join("capture.h264")
	else:
		var parts := host_port_edit.text.rsplit(":", false, 1)
		if parts.size() != 2 or not parts[1].is_valid_int():
			status_label.text = "ERROR: invalid host:port format (%s)" % host_port_edit.text
			return
		config["host"] = parts[0]
		config["port"] = int(parts[1])
		config["spool_path"] = OS.get_user_data_dir().path_join("stream_spool.bin")
	status_label.text = "Starting (%s)..." % mode
	ar_capture.start_capture(config)


func _on_capture_started() -> void:
	_is_capturing = true
	capture_button.text = "Stop capture"
	recording_indicator.visible = true
	status_label.text = "Capturing..."


func _on_capture_stopped(reason: String) -> void:
	_is_capturing = false
	capture_button.text = "Start capture"
	recording_indicator.visible = false
	status_label.text = "Stopped: " + reason
	if preview_button.button_pressed:
		# Preview was using the SAME session as the encoder during
		# recording/streaming; stop_capture() closed that session entirely.
		# If the user left preview on (toggle still pressed), it now needs to
		# be reopened as its own (encoder-less) session -- otherwise the image would freeze.
		var ar_capture: Object = Engine.get_singleton("ArCapture")
		ar_capture.set_preview_enabled(true)


func _on_preview_button_toggled(pressed: bool) -> void:
	var ar_capture: Object = Engine.get_singleton("ArCapture")
	# While capturing: the same session is already running, only the flag changes.
	# While not capturing: the native side opens/closes its own camera session.
	ar_capture.set_preview_enabled(pressed)


func _on_stats_updated(frames_encoded: int, bytes_written: int, fps: float) -> void:
	status_label.text = "Frames: %d, Written: %.1f KB, fps: %.1f" % [frames_encoded, bytes_written / 1024.0, fps]


func _on_capture_error(message: String) -> void:
	status_label.text = "ERROR: " + message
