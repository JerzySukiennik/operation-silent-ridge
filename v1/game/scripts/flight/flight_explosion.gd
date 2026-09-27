# Crash explosion: one-shot fireball, debris sparks, flash light and a lingering smoke column; frees itself when done.
class_name FlightExplosion
extends Node3D

const PUFF_SHADER := preload("res://shaders/vfx/smoke_puff.gdshader")
const LIFETIME := 28.0

static var _fire_mat: ShaderMaterial
static var _smoke_mat: ShaderMaterial
static var _spark_mat: StandardMaterial3D
static var _spray_mat: ShaderMaterial

var _t := 0.0
var _prewarm_only := false
var _light: OmniLight3D


static func _materials() -> void:
	if _fire_mat:
		return
	var noise := AircraftVfx.noise_texture()
	_fire_mat = ShaderMaterial.new()
	_fire_mat.shader = PUFF_SHADER
	_fire_mat.set_shader_parameter("noise_tex", noise)
	_fire_mat.set_shader_parameter("emissive", 2.4)
	_fire_mat.set_shader_parameter("softness", 0.6)
	_smoke_mat = ShaderMaterial.new()
	_smoke_mat.shader = PUFF_SHADER
	_smoke_mat.set_shader_parameter("noise_tex", noise)
	_smoke_mat.set_shader_parameter("emissive", 0.0)
	_smoke_mat.set_shader_parameter("softness", 0.7)
	_spray_mat = ShaderMaterial.new()
	_spray_mat.shader = PUFF_SHADER
	_spray_mat.set_shader_parameter("noise_tex", noise)
	_spray_mat.set_shader_parameter("emissive", 0.35)
	_spray_mat.set_shader_parameter("softness", 0.8)
	_spark_mat = StandardMaterial3D.new()
	_spark_mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	_spark_mat.albedo_color = Color(1.0, 0.7, 0.3)
	_spark_mat.billboard_mode = BaseMaterial3D.BILLBOARD_PARTICLES
	_spark_mat.vertex_color_use_as_albedo = true
	_spark_mat.emission_enabled = true
	_spark_mat.emission = Color(1.0, 0.55, 0.2)
	_spark_mat.emission_energy_multiplier = 4.0


static func prewarm(parent: Node3D) -> void:
	# a microscopic explosion in front of the jet compiles the particle pipelines at spawn instead of at the first crash
	var e := FlightExplosion.new()
	e._prewarm_only = true
	parent.add_child(e)
	e.position = Vector3(0, 0, -30)
	e.start(Vector3.ZERO, true)


func start(velocity: Vector3, water: bool = false) -> void:
	_materials()
	var carry := velocity * 0.25
	carry.y = maxf(carry.y, 0.0)
	add_child(_particles("Fire", 36, 2.2, 0.95, _fire_mat, _fire_process(carry), 9.0))
	add_child(_particles("Smoke", 60, 16.0, 0.35, _smoke_mat, _smoke_process(carry), 22.0))
	add_child(_particles("Debris", 40, 2.8, 1.0, _spark_mat, _debris_process(velocity * 0.4), 0.35))
	if water:
		add_child(_particles("Spray", 48, 3.5, 0.9, _spray_mat, _spray_process(), 12.0))
	_light = OmniLight3D.new()
	_light.light_color = Color(1.0, 0.6, 0.3)
	_light.light_energy = 0.0 if _prewarm_only else 12.0
	_light.omni_range = 180.0
	_light.shadow_enabled = false
	_light.position = Vector3(0, 8, 0)
	add_child(_light)


func _particles(n: String, amount: int, life: float, explosive: float, mat: Material, proc: ParticleProcessMaterial, size: float) -> GPUParticles3D:
	var p := GPUParticles3D.new()
	p.name = n
	p.amount = amount
	p.lifetime = life
	p.one_shot = true
	p.explosiveness = explosive
	p.randomness = 0.5
	p.local_coords = false
	p.process_material = proc
	p.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	p.visibility_aabb = AABB(Vector3(-300, -50, -300), Vector3(600, 700, 600))
	var q := QuadMesh.new()
	q.size = Vector2(size, size) * (0.001 if _prewarm_only else 1.0)
	q.material = mat
	p.draw_pass_1 = q
	p.emitting = true
	return p


func _ramp(stops: Array) -> GradientTexture1D:
	var g := Gradient.new()
	var offs := PackedFloat32Array()
	var cols := PackedColorArray()
	for s: Array in stops:
		offs.append(s[0])
		cols.append(s[1])
	g.offsets = offs
	g.colors = cols
	var t := GradientTexture1D.new()
	t.gradient = g
	return t


