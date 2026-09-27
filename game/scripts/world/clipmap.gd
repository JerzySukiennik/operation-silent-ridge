# Geometry clipmap: nested, focus-centred rings of flat grid tiles; the shader displaces them and geomorphs ring edges (terrain + ocean).
class_name Clipmap
extends Node3D

## ring half width in cells; each ring is 4x4 tiles of half_cells/2 cells
var half_cells := 128
## cells of each level >= 1 that stay covered by the next finer level (hole), leaves a 2-cell overlap margin
var hole := 62
var base_cell := 4.8828125
var level_count := 8
var y_range := Vector2(-1100.0, 4200.0)
var _levels: Array[Node3D] = []
var _cells: PackedFloat32Array = []

static var _mesh_cache := {}


## Morph constants for the shader: vertices must be fully collapsed onto the coarser grid (H-6 cells) where the next ring overlaps.
func morph_params() -> Vector2:
	var w := half_cells / 8.0
	return Vector2(half_cells - 6 - w, w)


func build(mat: Material, cast: GeometryInstance3D.ShadowCastingSetting, layer_mask := 1) -> void:
	hole = half_cells / 2 - 2
	for lv in level_count:
		var node := Node3D.new()
		node.name = "L%d" % lv
		add_child(node)
		_levels.append(node)
		_cells.append(base_cell * pow(2.0, lv))
		for ty in 4:
			for tx in 4:
				var inner := tx in [1, 2] and ty in [1, 2]
				var m := _tile_mesh(tx, ty, inner and lv > 0)
				if m == null:
					continue
				var mi := MeshInstance3D.new()
				mi.mesh = m
				mi.material_override = mat
				mi.cast_shadow = cast
				mi.layers = layer_mask
				mi.extra_cull_margin = 0.0
				node.add_child(mi)
	update_focus(Vector3.ZERO)


func update_focus(p: Vector3) -> void:
	for lv in _levels.size():
		var s := _cells[lv]
		var snap := 2.0 * s
		var ox := floorf(p.x / snap) * snap
		var oz := floorf(p.z / snap) * snap
		_levels[lv].transform = Transform3D(Basis.from_scale(Vector3(s, 1.0, s)), Vector3(ox, 0.0, oz))


func _tile_mesh(tx: int, ty: int, with_hole: bool) -> ArrayMesh:
	var key := "%d_%d_%d_%s_%s" % [half_cells, tx, ty, with_hole, y_range]
	if _mesh_cache.has(key):
		return _mesh_cache[key]
	var tile := half_cells / 2
	var x0 := -half_cells + tx * tile
	var z0 := -half_cells + ty * tile
	var verts := PackedVector3Array()
	var idx := PackedInt32Array()
	var vid := {}
	for j in tile:
		for i in tile:
			var gx := x0 + i
			var gz := z0 + j
			if with_hole and gx >= -hole and gx + 1 <= hole and gz >= -hole and gz + 1 <= hole:
				continue
			var q := [Vector2i(gx, gz), Vector2i(gx + 1, gz), Vector2i(gx, gz + 1), Vector2i(gx + 1, gz + 1)]
			var ids := []
			for c: Vector2i in q:
				if not vid.has(c):
					vid[c] = verts.size()
					verts.append(Vector3(c.x, 0.0, c.y))
				ids.append(vid[c])
			# clockwise front faces seen from above, diagonal (gx,gz)-(gx+1,gz+1) everywhere (needed for exact morph collapse)
			idx.append_array([ids[0], ids[1], ids[3], ids[0], ids[3], ids[2]])
	if idx.is_empty():
		_mesh_cache[key] = null
		return null
	var arr := []
	arr.resize(Mesh.ARRAY_MAX)
	arr[Mesh.ARRAY_VERTEX] = verts
	var nrm := PackedVector3Array()
	nrm.resize(verts.size())
	nrm.fill(Vector3.UP)
	arr[Mesh.ARRAY_NORMAL] = nrm
	arr[Mesh.ARRAY_INDEX] = idx
	var m := ArrayMesh.new()
	m.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arr)
	m.custom_aabb = AABB(Vector3(x0 - 1, y_range.x, z0 - 1), Vector3(tile + 2, y_range.y - y_range.x, tile + 2))
	_mesh_cache[key] = m
	return m
