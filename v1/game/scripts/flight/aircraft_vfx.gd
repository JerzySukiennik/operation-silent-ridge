# Aircraft VFX: afterburner plume with shock diamonds, nozzle glow, heat haze, wingtip vortex ribbons, wing/LEX condensation, transonic vapour cone and the crash explosion.
class_name AircraftVfx
extends Node3D

const AB_SHADER := preload("res://shaders/vfx/afterburner.gdshader")
const GLOW_SHADER := preload("res://shaders/vfx/engine_glow.gdshader")
const HAZE_SHADER := preload("res://shaders/vfx/heat_haze.gdshader")
const VAPOUR_SHADER := preload("res://shaders/vfx/vapour.gdshader")
const TRAIL_SHADER := preload("res://shaders/vfx/vortex_trail.gdshader")
const TRAIL_LIFE := 0.9
const TRAIL_MAX := 72

static var _noise: NoiseTexture2D
static var _mats: Dictionary = {}

var aircraft: Aircraft
var _plume_core: MeshInstance3D
var _plume_env: MeshInstance3D
var _glow: MeshInstance3D
var _haze: MeshInstance3D
var _wing_vapour: Array[MeshInstance3D] = []
var _lex: Array[MeshInstance3D] = []
var _cone: MeshInstance3D
var _trail_mesh: ImmediateMesh
var _trail_node: MeshInstance3D
var _trails: Array = [[], []]
var _tip_local: Array[Vector3] = []
var _seed := 0.0
var _nozzle := Transform3D.IDENTITY
var _prewarm := 0


static func noise_texture() -> NoiseTexture2D:
	if _noise == null:
		var n := FastNoiseLite.new()
		n.noise_type = FastNoiseLite.TYPE_SIMPLEX_SMOOTH
		n.frequency = 0.012
		n.fractal_octaves = 4
		_noise = NoiseTexture2D.new()
		_noise.width = 256
		_noise.height = 256
		_noise.seamless = true
		_noise.generate_mipmaps = true
		_noise.noise = n
	return _noise


static func shared_material(key: String, shader: Shader, params: Dictionary = {}) -> ShaderMaterial:
	if _mats.has(key):
		return _mats[key]
	var m := ShaderMaterial.new()
	m.shader = shader
	for k: String in params:
		m.set_shader_parameter(k, params[k])
	_mats[key] = m
	return m