func _curve(points: Array) -> CurveTexture:
	var c := Curve.new()
	c.max_value = 4.0
	for pt: Vector2 in points:
		c.add_point(pt)
	var t := CurveTexture.new()
	t.curve = c
	return t


func _fire_process(carry: Vector3) -> ParticleProcessMaterial:
	var m := ParticleProcessMaterial.new()
	m.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_SPHERE
	m.emission_sphere_radius = 6.0
	m.direction = Vector3(0, 1, 0)
	m.spread = 80.0
	m.initial_velocity_min = 8.0 + carry.length() * 0.2
	m.initial_velocity_max = 30.0 + carry.length() * 0.3
	m.gravity = Vector3(0, 6.0, 0)
	m.damping_min = 6.0
	m.damping_max = 12.0
	m.scale_min = 0.8
	m.scale_max = 2.0
	m.scale_curve = _curve([Vector2(0, 0.3), Vector2(0.25, 1.0), Vector2(1, 1.6)])
	m.angle_min = 0.0
	m.angle_max = 360.0
	m.color_ramp = _ramp([[0.0, Color(1.0, 0.85, 0.5, 1.0)], [0.15, Color(1.0, 0.45, 0.1, 1.0)], [0.45, Color(0.55, 0.14, 0.03, 0.9)], [0.75, Color(0.12, 0.08, 0.06, 0.7)], [1.0, Color(0.08, 0.07, 0.07, 0.0)]])
	return m


func _smoke_process(carry: Vector3) -> ParticleProcessMaterial:
	var m := ParticleProcessMaterial.new()
	m.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_SPHERE
	m.emission_sphere_radius = 10.0
	m.direction = Vector3(0, 1, 0)
	m.spread = 25.0
	m.initial_velocity_min = 6.0
	m.initial_velocity_max = 14.0
	m.gravity = Vector3(1.5, 3.0, 0.5)
	m.damping_min = 0.5
	m.damping_max = 1.2
	m.scale_min = 0.8
	m.scale_max = 1.6
	m.scale_curve = _curve([Vector2(0, 0.35), Vector2(0.4, 1.2), Vector2(1, 2.4)])
	m.angle_min = 0.0
	m.angle_max = 360.0
	m.color_ramp = _ramp([[0.0, Color(0.18, 0.16, 0.15, 0.0)], [0.08, Color(0.16, 0.15, 0.14, 0.85)], [0.6, Color(0.32, 0.31, 0.31, 0.55)], [1.0, Color(0.45, 0.45, 0.46, 0.0)]])
	return m


func _spray_process() -> ParticleProcessMaterial:
	var m := ParticleProcessMaterial.new()
	m.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_SPHERE
	m.emission_sphere_radius = 8.0
	m.direction = Vector3(0, 1, 0)
	m.spread = 22.0
	m.initial_velocity_min = 25.0
	m.initial_velocity_max = 55.0
	m.gravity = Vector3(0, -9.8, 0)
	m.damping_min = 1.0
	m.damping_max = 3.0
	m.scale_min = 0.7
	m.scale_max = 1.5
	m.angle_min = 0.0
	m.angle_max = 360.0
	m.scale_curve = _curve([Vector2(0, 0.4), Vector2(0.5, 1.2), Vector2(1, 2.0)])
	m.color_ramp = _ramp([[0.0, Color(0.95, 0.97, 1.0, 0.95)], [0.6, Color(0.9, 0.93, 0.96, 0.6)], [1.0, Color(0.9, 0.93, 0.96, 0.0)]])
	return m


func _debris_process(carry: Vector3) -> ParticleProcessMaterial:
	var m := ParticleProcessMaterial.new()
	m.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_SPHERE
	m.emission_sphere_radius = 3.0
	m.direction = Vector3(0, 1, 0)
	m.spread = 70.0
	m.initial_velocity_min = 25.0
	m.initial_velocity_max = 70.0
	m.gravity = Vector3(0, -9.8, 0)
	m.scale_min = 0.6
	m.scale_max = 1.4
	m.color_ramp = _ramp([[0.0, Color(1.0, 0.85, 0.5, 1.0)], [0.7, Color(1.0, 0.4, 0.1, 1.0)], [1.0, Color(0.3, 0.1, 0.05, 0.0)]])
	return m


func _process(delta: float) -> void:
	_t += delta
	if _light:
		_light.light_energy = 0.0 if _prewarm_only else 12.0 * exp(-_t * 2.5)
		if _t > 2.5:
			_light.queue_free()
			_light = null
	if _t > LIFETIME or (_prewarm_only and _t > 0.6):
		queue_free()
