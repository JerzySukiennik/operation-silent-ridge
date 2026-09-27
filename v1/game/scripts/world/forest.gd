# Conifer forest around the focus: near mesh trees placed on the CPU per streamed tile (worker threads), mid-range impostors placed on the GPU.
extends Node3D

const TREES := "res://assets/world/trees/"
const CELL := 8.0
const NEAR_TILE_CELLS := 16          # near tiles are 128 m, streamed around the focus
const NEAR_RADIUS := 420.0           # mesh trees fade out 350..410 m
const FAR_TILE_CELLS := 32
const FAR_TILES := 18                # 18x18 GPU-placed billboard tiles -> impostors to ~2.3 km
const M32 := 0xffffffff

var world: World
var mesh_material_needles: ShaderMaterial
var mesh_material_bark: ShaderMaterial
var billboard_material: ShaderMaterial
var _tree_mesh: ArrayMesh
var _far: Array[MultiMeshInstance3D] = []
var _near := {}          # Vector2i tile -> MultiMeshInstance3D (null while computing)
var _done := {}          # Vector2i tile -> PackedFloat32Array (filled by workers)
var _tasks := {}         # Vector2i tile -> task id
var _mutex := Mutex.new()
var _mask: PackedByteArray
var _mask_size := 0
var _treeline := 1550.0
var _heights: HeightField
var _focus := Vector3.ZERO


func setup(w: World, hm: Texture2D) -> void:
	world = w
	_heights = w.heights
	_treeline = w.treeline
	var masks_a: Texture2D = load("res://assets/world/terrain/masks_a.png")
	var masks_b: Texture2D = load("res://assets/world/terrain/masks_b.png")
	var img := masks_b.get_image()
	if img.is_compressed():
		img.decompress()
	img.convert(Image.FORMAT_RGBA8)
	_mask = img.get_data()
	_mask_size = img.get_width()
	mesh_material_needles = _material("res://shaders/world/tree_mesh.gdshader", hm, masks_a, masks_b)
	mesh_material_needles.set_shader_parameter("albedo_tex", load(TREES + "fir_twig_albedo.png"))
	mesh_material_bark = mesh_material_needles.duplicate()
	mesh_material_bark.set_shader_parameter("albedo_tex", load(TREES + "fir_bark_albedo.png"))
	mesh_material_bark.set_shader_parameter("is_bark", true)
	_tree_mesh = _load_tree_mesh().duplicate() as ArrayMesh
	_tree_mesh.surface_set_material(0, mesh_material_needles)
	if _tree_mesh.get_surface_count() > 1:
		_tree_mesh.surface_set_material(1, mesh_material_bark)
	billboard_material = _material("res://shaders/world/tree_billboard.gdshader", hm, masks_a, masks_b)
	billboard_material.set_shader_parameter("atlas_albedo", load(TREES + "fir_billboard_albedo.png"))
	billboard_material.set_shader_parameter("atlas_normal", load(TREES + "fir_billboard_normal.png"))
	billboard_material.set_shader_parameter("tile_cells", FAR_TILE_CELLS)
	for mat in [mesh_material_needles, mesh_material_bark]:
		mat.set_shader_parameter("fade_start", NEAR_RADIUS - 70.0)
		mat.set_shader_parameter("fade_end", NEAR_RADIUS - 10.0)
	billboard_material.set_shader_parameter("fade_in_start", NEAR_RADIUS - 70.0)
	billboard_material.set_shader_parameter("fade_in_end", NEAR_RADIUS - 10.0)
	var far_r := FAR_TILES * FAR_TILE_CELLS * CELL * 0.5
	billboard_material.set_shader_parameter("far_start", far_r - 300.0)
	billboard_material.set_shader_parameter("far_end", far_r - 40.0)
	_far = _make_gpu_tiles(_quad(), FAR_TILES, billboard_material)


func debug_stats() -> String:
	var n := 0
	var tiles := 0
	for k in _near:
		var mmi: MultiMeshInstance3D = _near[k]
		if mmi and mmi.multimesh:
			n += mmi.multimesh.instance_count
			tiles += 1
	return "%d/%dtiles/%dpending" % [n, tiles, _tasks.size()]


func far_distance() -> float:
	return FAR_TILES * FAR_TILE_CELLS * CELL * 0.5 - 150.0