func setup(a: Aircraft) -> void:
	aircraft = a
	_seed = float(a.peer_id % 97) * 0.173 + randf()
	_nozzle = a.marker("Nozzle")
	var tip_l := a.marker("WingtipL").origin
	var tip_r := a.marker("WingtipR").origin
	var canopy := a.marker("Canopy").origin
	_tip_local = [tip_l, tip_r]

	var core_mat := shared_material("ab_core", AB_SHADER, {"layer": 0.0, "brightness": 1.9, "diamonds": 4.0})
	var env_mat := shared_material("ab_env", AB_SHADER, {"layer": 1.0, "brightness": 0.6, "diamonds": 2.0})
	core_mat.render_priority = 2
	env_mat.render_priority = 1
	_plume_core = _mesh_child(cone_mesh(0.42, 0.16, 4.4, 20, 16), core_mat, _nozzle)
	_plume_env = _mesh_child(cone_mesh(0.56, 0.44, 8.0, 20, 12), env_mat, _nozzle)
	var quad := QuadMesh.new()
	quad.size = Vector2(1.15, 1.15)
	var glow_mat := shared_material("glow", GLOW_SHADER)
	glow_mat.render_priority = 3
	var haze_mat := shared_material("haze", HAZE_SHADER)
	haze_mat.render_priority = -2
	_glow = _mesh_child(quad, glow_mat, _nozzle * Transform3D(Basis.IDENTITY, Vector3(0, 0, -0.35)))
	_haze = _mesh_child(cone_mesh(0.55, 1.7, 16.0, 16, 6), haze_mat, _nozzle * Transform3D(Basis.IDENTITY, Vector3(0, 0, 0.3)))

	var noise := noise_texture()
	var wing_mat := shared_material("vap_wing", VAPOUR_SHADER, {"noise_tex": noise, "scroll": 2.8, "tiling": Vector2(1.4, 0.45), "edge_soft": 0.35, "front_bias": 1.4, "coverage": 0.6, "opacity": 0.95})
	var lex_mat := shared_material("vap_lex", VAPOUR_SHADER, {"noise_tex": noise, "scroll": 3.5, "tiling": Vector2(2.0, 1.2), "edge_soft": 0.0, "front_bias": 0.8, "fresnel_mode": 0.5, "coverage": 0.75, "opacity": 0.9})
	var cone_mat := shared_material("vap_cone", VAPOUR_SHADER, {"noise_tex": noise, "scroll": 0.8, "tiling": Vector2(6.0, 0.5), "edge_soft": 0.0, "front_bias": 1.8, "fresnel_mode": 1.0, "coverage": 1.0, "opacity": 0.7})
	for i in 2:
		var tip: Vector3 = _tip_local[i]
		var s := signf(tip.x)
		# three stacked sheets read as a vapour volume rising off the upper wing and streaming past the trailing edge
		for layer in 3:
			var h := 0.12 + layer * 0.32
			var trail := 1.2 + layer * 0.9
			var sheet := wing_sheet_mesh(
				Vector3(tip.x * 0.28, tip.y + h + 0.1, tip.z - 5.3 + layer * 0.3), Vector3(tip.x * 0.28, tip.y + h * 1.3 + 0.1, tip.z + 0.4 + trail),
				Vector3(tip.x * (0.9 - layer * 0.08), tip.y + h * 0.7, tip.z - 2.3 + layer * 0.3), Vector3(tip.x * (0.9 - layer * 0.08), tip.y + h * 0.9, tip.z + trail * 0.8))
			var mi := _mesh_child(sheet, wing_mat, Transform3D.IDENTITY)
			mi.set_meta("layer_gain", [1.0, 0.7, 0.45][layer])
			mi.set_meta("seed", float(layer) * 1.7 + float(i) * 0.9)
			_wing_vapour.append(mi)
		var a0 := Vector3(s * 1.05, canopy.y - 0.5, canopy.z + 1.2)
		var a1 := Vector3(s * 2.7, canopy.y - 0.1, tip.z + 1.5)
		_lex.append(_mesh_child(cone_mesh(0.1, 0.95, a0.distance_to(a1), 14, 8), lex_mat, _axis_transform(a0, a1)))
	var cz := canopy.z + 3.0
	_cone = _mesh_child(cone_mesh(1.7, 4.4, 3.6, 32, 6), cone_mat, Transform3D(Basis.IDENTITY, Vector3(0, canopy.y - 0.95, cz)))

	_trail_mesh = ImmediateMesh.new()
	_trail_node = MeshInstance3D.new()
	_trail_node.name = "VortexTrails"
	_trail_node.top_level = true
	_trail_node.mesh = _trail_mesh
	_trail_node.material_override = shared_material("trail", TRAIL_SHADER)
	_trail_node.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(_trail_node)
	_trail_node.global_transform = Transform3D.IDENTITY
	reset()
	_prewarm = 3
	if a.is_local:
		FlightExplosion.prewarm(a)


func _axis_transform(a: Vector3, b: Vector3) -> Transform3D:
	var z := (b - a).normalized()
	var ref := Vector3.UP if absf(z.y) < 0.95 else Vector3.RIGHT
	var x := ref.cross(z).normalized()
	var y := z.cross(x)
	return Transform3D(Basis(x, y, z), a)


func _mesh_child(mesh: Mesh, mat: Material, xf: Transform3D) -> MeshInstance3D:
	var mi := MeshInstance3D.new()
	mi.mesh = mesh
	mi.material_override = mat
	mi.transform = xf
	mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	mi.gi_mode = GeometryInstance3D.GI_MODE_DISABLED
	mi.visible = false
	add_child(mi)
	return mi


static func cone_mesh(r0: float, r1: float, length: float, seg: int, rings: int) -> ArrayMesh:
	# open cone along +Z from r0 at z=0 to r1 at z=length; UV.x around, UV.y along
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	var slope := (r0 - r1) / length
	for i in rings:
		for k in seg:
			var quad := [[i, k], [i, k + 1], [i + 1, k + 1], [i, k], [i + 1, k + 1], [i + 1, k]]
			for c: Array in quad:
				var v := float(c[0]) / rings
				var u := float(c[1]) / seg
				var th := u * TAU
				var r := lerpf(r0, r1, v)
				st.set_uv(Vector2(u, v))
				st.set_normal(Vector3(cos(th), sin(th), slope).normalized())
				st.add_vertex(Vector3(cos(th) * r, sin(th) * r, v * length))
	return st.commit()


