# World root: heightfield queries (headless-safe), terrain/ocean clipmaps, forest, sky, sun and fog for the 80x80 km map.
class_name World
extends Node3D

const SEA_LEVEL := 0.0
const TEX := "res://assets/world/textures/"

## set false to skip all rendering setup (automatic when running headless)
@export var visual := true
@export var forest_enabled := true
## lowest altitude (m) of continuous snow cover and the upper forest limit, shared by terrain and trees
@export var snowline := 520.0
@export var treeline := 1550.0

var heights := HeightField.new()
var terrain: Clipmap
var terrain_shadow: Clipmap
var ocean: Clipmap
var forest: Node3D
var sun: DirectionalLight3D
var environment: Environment
var terrain_material: ShaderMaterial
var ocean_material: ShaderMaterial
var _focus := Vector3.ZERO
var _focus_set := false


func _init() -> void:
	heights.load_default()


func _ready() -> void:
	if DisplayServer.get_name() == "headless":
		visual = false
	var args: Dictionary = Engine.get_meta("boot_args", {}) if Engine.has_meta("boot_args") else {}
	if args.has("world_forest"):
		forest_enabled = str(args.world_forest) != "0"
	if not visual:
		return
	_build_environment()
	var hm := _height_texture()
	_build_terrain(hm)
	_build_ocean(hm)
	if forest_enabled:
		forest = preload("res://scripts/world/forest.gd").new()
		forest.name = "Forest"
		add_child(forest)
		forest.setup(self, hm)
		terrain_material.set_shader_parameter("tree_far", forest.far_distance())
	set_focus(Vector3.ZERO)


func _process(_delta: float) -> void:
	if not visual or _focus_set:
		_focus_set = false
		return
	var cam := get_viewport().get_camera_3d()
	if cam:
		_apply_focus(cam.global_position)


# ---------------------------------------------------------------- public API

## Terrain height (m) of the rendered surface (exact for the finest clipmap ring around the focus; < 0.5 m p99 elsewhere).
func height_at(x: float, z: float) -> float:
	return heights.height(x, z)


## Smooth bilinear terrain height, ~3x cheaper than height_at — use for long line-of-sight / radar sweeps.
func height_at_fast(x: float, z: float) -> float:
	return heights.height_bilinear(x, z)


func surface_at(x: float, z: float) -> float:
	return maxf(heights.height(x, z), SEA_LEVEL)


func normal_at(x: float, z: float) -> Vector3:
	return heights.normal(x, z)


## Air-start transforms near the carrier: 600 m, echelon, facing the canyon mouth (-Z forward).
func spawn_points() -> Array[Transform3D]:
	var out: Array[Transform3D] = []
	var c := WorldLayout.CARRIER
	var to_coast := (WorldLayout.CANYON_MOUTH - c)
	to_coast.y = 0.0
	to_coast = to_coast.normalized()
	var right := to_coast.cross(Vector3.UP).normalized()
	var basis := Basis.looking_at(to_coast, Vector3.UP)
	for i in 4:
		var p := c + to_coast * 1500.0 + right * (float(i) * 120.0 - 180.0) - to_coast * float(i) * 60.0
		p.y = 600.0 + float(i) * 15.0
		out.append(Transform3D(basis, p))
	return out


func carrier_transform() -> Transform3D:
	var h := deg_to_rad(WorldLayout.CARRIER_HEADING_DEG)
	var fwd := Vector3(sin(h), 0.0, -cos(h))
	return Transform3D(Basis.looking_at(fwd, Vector3.UP), WorldLayout.CARRIER)


func target_position() -> Vector3:
	return WorldLayout.TARGET


## Canyon centreline every 200 m from the fjord mouth to the target valley; y = terrain floor (0 over water).
func canyon_path() -> PackedVector3Array:
	return PackedVector3Array(WorldLayout.CANYON_PATH)


func set_focus(world_pos: Vector3) -> void:
	_focus_set = true
	_apply_focus(world_pos)