func update_focus(p: Vector3) -> void:
	_focus = p
	for mat in [mesh_material_needles, mesh_material_bark, billboard_material]:
		mat.set_shader_parameter("focus", p)
	var cx := floori(p.x / CELL) - FAR_TILES * FAR_TILE_CELLS / 2
	var cz := floori(p.z / CELL) - FAR_TILES * FAR_TILE_CELLS / 2
	for i in _far.size():
		_far[i].position = Vector3((cx + (i % FAR_TILES) * FAR_TILE_CELLS) * CELL, 0.0, (cz + (i / FAR_TILES) * FAR_TILE_CELLS) * CELL)
	_stream_near(p)


# ---------------------------------------------------------------- near trees (CPU placement, exact match of tree_at())

func _stream_near(p: Vector3) -> void:
	var span := NEAR_TILE_CELLS * CELL
	var r := NEAR_RADIUS + span * 0.75
	var t0 := Vector2i(floori((p.x - r) / span), floori((p.z - r) / span))
	var t1 := Vector2i(floori((p.x + r) / span), floori((p.z + r) / span))
	var want := {}
	for tz in range(t0.y, t1.y + 1):
		for tx in range(t0.x, t1.x + 1):
			var cxz := Vector2((tx + 0.5) * span, (tz + 0.5) * span)
			if cxz.distance_to(Vector2(p.x, p.z)) < r:
				want[Vector2i(tx, tz)] = true
	for k in _near.keys():
		if not want.has(k) and not _tasks.has(k):
			var node: MultiMeshInstance3D = _near[k]
			if node:
				node.queue_free()
			_near.erase(k)
	var started := 0
	for k in want.keys():
		if not _near.has(k) and started < 4:
			_near[k] = null
			var key: Vector2i = k
			_tasks[k] = WorkerThreadPool.add_task(_compute_tile.bind(key))
			started += 1
	# collect finished tiles
	_mutex.lock()
	var ready := _done.duplicate()
	_done.clear()
	_mutex.unlock()
	for k in ready.keys():
		WorkerThreadPool.wait_for_task_completion(_tasks[k])
		_tasks.erase(k)
		if not _near.has(k):
			continue
		var buf: PackedFloat32Array = ready[k]
		if buf.is_empty():
			_near[k] = _empty_marker()
			continue
		var mm := MultiMesh.new()
		mm.transform_format = MultiMesh.TRANSFORM_3D
		mm.mesh = _tree_mesh
		mm.instance_count = buf.size() / 12
		mm.buffer = buf
		var mmi := MultiMeshInstance3D.new()
		mmi.multimesh = mm
		mmi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_ON
		add_child(mmi)
		_near[k] = mmi


func _empty_marker() -> MultiMeshInstance3D:
	var mmi := MultiMeshInstance3D.new()
	add_child(mmi)
	return mmi


func _compute_tile(tile: Vector2i) -> void:
	var buf := PackedFloat32Array()
	var c0 := tile * NEAR_TILE_CELLS
	for j in NEAR_TILE_CELLS:
		for i in NEAR_TILE_CELLS:
			var t := tree_at(Vector2i(c0.x + i, c0.y + j))
			if t.is_empty():
				continue
			var p: Vector3 = t.pos
			var sc: float = t.scale
			var rnd: Vector4 = t.rnd
			var a := rnd.x * TAU
			var ca := cos(a)
			var sa := sin(a)
			var sxz := sc * (0.9 + 0.25 * rnd.y)
			# Basis columns (ca,0,-sa), (0,1,0), (sa,0,ca) scaled -> row-major 3x4 buffer layout
			buf.append_array([ca * sxz, 0.0, sa * sxz, p.x, 0.0, sc, 0.0, p.y, -sa * sxz, 0.0, ca * sxz, p.z])
	_mutex.lock()
	_done[tile] = buf
	_mutex.unlock()


static func _thash(x: int) -> int:
	x &= M32
	x ^= x >> 16
	x = (x * 0x7feb352d) & M32
	x ^= x >> 15
	x = (x * 0x846ca68b) & M32
	x ^= x >> 16
	return x


static func _h01(h: int) -> float:
	return float(h & 0xffffff) / 16777215.0


