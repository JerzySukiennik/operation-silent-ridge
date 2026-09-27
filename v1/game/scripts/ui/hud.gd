# HUD layer (F-35 HMD style): owns the symbology Control, the aircraft/camera it reads and the teammate list; Back/Select toggles the controls help.
class_name FlightHud
extends CanvasLayer

var aircraft: Aircraft
var camera: Camera3D
var teammates: Array = []           # each {name: String, position: Vector3}
var show_help := false
var hud_visible := true

@onready var symbology: Control = $Symbology


func set_aircraft(a: Aircraft) -> void:
	aircraft = a


func set_camera(c: Camera3D) -> void:
	camera = c


func set_teammates(list: Array) -> void:
	teammates = list


func toggle_help() -> void:
	show_help = not show_help


func active_camera() -> Camera3D:
	if camera and is_instance_valid(camera) and camera.is_inside_tree():
		return camera
	return get_viewport().get_camera_3d()


func _process(_delta: float) -> void:
	if InputMap.has_action("osr_help") and Input.is_action_just_pressed("osr_help"):
		toggle_help()
	symbology.visible = hud_visible
	symbology.queue_redraw()
