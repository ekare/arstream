extends Control

# M1 duman testi: ArCapture (GDExtension singleton) ile GDScript arasindaki
# kablolamayi kanitlar. Gercek yakalama/encode/ag mantigi burada degil,
# native tarafta (mobile/native/src/) yasiyor.

@onready var result_label: Label = $VBoxContainer/ResultLabel


func _ready() -> void:
	var ar_capture: Object = Engine.get_singleton("ArCapture")
	if ar_capture == null:
		result_label.text = "HATA: ArCapture singleton bulunamadi"
		return
	ar_capture.connect("pong", _on_pong)


func _on_ping_button_pressed() -> void:
	var ar_capture: Object = Engine.get_singleton("ArCapture")
	ar_capture.ping("merhaba")


func _on_pong(message: String) -> void:
	result_label.text = message
