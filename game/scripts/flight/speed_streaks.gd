# Speed cue: sparse world-space air streaks and ground-effect mist around the chase camera, scaled by airspeed and height above ground.
class_name SpeedStreaks
extends GPUParticles3D

var cam: Camera3D
var aircraft: Aircraft
var _mat: ParticleProcessMaterial
var _draw_mat: StandardMaterial3D


func setup(c: Camera3D, a: Aircraft) -> void:
	cam = c
	aircraft = a
	local_coords = false
	amount = 220
	lifetime = 0.9
	preprocess = 0.0
	fixed_fps = 0
	visibility_aabb = AABB(Vector3(-400, -400, -400), Vector3(800, 800, 800))
	transform_align = GPUParticles3D.TRANSFORM_ALIGN_Z_BILLBOARD_Y_TO_VELOCITY
	_mat = ParticleProcessMaterial.new()
	_mat.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_BOX
	_mat.emission_box_extents = Vector3(22, 12, 45)
	_mat.gravity = Vector3.ZERO
	_mat.direction = Vector3(0, 0, 1)
	_mat.spread = 0.0
	_mat.initial_velocity_min = 0.5
	_mat.initial_velocity_max = 0.5
	_mat.scale_min = 0.6
	_mat.scale_max = 1.4
	var fade := Gradient.new()
	fade.set_color(0, Color(1, 1, 1, 0))
	fade.add_point(0.25, Color(1, 1, 1, 1))
	fade.add_point(0.75, Color(1, 1, 1, 1))
	fade.set_color(fade.get_point_count() - 1, Color(1, 1, 1, 0))
	var ramp := GradientTexture1D.new()
	ramp.gradient = fade
	_mat.color_ramp = ramp
	process_material = _mat
	var q := QuadMesh.new()
	q.size = Vector2(0.06, 9.0)
	_draw_mat = StandardMaterial3D.new()
	_draw_mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	_draw_mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	_draw_mat.vertex_color_use_as_albedo = true
	_draw_mat.albedo_color = Color(1, 1, 1, 0.0)
	_draw_mat.cull_mode = BaseMaterial3D.CULL_DISABLED
	_draw_mat.disable_receive_shadows = true
	_draw_mat.no_depth_test = false
	q.material = _draw_mat
	draw_pass_1 = q
	cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	emitting = true


func _process(_delta: float) -> void:
	if aircraft == null or not aircraft.alive:
		_draw_mat.albedo_color.a = 0.0
		return
	var m := aircraft.model
	var v := m.velocity
	var tas := v.length()
	var agl := m.position.y - aircraft.surface_height(m.position.x, m.position.z)
	var k := smoothstep(140.0, 320.0, tas) * lerpf(0.35, 1.0, 1.0 - smoothstep(40.0, 600.0, agl))
	_draw_mat.albedo_color.a = 0.32 * k
	amount_ratio = clampf(k, 0.05, 1.0)
	var dir := v.normalized() if tas > 1.0 else -cam.global_transform.basis.z
	global_position = cam.global_position + dir * 50.0
	global_basis = Basis.looking_at(-dir, Vector3.UP) if absf(dir.y) < 0.98 else Basis.IDENTITY
	_mat.direction = Vector3(0, 0, 1)