## Returns {} or {pos, scale, rnd}; mirrors tree_at() in tree_common.gdshaderinc.
func tree_at(c: Vector2i) -> Dictionary:
	var h := _thash(((c.x & M32) * 0x8da6b343) & M32 ^ _thash((((c.y & M32) * 0xd8163841) + 0x9e3779b9) & M32))
	var rnd := Vector4(_h01(_thash(h + 1)), _h01(_thash(h + 2)), _h01(_thash(h + 3)), _h01(_thash(h + 4)))
	var jit := Vector2(_h01(h), _h01(_thash(h + 5))) - Vector2(0.5, 0.5)
	var p := (Vector2(c) + Vector2(0.5, 0.5) + jit * 0.85) * CELL
	var dens := _mask_r(p)
	if dens * 1.15 < rnd.w:
		return {}
	var y := _heights.height(p.x, p.y)
	if y < 3.0:
		return {}
	dens *= smoothstep(_treeline + 60.0, _treeline - 140.0, y)
	if dens * 1.15 < rnd.w:
		return {}
	var sc := (0.55 + 0.55 * rnd.z) * lerpf(0.55, 1.0, smoothstep(_treeline + 50.0, _treeline - 500.0, y))
	return {"pos": Vector3(p.x, y - 0.4, p.y), "scale": sc, "rnd": rnd}


func _mask_r(p: Vector2) -> float:
	var n := _mask_size
	var u := ((p.x + _heights.half) / _heights.cell + 0.5) / float(_heights.size) * n - 0.5
	var v := ((p.y + _heights.half) / _heights.cell + 0.5) / float(_heights.size) * n - 0.5
	var i := clampi(floori(u), 0, n - 2)
	var j := clampi(floori(v), 0, n - 2)
	var fu := clampf(u - i, 0.0, 1.0)
	var fv := clampf(v - j, 0.0, 1.0)
	var a := _mask[(j * n + i) * 4]
	var b := _mask[(j * n + i + 1) * 4]
	var c := _mask[((j + 1) * n + i) * 4]
	var d := _mask[((j + 1) * n + i + 1) * 4]
	return lerpf(lerpf(a, b, fu), lerpf(c, d, fu), fv) / 255.0


# ---------------------------------------------------------------- shared setup

func _material(path: String, hm: Texture2D, ma: Texture2D, mb: Texture2D) -> ShaderMaterial:
	var mat := ShaderMaterial.new()
	mat.shader = load(path)
	world._common_uniforms(mat, hm)
	mat.set_shader_parameter("forest_mask", mb)
	mat.set_shader_parameter("sun_mask", ma)
	mat.set_shader_parameter("cell_size", CELL)
	mat.set_shader_parameter("treeline", world.treeline)
	mat.set_shader_parameter("snowline", world.snowline)
	return mat


func _make_gpu_tiles(mesh: Mesh, n: int, mat: Material) -> Array[MultiMeshInstance3D]:
	var count := FAR_TILE_CELLS * FAR_TILE_CELLS
	var buf := PackedFloat32Array([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0])
	while buf.size() < count * 12:
		buf.append_array(buf)
	buf.resize(count * 12)
	var out: Array[MultiMeshInstance3D] = []
	var span := FAR_TILE_CELLS * CELL
	for i in n * n:
		var mm := MultiMesh.new()
		mm.transform_format = MultiMesh.TRANSFORM_3D
		mm.mesh = mesh
		mm.instance_count = count
		mm.buffer = buf
		mm.custom_aabb = AABB(Vector3(-12.0, -1100.0, -12.0), Vector3(span + 24.0, 5300.0, span + 24.0))
		var mmi := MultiMeshInstance3D.new()
		mmi.multimesh = mm
		mmi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		mmi.material_override = mat
		add_child(mmi)
		out.append(mmi)
	return out


func _quad() -> ArrayMesh:
	var arr := []
	arr.resize(Mesh.ARRAY_MAX)
	arr[Mesh.ARRAY_VERTEX] = PackedVector3Array([Vector3(-0.5, 0, 0), Vector3(0.5, 0, 0), Vector3(0.5, 1, 0), Vector3(-0.5, 1, 0)])
	arr[Mesh.ARRAY_NORMAL] = PackedVector3Array([Vector3.BACK, Vector3.BACK, Vector3.BACK, Vector3.BACK])
	arr[Mesh.ARRAY_TEX_UV] = PackedVector2Array([Vector2(0, 1), Vector2(1, 1), Vector2(1, 0), Vector2(0, 0)])
	arr[Mesh.ARRAY_INDEX] = PackedInt32Array([0, 2, 1, 0, 3, 2])
	var m := ArrayMesh.new()
	m.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arr)
	return m


func _load_tree_mesh() -> ArrayMesh:
	var scene: PackedScene = load(TREES + "fir.glb")
	var root := scene.instantiate()
	var mi := _find_mesh(root)
	var mesh: ArrayMesh = mi.mesh if mi else null
	root.free()
	return mesh


func _find_mesh(n: Node) -> MeshInstance3D:
	if n is MeshInstance3D:
		return n
	for c in n.get_children():
		var r := _find_mesh(c)
		if r:
			return r
	return null