func sun_direction() -> Vector3:
	return WorldLayout.TO_SUN


# ---------------------------------------------------------------- build

func _apply_focus(p: Vector3) -> void:
	_focus = p
	if terrain:
		terrain.update_focus(p)
		terrain_shadow.update_focus(p)
		ocean.update_focus(p)
	if forest:
		forest.update_focus(p)


func _height_texture() -> ImageTexture:
	var img := heights.image.duplicate() as Image
	img.generate_mipmaps()
	return ImageTexture.create_from_image(img)


func _tex(n: String) -> Texture2D:
	return load(TEX + n)


func _common_uniforms(m: ShaderMaterial, hm: Texture2D) -> void:
	m.set_shader_parameter("height_map", hm)
	m.set_shader_parameter("hm_size", heights.size)
	m.set_shader_parameter("hm_cell", heights.cell)
	m.set_shader_parameter("hm_min", WorldLayout.HEIGHT_MIN)
	m.set_shader_parameter("hm_range", WorldLayout.HEIGHT_MAX - WorldLayout.HEIGHT_MIN)
	m.set_shader_parameter("map_half", WorldLayout.MAP_SIZE * 0.5)


func _build_terrain(hm: Texture2D) -> void:
	var m := ShaderMaterial.new()
	m.shader = preload("res://shaders/world/terrain.gdshader")
	_common_uniforms(m, hm)
	m.set_shader_parameter("masks_a", _tex("../terrain/masks_a.png"))
	m.set_shader_parameter("masks_b", _tex("../terrain/masks_b.png"))
	m.set_shader_parameter("noise_tex", _tex("noise.png"))
	for pair in [["rock_a", "rock_cliff_albedo_h"], ["rock_n", "rock_cliff_normal"], ["rockm_a", "rock_macro_albedo_h"],
			["rockm_n", "rock_macro_normal"], ["scree_a", "scree_albedo_h"], ["scree_n", "scree_normal"],
			["screem_a", "scree_macro_albedo_h"], ["snow_a", "snow_albedo_h"], ["snow_n", "snow_normal"],
			["snowm_a", "snow_macro_albedo_h"], ["ground_a", "ground_albedo_h"], ["ground_n", "ground_normal"],
			["floor_a", "forest_floor_albedo_h"], ["shore_a", "shore_albedo_h"], ["shore_n", "shore_normal"],
			["water_n", "water_normal_b"]]:
		m.set_shader_parameter(pair[0], _tex(pair[1] + ".png"))
	m.set_shader_parameter("snowline", snowline)
	m.set_shader_parameter("treeline", treeline)
	terrain_material = m
	terrain = Clipmap.new()
	terrain.name = "Terrain"
	terrain.half_cells = 128
	terrain.base_cell = heights.cell * 0.5
	terrain.level_count = 9
	var mp := terrain.morph_params()
	m.set_shader_parameter("morph_start", mp.x)
	m.set_shader_parameter("morph_len", mp.y)
	add_child(terrain)
	terrain.build(m, GeometryInstance3D.SHADOW_CASTING_SETTING_OFF)
	# coarse, slightly lowered copy that only casts shadows (canyon walls onto the jet / trees)
	var sm := m.duplicate() as ShaderMaterial
	terrain_shadow = Clipmap.new()
	terrain_shadow.name = "TerrainShadowCaster"
	terrain_shadow.half_cells = 64
	terrain_shadow.base_cell = heights.cell * 2.0
	terrain_shadow.level_count = 3
	var sp := terrain_shadow.morph_params()
	sm.set_shader_parameter("morph_start", sp.x)
	sm.set_shader_parameter("morph_len", sp.y)
	sm.set_shader_parameter("shadow_drop", 12.0)
	add_child(terrain_shadow)
	terrain_shadow.build(sm, GeometryInstance3D.SHADOW_CASTING_SETTING_SHADOWS_ONLY)


