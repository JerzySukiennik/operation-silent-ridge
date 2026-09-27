# Stand-in world for flight tests: ocean plane, box "mountains" and a canyon corridor, sky/sun/fog, with the World height_at/surface_at interface.
extends Node3D

const SEA_LEVEL := 0.0
const SEA_FLOOR := -40.0

# [centre x, centre z, size x, height, size z, yaw deg]
const BOXES := [
	[6000.0, -1800.0, 2600.0, 1500.0, 1800.0, 20.0],
	[8200.0, 2600.0, 3000.0, 2100.0, 2200.0, -15.0],
	[11000.0, -300.0, 2400.0, 1700.0, 3000.0, 35.0],
	[3500.0, 3800.0, 1500.0, 900.0, 1500.0, 50.0],
	[4200.0, -4600.0, 2000.0, 1200.0, 1400.0, -30.0],
	[14000.0, 3500.0, 3500.0, 2500.0, 2500.0, 10.0],
	[-2500.0, 6500.0, 1800.0, 700.0, 1800.0, 25.0],
	# canyon corridor walls along +X, 180 m wide
	[5200.0, 95.0, 5600.0, 380.0, 10.0, 0.0],
	[5200.0, -95.0, 5600.0, 380.0, 10.0, 0.0],
]

var _boxes: Array = []


func _init() -> void:
	for b: Array in BOXES:
		var yaw := deg_to_rad(b[5])
		_boxes.append({"c": Vector2(b[0], b[1]), "half": Vector2(b[2], b[4]) * 0.5, "h": b[3], "cos": cos(yaw), "sin": sin(yaw), "yaw": yaw})


func build() -> void:
	var env := WorldEnvironment.new()
	var e := Environment.new()
	e.background_mode = Environment.BG_SKY
	var sky := Sky.new()
	var psky := ProceduralSkyMaterial.new()
	psky.sky_top_color = Color(0.22, 0.42, 0.75)
	psky.sky_horizon_color = Color(0.66, 0.76, 0.88)
	psky.ground_horizon_color = Color(0.5, 0.6, 0.7)
	psky.ground_bottom_color = Color(0.12, 0.2, 0.3)
	psky.sun_angle_max = 20.0
	sky.sky_material = psky
	e.sky = sky
	e.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	e.reflected_light_source = Environment.REFLECTION_SOURCE_SKY
	e.tonemap_mode = Environment.TONE_MAPPER_ACES
	e.tonemap_exposure = 0.95
	e.glow_enabled = true
	e.glow_intensity = 0.6
	e.glow_bloom = 0.05
	e.glow_hdr_threshold = 1.2
	e.fog_enabled = true
	e.fog_light_color = Color(0.68, 0.76, 0.86)
	e.fog_density = 0.00005
	e.fog_aerial_perspective = 0.6
	e.fog_sky_affect = 0.25
	env.environment = e
	add_child(env)

	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-40.0, -130.0, 0.0)
	sun.light_energy = 1.25
	sun.shadow_enabled = true
	sun.directional_shadow_mode = DirectionalLight3D.SHADOW_PARALLEL_2_SPLITS
	sun.directional_shadow_max_distance = 600.0
	add_child(sun)

	var ocean := MeshInstance3D.new()
	var plane := PlaneMesh.new()
	plane.size = Vector2(90000.0, 90000.0)
	ocean.mesh = plane
	var om := StandardMaterial3D.new()
	om.albedo_color = Color(0.03, 0.12, 0.2)
	om.roughness = 0.12
	om.metallic_specular = 0.6
	var wn := FastNoiseLite.new()
	wn.frequency = 0.05
	var nt := NoiseTexture2D.new()
	nt.seamless = true
	nt.as_normal_map = true
	nt.bump_strength = 3.0
	nt.width = 256
	nt.height = 256
	nt.noise = wn
	om.normal_enabled = true
	om.normal_texture = nt
	om.normal_scale = 0.6
	om.uv1_scale = Vector3(900.0, 900.0, 1.0)
	ocean.material_override = om
	ocean.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(ocean)

	var rock := StandardMaterial3D.new()
	rock.albedo_color = Color(0.86, 0.88, 0.92)
	rock.roughness = 0.9
	var rn := NoiseTexture2D.new()
	var rnn := FastNoiseLite.new()
	rnn.frequency = 0.02
	rn.noise = rnn
	rn.seamless = true
	rock.albedo_texture = rn
	rock.uv1_triplanar = true
	rock.uv1_scale = Vector3(0.004, 0.004, 0.004)
	var wall := rock.duplicate() as StandardMaterial3D
	wall.albedo_color = Color(0.55, 0.52, 0.5)
	wall.uv1_scale = Vector3(0.02, 0.02, 0.02)
	for b: Dictionary in _boxes:
		var mi := MeshInstance3D.new()
		var bm := BoxMesh.new()
		bm.size = Vector3(b.half.x * 2.0, b.h - SEA_FLOOR, b.half.y * 2.0)
		mi.mesh = bm
		mi.material_override = wall if b.half.y < 20.0 else rock
		mi.position = Vector3(b.c.x, (b.h + SEA_FLOOR) * 0.5, b.c.y)
		mi.rotation.y = b.yaw
		add_child(mi)


func height_at(x: float, z: float) -> float:
	var h := SEA_FLOOR
	for b: Dictionary in _boxes:
		var dx: float = x - b.c.x
		var dz: float = z - b.c.y
		var lx: float = dx * b.cos - dz * b.sin
		var lz: float = dx * b.sin + dz * b.cos
		if absf(lx) <= b.half.x and absf(lz) <= b.half.y:
			h = maxf(h, b.h)
	return h


func surface_at(x: float, z: float) -> float:
	return maxf(height_at(x, z), SEA_LEVEL)


func normal_at(_x: float, _z: float) -> Vector3:
	return Vector3.UP


func set_focus(_p: Vector3) -> void:
	pass
