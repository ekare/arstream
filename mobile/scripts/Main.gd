extends Control

# M1: ArCapture (GDExtension singleton) ile GDScript arasindaki kablolamayi
# kanitlayan ping/pong duman testi.
# M2/M3: Camera2 geri-dusus yakalama + AMediaCodec H.264 encode; "save" (dosya)
# / "stream" (henuz uygulanmadi, bkz. docs/ROADMAP.md) secilebilir cikis.
# Onizleme: kayittan bagimsiz ac/kapat -- ayni capture session'in ikinci
# (kucuk) kamera ciktisi, yalniz acikken CPU'da islenir (bkz. ar_capture.cpp).
# Gercek yakalama/encode/dosya/onizleme mantigi burada degil, native tarafta
# (mobile/native/src/) yasiyor -- bu betik yalnizca UI ve izin akisi.

const CAMERA_PERMISSION := "android.permission.CAMERA"

@onready var preview_rect: TextureRect = $PreviewRect
@onready var result_label: Label = $Overlay/ResultLabel
@onready var mode_button: OptionButton = $Overlay/ModeButton
@onready var host_port_edit: LineEdit = $Overlay/HostPortEdit
@onready var capture_button: Button = $Overlay/CaptureButton
@onready var preview_button: CheckButton = $PreviewButton
@onready var status_label: Label = $Overlay/StatusLabel

var _permission_check_timer: Timer
var _is_capturing := false


func _ready() -> void:
	var ar_capture: Object = Engine.get_singleton("ArCapture")
	if ar_capture == null:
		status_label.text = "HATA: ArCapture singleton bulunamadi"
		return

	preview_rect.texture = ar_capture.get_preview_texture()

	ar_capture.connect("pong", _on_pong)
	ar_capture.connect("capture_started", _on_capture_started)
	ar_capture.connect("capture_stopped", _on_capture_stopped)
	ar_capture.connect("stats_updated", _on_stats_updated)
	ar_capture.connect("capture_error", _on_capture_error)

	mode_button.add_item("Kaydet (dosyaya)", 0)
	mode_button.add_item("Yayinla (TCP)", 1)

	_ensure_camera_permission()


func _ensure_camera_permission() -> void:
	if not OS.has_feature("android"):
		status_label.text = "Bu platform simdilik desteklenmiyor (yalniz Android)."
		return

	var granted: PackedStringArray = OS.get_granted_permissions()
	if granted.has(CAMERA_PERMISSION):
		_on_permission_granted()
		return

	status_label.text = "Kamera izni isteniyor..."
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
	status_label.text = "Kamera izni verildi. Hazir."
	capture_button.disabled = false


func _on_ping_button_pressed() -> void:
	var ar_capture: Object = Engine.get_singleton("ArCapture")
	ar_capture.ping("merhaba")


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
			status_label.text = "HATA: host:port formati gecersiz (%s)" % host_port_edit.text
			return
		config["host"] = parts[0]
		config["port"] = int(parts[1])
		config["spool_path"] = OS.get_user_data_dir().path_join("stream_spool.bin")
	status_label.text = "Baslatiliyor (%s)..." % mode
	ar_capture.start_capture(config)


func _on_capture_started() -> void:
	_is_capturing = true
	capture_button.text = "Yakalamayi durdur"
	preview_button.disabled = false
	status_label.text = "Yakalaniyor..."


func _on_capture_stopped(reason: String) -> void:
	_is_capturing = false
	capture_button.text = "Yakalamayi baslat"
	preview_button.disabled = true
	preview_button.button_pressed = false
	status_label.text = "Durduruldu: " + reason


func _on_preview_button_toggled(pressed: bool) -> void:
	var ar_capture: Object = Engine.get_singleton("ArCapture")
	# Async: kaydin kendisini durdurmadan/beklemeden ac-kapa -- ayni capture
	# session zaten calisiyor, burada yalniz native tarafta bir bayrak degisiyor.
	ar_capture.set_preview_enabled(pressed)


func _on_stats_updated(frames_encoded: int, bytes_written: int, fps: float) -> void:
	status_label.text = "Kare: %d, Yazilan: %.1f KB, fps: %.1f" % [frames_encoded, bytes_written / 1024.0, fps]


func _on_capture_error(message: String) -> void:
	status_label.text = "HATA: " + message