func _build_ocean(hm: Texture2D) -> void:
	var m := ShaderMaterial.new()
	m.shader = preload("res://shaders/world/ocean.gdshader")
	_common_uniforms(m, hm)
	m.set_shader_parameter("wave_a", _tex("water_normal_a.png"))
	m.set_shader_parameter("wave_b", _tex("water_normal_b.png"))
	m.set_shader_parameter("noise_tex", _tex("noise.png"))
	m.set_shader_parameter("to_sun", WorldLayout.TO_SUN)
	ocean_material = m
	ocean = Clipmap.new()
	ocean.name = "Ocean"
	ocean.half_cells = 64
	ocean.base_cell = 4.0
	ocean.level_count = 10
	ocean.y_range = Vector2(-12.0, 12.0)
	var mp := ocean.morph_params()
	m.set_shader_parameter("morph_start", mp.x)
	m.set_shader_parameter("morph_len", mp.y)
	add_child(ocean)
	ocean.build(m, GeometryInstance3D.SHADOW_CASTING_SETTING_OFF)


func _build_environment() -> void:
	var env := Environment.new()
	env.background_mode = Environment.BG_SKY
	var sky := Sky.new()
	var psm := PhysicalSkyMaterial.new()
	psm.rayleigh_coefficient = 2.0
	psm.rayleigh_color = Color(0.26, 0.41, 0.72)
	psm.mie_coefficient = 0.005
	psm.mie_eccentricity = 0.76
	psm.mie_color = Color(0.69, 0.73, 0.8)
	psm.turbidity = 6.5
	psm.sun_disk_scale = 1.0
	psm.ground_color = Color(0.28, 0.32, 0.38)
	psm.energy_multiplier = 1.4
	sky.sky_material = psm
	sky.radiance_size = Sky.RADIANCE_SIZE_256
	sky.process_mode = Sky.PROCESS_MODE_QUALITY
	env.sky = sky
	env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	env.ambient_light_sky_contribution = 1.0
	env.reflected_light_source = Environment.REFLECTION_SOURCE_SKY
	env.tonemap_mode = Environment.TONE_MAPPER_AGX
	env.tonemap_exposure = 1.15
	env.ambient_light_energy = 1.25
	env.fog_enabled = true
	env.fog_mode = Environment.FOG_MODE_EXPONENTIAL
	env.fog_light_color = Color(0.72, 0.8, 0.92)
	env.fog_light_energy = 1.0
	env.fog_sun_scatter = 0.18
	env.fog_density = 0.00002
	env.fog_aerial_perspective = 0.6
	env.fog_sky_affect = 0.25
	env.fog_height = 600.0
	env.fog_height_density = 0.00012
	env.glow_enabled = true
	env.glow_intensity = 0.25
	env.glow_bloom = 0.03
	env.glow_hdr_threshold = 1.2
	env.adjustment_enabled = true
	env.adjustment_saturation = 1.05
	environment = env
	var we := WorldEnvironment.new()
	we.name = "WorldEnvironment"
	we.environment = env
	add_child(we)
	sun = DirectionalLight3D.new()
	sun.name = "Sun"
	sun.transform = Transform3D(Basis.looking_at(-WorldLayout.TO_SUN, Vector3.UP), Vector3.ZERO)
	sun.light_color = Color(1.0, 0.97, 0.92)
	sun.light_energy = 2.1
	sun.light_angular_distance = 0.0  # PCSS noise shows as dithering at FSR scale; PCF filter instead
	sun.shadow_enabled = true
	sun.directional_shadow_mode = DirectionalLight3D.SHADOW_PARALLEL_4_SPLITS
	sun.directional_shadow_max_distance = 3500.0
	sun.directional_shadow_split_1 = 0.03
	sun.directional_shadow_split_2 = 0.1
	sun.directional_shadow_split_3 = 0.35
	sun.directional_shadow_blend_splits = true
	sun.directional_shadow_fade_start = 0.85
	sun.shadow_bias = 0.05
	sun.shadow_normal_bias = 1.0
	sun.shadow_blur = 1.0
	add_child(sun)
