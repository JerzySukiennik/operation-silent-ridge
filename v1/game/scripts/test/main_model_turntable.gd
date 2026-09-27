# Model check: renders the real F-35C visual from several angles under a physical sky and saves screenshots.
extends Node3D

const VIEWS := [Vector3(14, 4, -16), Vector3(0, 30, 0.01), Vector3(24, 1, 0), Vector3(-13, 5, 16), Vector3(9, -8, -11), Vector3(3, 1.2, -9), Vector3(12, -2, -6), Vector3(-10, -3, 8)]
var cam: Camera3D
var i := 0
var wait := 0
var anim: AnimationPlayer


func _ready() -> void:
	var env := WorldEnvironment.new()
	var e := Environment.new()
	e.background_mode = Environment.BG_SKY
	e.sky = Sky.new()
	e.sky.sky_material = PhysicalSkyMaterial.new()
	e.tonemap_mode = Environment.TONE_MAPPER_AGX
	e.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	e.reflected_light_source = Environment.REFLECTION_SOURCE_SKY
	e.ssao_enabled = true
	env.environment = e
	add_child(env)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-42, 35, 0)
	sun.shadow_enabled = true
	add_child(sun)
	var v: Node3D = load("res://scenes/aircraft/f35c_visual_real.tscn").instantiate()
	add_child(v)
	var players := v.find_children("*", "AnimationPlayer", true, false)
	if players.size() > 0:
		anim = players[0]
		print("REPORT anims=%s" % [anim.get_animation_list()])
	for m in ["Nozzle", "WingtipL", "WingtipR", "Canopy"]:
		var s := MeshInstance3D.new()
		s.mesh = SphereMesh.new()
		s.scale = Vector3.ONE * 0.25
		var mat := StandardMaterial3D.new()
		mat.albedo_color = Color(1, 0.1, 0.8)
		mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		s.material_override = mat
		v.get_node(m).add_child(s)
	cam = Camera3D.new()
	cam.fov = 40
	add_child(cam)
	_place()


func _place() -> void:
	get_child(2).call("set_gear", 1.0 if i >= 6 else 0.0)
	cam.position = VIEWS[i]
	cam.look_at(Vector3.ZERO, Vector3.UP if abs(VIEWS[i].normalized().y) < 0.99 else Vector3.FORWARD)


func _process(_d: float) -> void:
	wait += 1
	if wait < 40:
		return
	wait = 0
	var dir := str(Engine.get_meta("boot_args", {}).get("shots", "user://"))
	get_viewport().get_texture().get_image().save_png(dir.path_join("model_%d.png" % i))
	i += 1
	if i >= VIEWS.size():
		print("REPORT model views=%d fps=%.1f vram_mb=%.0f" % [i, Engine.get_frames_per_second(), Performance.get_monitor(Performance.RENDER_VIDEO_MEM_USED) / 1e6])
		get_tree().quit()
		return
	_place()
