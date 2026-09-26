# Pipeline smoke test: renders a lit cube for a few seconds, reports average FPS, saves one screenshot, quits.
extends Node3D

var t := 0.0
var frames := 0


func _ready() -> void:
	var env := WorldEnvironment.new()
	var e := Environment.new()
	e.background_mode = Environment.BG_SKY
	e.sky = Sky.new()
	e.sky.sky_material = PhysicalSkyMaterial.new()
	env.environment = e
	add_child(env)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-40, 30, 0)
	sun.shadow_enabled = true
	add_child(sun)
	var m := MeshInstance3D.new()
	m.mesh = BoxMesh.new()
	add_child(m)
	var cam := Camera3D.new()
	cam.position = Vector3(2, 1.5, 3)
	add_child(cam)
	cam.look_at(Vector3.ZERO)


func _process(delta: float) -> void:
	t += delta
	frames += 1
	if t > 4.0:
		var dir := str(Engine.get_meta("boot_args", {}).get("shots", "user://"))
		get_viewport().get_texture().get_image().save_png(dir.path_join("smoke.png"))
		print("REPORT smoke fps=%.1f gpu=%s" % [frames / t, RenderingServer.get_video_adapter_name()])
		get_tree().quit()