static func wing_sheet_mesh(root_le: Vector3, root_te: Vector3, tip_le: Vector3, tip_te: Vector3) -> ArrayMesh:
	# vapour sheet hugging the upper wing; UV.x root->tip, UV.y leading->trailing edge, slight arch
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	var nu := 6
	var nv := 6
	for i in nu:
		for j in nv:
			for c: Array in [[i, j], [i + 1, j], [i + 1, j + 1], [i, j], [i + 1, j + 1], [i, j + 1]]:
				var u := float(c[0]) / nu
				var v := float(c[1]) / nv
				var le := root_le.lerp(tip_le, u)
				var te := root_te.lerp(tip_te, u)
				var p := le.lerp(te, v)
				p.y += sin(v * PI) * 0.18 * (1.0 - u * 0.5)
				st.set_uv(Vector2(u, v))
				st.set_normal(Vector3.UP)
				st.add_vertex(p)
	return st.commit()


func reset() -> void:
	_trails = [[], []]
	if _trail_mesh:
		_trail_mesh.clear_surfaces()


func update_effects(delta: float, p: Dictionary) -> void:
	if aircraft == null:
		return
	var alive: bool = p.alive
	var thr: float = p.thr
	var ab: float = p.ab
	var nz: float = p.nz
	var alpha: float = p.alpha
	var mach: float = p.mach
	var tas: float = p.tas
	var alt: float = p.alt
	var humid := lerpf(1.0, 0.3, smoothstep(0.0, 7000.0, alt))
	if _prewarm > 0:
		# draw every effect once with zero intensity so its pipeline compiles at spawn, not mid-manoeuvre
		_prewarm -= 1
		for mi in [_plume_core, _plume_env, _glow]:
			mi.visible = true
			mi.set_instance_shader_parameter("ab", 0.0)
		_haze.visible = true
		_haze.set_instance_shader_parameter("strength", 0.0)
		for mi in [_cone] + _wing_vapour + _lex:
			mi.visible = true
			mi.set_instance_shader_parameter("intensity", 0.0)
		return

	var ab_vis := ab if alive else 0.0
	_set_vis(_plume_core, ab_vis > 0.01)
	_set_vis(_plume_env, ab_vis > 0.01)
	if ab_vis > 0.01:
		var stretch := 0.55 + 0.55 * ab_vis
		var spread := 1.0 + 0.35 * smoothstep(2000.0, 12000.0, alt)
		for mi in [_plume_core, _plume_env]:
			mi.transform = _nozzle * Transform3D(Basis.from_scale(Vector3(spread, spread, stretch)), Vector3.ZERO)
			mi.set_instance_shader_parameter("ab", clampf(ab_vis * 1.2, 0.0, 1.0))
			mi.set_instance_shader_parameter("seed", _seed)
	_set_vis(_glow, alive)
	if alive:
		_glow.set_instance_shader_parameter("power", thr)
		_glow.set_instance_shader_parameter("ab", ab_vis)
	var haze := (smoothstep(0.3, 1.0, thr) * 0.6 + ab_vis) if alive else 0.0
	_set_vis(_haze, haze > 0.02)
	if haze > 0.02:
		_haze.set_instance_shader_parameter("strength", haze)

	var g_term := smoothstep(4.5, 7.2, nz)
	var wing_i := humid * g_term * smoothstep(110.0, 200.0, tas) * (1.0 if alive else 0.0)
	for mi in _wing_vapour:
		_set_vis(mi, wing_i > 0.01)
		if wing_i > 0.01:
			mi.set_instance_shader_parameter("intensity", wing_i * float(mi.get_meta("layer_gain", 1.0)))
			mi.set_instance_shader_parameter("seed", _seed + float(mi.get_meta("seed", 0.0)))
	var lex_i := humid * clampf(smoothstep(deg_to_rad(9.0), deg_to_rad(22.0), alpha) + g_term * 0.6, 0.0, 1.0) * smoothstep(55.0, 110.0, tas) * (1.0 if alive else 0.0)
	for mi in _lex:
		_set_vis(mi, lex_i > 0.01)
		if lex_i > 0.01:
			mi.set_instance_shader_parameter("intensity", lex_i * 0.8)
			mi.set_instance_shader_parameter("seed", _seed + 3.0)
	var cone_i := (1.0 - smoothstep(0.0, 0.075, absf(mach - 0.98))) + smoothstep(3.0, 6.0, nz) * (1.0 - smoothstep(0.0, 0.08, absf(mach - 0.9))) * 0.7
	cone_i = clampf(cone_i, 0.0, 1.0) * humid * (1.0 if alive else 0.0)
	_set_vis(_cone, cone_i > 0.01)
	if cone_i > 0.01:
		_cone.set_instance_shader_parameter("intensity", cone_i * 0.85)
		_cone.set_instance_shader_parameter("seed", _seed)

	var trail_i := humid * clampf(smoothstep(3.0, 6.5, nz) + smoothstep(deg_to_rad(11.0), deg_to_rad(24.0), alpha) * 0.8, 0.0, 1.0) * smoothstep(60.0, 120.0, tas)
	if not alive:
		trail_i = 0.0
	_update_trails(delta, trail_i)


func _set_vis(mi: MeshInstance3D, v: bool) -> void:
	if mi.visible != v:
		mi.visible = v


func _update_trails(delta: float, intensity: float) -> void:
	var now := Time.get_ticks_msec() * 0.001
	var xf := aircraft.global_transform
	var any := false
	for i in 2:
		var pts: Array = _trails[i]
		while not pts.is_empty() and now - pts[0][1] > TRAIL_LIFE:
			pts.pop_front()
		var wp: Vector3 = xf * _tip_local[i]
		if intensity > 0.02 or (not pts.is_empty() and pts[pts.size() - 1][2] > 0.02):
			if pts.is_empty() or (pts[pts.size() - 1][0] as Vector3).distance_to(wp) > 2.5:
				pts.append([wp, now, intensity])
				if pts.size() > TRAIL_MAX:
					pts.pop_front()
		if pts.size() >= 2:
			any = true
	_trail_mesh.clear_surfaces()
	_trail_node.visible = any
	if not any:
		return
	var cam := get_viewport().get_camera_3d()
	var cam_pos := cam.global_transform.origin if cam else xf.origin + Vector3.UP * 10.0
	_trail_mesh.surface_begin(Mesh.PRIMITIVE_TRIANGLES)
	for i in 2:
		var pts: Array = _trails[i]
		var n := pts.size()
		if n < 2:
			continue
		var live_tip: Vector3 = xf * _tip_local[i]
		var prev_l := Vector3.ZERO
		var prev_r := Vector3.ZERO
		var prev_age := 0.0
		var prev_a := 0.0
		for j in range(n, -1, -1):
			var pos: Vector3
			var age: float
			var a: float
			if j == n:
				pos = live_tip
				age = 0.0
				a = intensity
			else:
				pos = pts[j][0]
				age = clampf((now - pts[j][1]) / TRAIL_LIFE, 0.0, 1.0)
				a = pts[j][2]
			var nxt: Vector3 = pts[maxi(j - 1, 0)][0] if j > 0 else pos + (pos - (pts[1][0] as Vector3))
			var tangent := (pos - nxt)
			if tangent.length_squared() < 1e-6:
				tangent = xf.basis.z
			var side := tangent.cross(cam_pos - pos).normalized()
			var w := 0.07 + age * 0.45
			var l := pos - side * w
			var r := pos + side * w
			if j < n:
				_trail_mesh.surface_set_color(Color(1, 1, 1, prev_a))
				_trail_mesh.surface_set_uv(Vector2(0, prev_age))
				_trail_mesh.surface_add_vertex(prev_l)
				_trail_mesh.surface_set_color(Color(1, 1, 1, prev_a))
				_trail_mesh.surface_set_uv(Vector2(1, prev_age))
				_trail_mesh.surface_add_vertex(prev_r)
				_trail_mesh.surface_set_color(Color(1, 1, 1, a))
				_trail_mesh.surface_set_uv(Vector2(1, age))
				_trail_mesh.surface_add_vertex(r)
				_trail_mesh.surface_set_color(Color(1, 1, 1, prev_a))
				_trail_mesh.surface_set_uv(Vector2(0, prev_age))
				_trail_mesh.surface_add_vertex(prev_l)
				_trail_mesh.surface_set_color(Color(1, 1, 1, a))
				_trail_mesh.surface_set_uv(Vector2(1, age))
				_trail_mesh.surface_add_vertex(r)
				_trail_mesh.surface_set_color(Color(1, 1, 1, a))
				_trail_mesh.surface_set_uv(Vector2(0, age))
				_trail_mesh.surface_add_vertex(l)
			prev_l = l
			prev_r = r
			prev_age = age
			prev_a = a
	_trail_mesh.surface_end()


func explode(at: Vector3, velocity: Vector3) -> void:
	var e := FlightExplosion.new()
	var root := aircraft.get_parent() if aircraft.get_parent() else aircraft
	root.add_child(e)
	e.global_position = at
	var water := aircraft.surface_height(at.x, at.z) <= 0.01 and at.y < 3.0
	e.start(velocity, water)
	reset()
