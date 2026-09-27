# Offline tool: builds the placeholder F-35C visual (lofted chined fuselage, trapezoid wing, canted tails, control-surface pivots, gear, markers) and saves scenes/aircraft/f35c_visual.tscn.
extends SceneTree

const OUT_SCENE := "res://scenes/aircraft/f35c_visual.tscn"
const CG_STATION := 8.3          # m from the nose tip to the centre of gravity (origin)
const LENGTH := 15.7             # F-35C length
const SEMISPAN := 6.55           # 43 ft span / 2

# Fuselage stations from the nose: [station, half width, top y, bottom y, chine y, top exponent, bottom exponent] [estimated from F-35C 3-views]
const BODY := [
	[0.00, 0.02, 0.02, -0.04, -0.03, 2.2, 3.0],
	[0.35, 0.17, 0.10, -0.15, -0.08, 2.2, 3.0],
	[0.90, 0.34, 0.24, -0.30, -0.10, 2.2, 3.0],
	[1.70, 0.54, 0.40, -0.46, -0.10, 2.3, 3.2],
	[2.60, 0.70, 0.54, -0.60, -0.08, 2.4, 3.4],
	[3.50, 0.82, 0.64, -0.70, -0.05, 2.6, 3.8],
	[4.40, 0.98, 0.71, -0.76, -0.03, 3.0, 4.2],
	[5.10, 1.46, 0.76, -0.79, -0.02, 3.6, 4.8],
	[5.80, 1.58, 0.81, -0.81, -0.01, 3.8, 5.0],
	[6.60, 1.72, 0.86, -0.82, 0.0, 3.6, 5.0],
	[7.60, 1.86, 0.88, -0.82, 0.01, 3.2, 4.6],
	[9.00, 1.90, 0.86, -0.78, 0.0, 3.0, 4.2],
	[10.40, 1.82, 0.80, -0.72, -0.01, 2.8, 4.0],
	[11.60, 1.64, 0.72, -0.64, -0.02, 2.6, 3.6],
	[12.60, 1.36, 0.62, -0.56, -0.02, 2.4, 3.2],
	[13.50, 1.00, 0.52, -0.48, -0.02, 2.2, 2.8],
	[14.30, 0.76, 0.44, -0.40, -0.01, 2.0, 2.4],
	[14.75, 0.68, 0.38, -0.36, 0.0, 2.0, 2.2],
]
# Canopy / spine fairing: [station, half width, height above the body top]
const CANOPY := [
	[2.70, 0.04, 0.00],
	[3.10, 0.30, 0.24],
	[3.80, 0.43, 0.44],
	[4.60, 0.47, 0.56],
	[5.40, 0.46, 0.46],
	[6.20, 0.42, 0.24],
	[7.00, 0.36, 0.10],
	[7.80, 0.26, 0.0],
]
const CANOPY_GLASS_END := 5.85

var mat_skin: StandardMaterial3D
var mat_skin_dark: StandardMaterial3D
var mat_glass: StandardMaterial3D
var mat_nozzle: StandardMaterial3D
var mat_black: StandardMaterial3D
var mat_gear: StandardMaterial3D
var mat_tire: StandardMaterial3D
var obj_lines: PackedStringArray = []
var obj_vcount := 0


func _init() -> void:
	_make_materials()
	var root := Node3D.new()
	root.name = "F35CVisual"
	var body := MeshInstance3D.new()
	body.name = "Body"
	body.mesh = _build_body_mesh()
	_add(root, body)

	_add_pivot_surface(root, "ElevonR", _stab_planform(1.0), true)
	_add_pivot_surface(root, "ElevonL", _stab_planform(-1.0), true)
	_add_wing_surface(root, "FlaperonR", 1.0, 1.95, 4.55)
	_add_wing_surface(root, "FlaperonL", -1.0, 1.95, 4.55)
	_add_wing_surface(root, "AileronR", 1.0, 4.75, 6.25)
	_add_wing_surface(root, "AileronL", -1.0, 4.75, 6.25)
	_add_rudder(root, "RudderR", 1.0)
	_add_rudder(root, "RudderL", -1.0)
	_add_gear(root)

	_marker(root, "Nozzle", Vector3(0.0, 0.0, LENGTH - CG_STATION))
	_marker(root, "WingtipL", Vector3(-SEMISPAN, -0.05, _z(11.7)))
	_marker(root, "WingtipR", Vector3(SEMISPAN, -0.05, _z(11.7)))
	_marker(root, "Canopy", Vector3(0.0, 1.08, _z(4.1)))

	var scene := PackedScene.new()
	var err := scene.pack(root)
	if err == OK:
		err = ResourceSaver.save(scene, OUT_SCENE)
	print("REPORT build_f35c_visual save=%s verts=%d" % [error_string(err), obj_vcount])
	var obj_path := str(OS.get_environment("OSR_OBJ_OUT"))
	if obj_path != "":
		var f := FileAccess.open(obj_path, FileAccess.WRITE)
		f.store_string("\n".join(obj_lines))
		f.close()
	root.free()
	quit()


func _add(parent: Node, child: Node) -> void:
	parent.add_child(child)
	child.owner = parent if parent.owner == null else parent.owner


func _marker(root: Node3D, n: String, p: Vector3) -> void:
	var m := Marker3D.new()
	m.name = n
	m.position = p
	_add(root, m)


func _z(station: float) -> float:
	return station - CG_STATION


func _make_materials() -> void:
	mat_skin = StandardMaterial3D.new()
	mat_skin.albedo_color = Color(0.34, 0.36, 0.38)
	mat_skin.roughness = 0.58
	mat_skin.metallic = 0.12
	mat_skin.metallic_specular = 0.45
	mat_skin_dark = mat_skin.duplicate()
	mat_skin_dark.albedo_color = Color(0.29, 0.305, 0.32)
	mat_glass = StandardMaterial3D.new()
	mat_glass.albedo_color = Color(0.42, 0.33, 0.14)
	mat_glass.metallic = 0.55
	mat_glass.roughness = 0.05
	mat_glass.metallic_specular = 1.0
	mat_glass.clearcoat_enabled = true
	mat_glass.clearcoat = 1.0
	mat_glass.clearcoat_roughness = 0.02
	mat_nozzle = StandardMaterial3D.new()
	mat_nozzle.albedo_color = Color(0.30, 0.29, 0.28)
	mat_nozzle.metallic = 0.55
	mat_nozzle.roughness = 0.42
	mat_black = StandardMaterial3D.new()
	mat_black.albedo_color = Color(0.03, 0.03, 0.035)
	mat_black.roughness = 0.9
	mat_gear = StandardMaterial3D.new()
	mat_gear.albedo_color = Color(0.82, 0.83, 0.84)
	mat_gear.roughness = 0.5
	mat_tire = StandardMaterial3D.new()
	mat_tire.albedo_color = Color(0.06, 0.06, 0.06)
	mat_tire.roughness = 0.85


# ---------------------------------------------------------------- helpers

func _interp(table: Array, s: float, col: int) -> float:
	# monotone cubic (Fritsch-Carlson) through the station table
	var n := table.size()
	if s <= table[0][0]:
		return table[0][col]
	if s >= table[n - 1][0]:
		return table[n - 1][col]
	var i := 0
	while i < n - 2 and s > table[i + 1][0]:
		i += 1
	var x0: float = table[i][0]
	var x1: float = table[i + 1][0]
	var y0: float = table[i][col]
	var y1: float = table[i + 1][col]
	var h := x1 - x0
	var d := (y1 - y0) / h
	var m0 := _slope(table, i, col)
	var m1 := _slope(table, i + 1, col)
	if absf(d) < 1e-9:
		m0 = 0.0
		m1 = 0.0
	else:
		var a := m0 / d
		var b := m1 / d
		if a < 0.0:
			m0 = 0.0
		if b < 0.0:
			m1 = 0.0
		var r := a * a + b * b
		if r > 9.0:
			var t := 3.0 / sqrt(r)
			m0 = t * a * d
			m1 = t * b * d
	var u := (s - x0) / h
	var u2 := u * u
	var u3 := u2 * u
	return (2 * u3 - 3 * u2 + 1) * y0 + (u3 - 2 * u2 + u) * h * m0 + (-2 * u3 + 3 * u2) * y1 + (u3 - u2) * h * m1


func _slope(table: Array, i: int, col: int) -> float:
	var n := table.size()
	if i == 0:
		return (table[1][col] - table[0][col]) / (table[1][0] - table[0][0])
	if i == n - 1:
		return (table[n - 1][col] - table[n - 2][col]) / (table[n - 1][0] - table[n - 2][0])
	var d0: float = (table[i][col] - table[i - 1][col]) / (table[i][0] - table[i - 1][0])
	var d1: float = (table[i + 1][col] - table[i][col]) / (table[i + 1][0] - table[i][0])
	if d0 * d1 <= 0.0:
		return 0.0
	return 0.5 * (d0 + d1)


func _tri(st: SurfaceTool, a: Vector3, b: Vector3, c: Vector3, inside: Vector3) -> void:
	# Godot front faces are clockwise: front normal = (c - a) x (b - a); orient it away from `inside`
	var n := (c - a).cross(b - a)
	if n.length_squared() < 1e-14:
		return
	var g := (a + b + c) / 3.0
	if n.dot(g - inside) < 0.0:
		var t := b
		b = c
		c = t
	st.add_vertex(a)
	st.add_vertex(b)
	st.add_vertex(c)
	obj_lines.append("v %f %f %f" % [a.x, a.y, a.z])
	obj_lines.append("v %f %f %f" % [b.x, b.y, b.z])
	obj_lines.append("v %f %f %f" % [c.x, c.y, c.z])
	obj_vcount += 3
	obj_lines.append("f %d %d %d" % [obj_vcount - 2, obj_vcount, obj_vcount - 1])


func _grid(st: SurfaceTool, pts: Array, inside_fn: Callable) -> void:
	# pts: rows of Vector3 (same length); emits two triangles per cell
	for i in pts.size() - 1:
		var r0: Array = pts[i]
		var r1: Array = pts[i + 1]
		for j in r0.size() - 1:
			var a: Vector3 = r0[j]
			var b: Vector3 = r0[j + 1]
			var c: Vector3 = r1[j + 1]
			var d: Vector3 = r1[j]
			var inside: Vector3 = inside_fn.call((a + b + c + d) * 0.25)
			_tri(st, a, b, c, inside)
			_tri(st, a, c, d, inside)


func _begin(smooth: bool) -> SurfaceTool:
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	st.set_smooth_group(0 if smooth else 0xFFFFFFFF)
	return st


func _commit(st: SurfaceTool, mesh: ArrayMesh, mat: Material) -> void:
	st.index()
	st.generate_normals()
	st.set_material(mat)
	st.commit(mesh)


# ---------------------------------------------------------------- fuselage

func _body_section(s: float) -> Array:
	var w := _interp(BODY, s, 1)
	var yt := _interp(BODY, s, 2)
	var yb := _interp(BODY, s, 3)
	var yc := _interp(BODY, s, 4)
	return [w, yt, yb, yc, _interp(BODY, s, 5), _interp(BODY, s, 6)]


func _body_top_y(x: float, s: float) -> float:
	var sec := _body_section(s)
	var au := clampf(absf(x) / sec[0], 0.0, 1.0)
	return sec[3] + (sec[1] - sec[3]) * (1.0 - pow(au, sec[4]))


func _body_bot_y(x: float, s: float) -> float:
	var sec := _body_section(s)
	var au := clampf(absf(x) / sec[0], 0.0, 1.0)
	return sec[3] - (sec[3] - sec[2]) * (1.0 - pow(au, sec[5]))


func _build_booms(mesh: ArrayMesh) -> void:
	# tail booms outboard of the engine carrying the stabilators and fins
	var st := _begin(true)
	for side in [-1.0, 1.0]:
		var rows: Array = []
		for i in 13:
			var t := float(i) / 12.0
			var s := lerpf(11.6, 15.25, t)
			var cx := 1.08
			var hw := lerpf(0.36, 0.2, t)
			var ht := lerpf(0.34, 0.16, t * t)
			var hb := lerpf(0.30, 0.14, t * t)
			var cy := -0.04
			var row: Array = []
			for k in 17:
				var th := TAU * float(k) / 16.0
				var c := cos(th)
				var sn := sin(th)
				var y := cy + (ht if sn > 0.0 else hb) * signf(sn) * pow(absf(sn), 0.7)
				row.append(Vector3(side * cx + hw * signf(c) * pow(absf(c), 0.7), y, _z(s)))
			rows.append(row)
		var ax := func(p: Vector3) -> Vector3: return Vector3(side * 1.08, -0.04, p.z)
		_grid(st, rows, ax)
		var last: Array = rows[rows.size() - 1]
		var cap := Vector3(side * 1.08, -0.04, _z(15.25))
		for k in 16:
			_tri(st, last[k], last[k + 1], cap, cap - Vector3(0, 0, 1))
	_commit(st, mesh, mat_skin)


func _body_axis(p: Vector3) -> Vector3:
	var sec := _body_section(p.z + CG_STATION)
	return Vector3(0.0, (sec[1] + sec[2]) * 0.5, p.z)


func _stations(a: float, b: float, n: int) -> PackedFloat32Array:
	var out := PackedFloat32Array()
	for i in n + 1:
		var t := float(i) / n
		t = t * t * (1.5 - 0.5 * t) if a < 0.5 else t
		out.append(lerpf(a, b, t))
	return out


func _build_body_mesh() -> ArrayMesh:
	var mesh := ArrayMesh.new()
	var ring_n := 14
	var stations := PackedFloat32Array()
	for i in 72:
		var t := float(i) / 71.0
		stations.append(14.75 * (t * t * 0.35 + t * 0.65))
	var top: Array = []
	var bottom: Array = []
	for s in stations:
		var sec := _body_section(s)
		var w: float = sec[0]
		var yt: float = sec[1]
		var yb: float = sec[2]
		var yc: float = sec[3]
		var z := _z(s)
		var rt: Array = []
		var rb: Array = []
		for k in range(-ring_n, ring_n + 1):
			var u := float(k) / ring_n
			var x := u * w
			var au := absf(u)
			rt.append(Vector3(x, yc + (yt - yc) * (1.0 - pow(au, sec[4])), z))
			rb.append(Vector3(x, yc - (yc - yb) * (1.0 - pow(au, sec[5])), z))
		top.append(rt)
		bottom.append(rb)
	var st := _begin(true)
	_grid(st, top, _body_axis)
	_commit(st, mesh, mat_skin)
	st = _begin(true)
	_grid(st, bottom, _body_axis)
	_commit(st, mesh, mat_skin_dark)

	_build_canopy(mesh)
	_build_intakes(mesh)
	_build_wing(mesh)
	_build_tails(mesh)
	_build_booms(mesh)
	_build_nozzle(mesh)
	return mesh


func _build_canopy(mesh: ArrayMesh) -> void:
	var glass := _begin(true)
	var fairing := _begin(true)
	var rows_g: Array = []
	var rows_f: Array = []
	var n := 12
	for i in 49:
		var s := lerpf(2.70, 7.8, float(i) / 48.0)
		var wc := _interp(CANOPY, s, 1)
		var hc := _interp(CANOPY, s, 2)
		var sec := _body_section(s)
		var base: float = sec[1] - 0.06
		var row: Array = []
		for k in range(0, n + 1):
			var th := PI * float(k) / n
			row.append(Vector3(cos(th) * wc, base + sin(th) * (hc + 0.06), _z(s)))
		if s <= CANOPY_GLASS_END + 0.01:
			rows_g.append(row)
		if s >= CANOPY_GLASS_END - 0.13:
			rows_f.append(row)
	var axis := func(p: Vector3) -> Vector3:
		var sec := _body_section(p.z + CG_STATION)
		return Vector3(0.0, sec[1] - 0.1, p.z)
	_grid(glass, rows_g, axis)
	_commit(glass, mesh, mat_glass)
	_grid(fairing, rows_f, axis)
	_commit(fairing, mesh, mat_skin)


func _build_intakes(mesh: ArrayMesh) -> void:
	# caret DSI intakes: a swept dark mouth on each side of the forward fuselage, duct fairing blends aft into the body
	var skin := _begin(false)
	var dark := _begin(false)
	for side in [-1.0, 1.0]:
		var rows: Array = []
		for i in 13:
			var t := float(i) / 12.0
			var s := lerpf(4.45, 5.7, t)
			var sec := _body_section(s)
			var w_in := 0.86
			var w_out: float = lerpf(1.50, sec[0] - 0.01, smoothstep(0.0, 1.0, t))
			var y_top := 0.44
			var y_bot := -0.52
			var lip := (y_top - y_bot) * 0.35 * (1.0 - t)
			var z := _z(s)
			var sink := smoothstep(0.2, 1.0, t)
			var pts := [
				Vector2(w_in, y_top), Vector2(w_out, y_top - 0.04), Vector2(w_out + 0.02, y_bot + 0.08),
				Vector2(w_out * 0.93, y_bot), Vector2(w_in, y_bot + 0.02)]
			var zoff := [-lip, -lip * 0.6, lip * 0.3, lip * 0.6, lip]
			var row: Array = []
			for k in pts.size():
				var q: Vector2 = pts[k]
				var ys: float
				if k < 2:
					ys = lerpf(q.y, _body_top_y(q.x, s) - 0.03, sink)
				elif k == 2:
					ys = lerpf(q.y, _interp(BODY, s, 4), sink)
				else:
					ys = lerpf(q.y, _body_bot_y(q.x, s) + 0.03, sink)
				row.append(Vector3(side * q.x, ys, z + zoff[k]))
			rows.append(row)
		var centre := func(p: Vector3) -> Vector3:
			return Vector3(side * 1.15, -0.1, p.z)
		_grid(skin, rows, centre)
		var r0: Array = rows[0]
		var mouth_c := Vector3.ZERO
		for p: Vector3 in r0:
			mouth_c += p
		mouth_c /= r0.size()
		var recess := mouth_c + Vector3(0.0, 0.0, 0.35)
		for j in r0.size() - 1:
			_tri(dark, r0[j], r0[j + 1], recess, recess + Vector3(0, 0, 1.0))
		_tri(dark, r0[r0.size() - 1], r0[0], recess, recess + Vector3(0, 0, 1.0))
	_commit(skin, mesh, mat_skin)
	_commit(dark, mesh, mat_black)


func _naca_half(x: float, t: float) -> float:
	return 5.0 * t * (0.2969 * sqrt(x) - 0.1260 * x - 0.3516 * x * x + 0.2843 * x * x * x - 0.1036 * x * x * x * x)


func _wing_le(x: float) -> float:
	return 5.2 + absf(x) * tan(deg_to_rad(34.0))


func _wing_te(x: float) -> float:
	return 12.4 - absf(x) / SEMISPAN * 0.62


func _slab(st: SurfaceTool, span_pts: PackedFloat32Array, le_fn: Callable, te_fn: Callable, pos_fn: Callable, t_ratio: float, c0: float, c1: float) -> void:
	# thin airfoil slab between chord fractions c0..c1; pos_fn(span, station, y_offset) -> Vector3
	var chord_n := 10
	var up: Array = []
	var lo: Array = []
	for sp in span_pts:
		var le: float = le_fn.call(sp)
		var te: float = te_fn.call(sp)
		var ru: Array = []
		var rl: Array = []
		for k in chord_n + 1:
			var f := lerpf(c0, c1, 0.5 - 0.5 * cos(PI * float(k) / chord_n))
			var s := lerpf(le, te, f)
			var h := maxf(_naca_half(f, t_ratio) * (te - le), 0.004)
			ru.append(pos_fn.call(sp, s, h))
			rl.append(pos_fn.call(sp, s, -h))
		up.append(ru)
		lo.append(rl)
	var inside_up := func(p: Vector3) -> Vector3:
		return p - _surface_normal_hint(pos_fn, p)
	var inside_lo := func(p: Vector3) -> Vector3:
		return p + _surface_normal_hint(pos_fn, p)
	_grid(st, up, inside_up)
	_grid(st, lo, inside_lo)
	# close tip, root and cut edges
	for edge in [0, span_pts.size() - 1]:
		var a: Array = up[edge]
		var b: Array = lo[edge]
		var outward := 1.0 if edge == span_pts.size() - 1 else -1.0
		for k in a.size() - 1:
			var cen: Vector3 = (a[k] + b[k + 1]) * 0.5
			var ins: Vector3 = cen - _span_dir_hint(pos_fn, span_pts, edge) * outward
			_tri(st, a[k], a[k + 1], b[k + 1], ins)
			_tri(st, a[k], b[k + 1], b[k], ins)
	if c1 < 0.999:
		for i in span_pts.size() - 1:
			var a0: Vector3 = up[i][chord_n]
			var a1: Vector3 = up[i + 1][chord_n]
			var b0: Vector3 = lo[i][chord_n]
			var b1: Vector3 = lo[i + 1][chord_n]
			var ins := (a0 + a1 + b0 + b1) * 0.25 - Vector3(0, 0, 0.3)
			_tri(st, a0, a1, b1, ins)
			_tri(st, a0, b1, b0, ins)
	if c0 > 0.001:
		for i in span_pts.size() - 1:
			var a0: Vector3 = up[i][0]
			var a1: Vector3 = up[i + 1][0]
			var b0: Vector3 = lo[i][0]
			var b1: Vector3 = lo[i + 1][0]
			var ins := (a0 + a1 + b0 + b1) * 0.25 + Vector3(0, 0, 0.3)
			_tri(st, a0, a1, b1, ins)
			_tri(st, a0, b1, b0, ins)


func _surface_normal_hint(pos_fn: Callable, p: Vector3) -> Vector3:
	var a: Vector3 = pos_fn.call(1.0, 5.0, 0.0)
	var b: Vector3 = pos_fn.call(1.0, 5.0, 0.1)
	return (b - a).normalized() * 0.05


func _span_dir_hint(pos_fn: Callable, span_pts: PackedFloat32Array, edge: int) -> Vector3:
	var a: Vector3 = pos_fn.call(span_pts[0], 8.0, 0.0)
	var b: Vector3 = pos_fn.call(span_pts[span_pts.size() - 1], 8.0, 0.0)
	return (b - a).normalized() * 0.2


func _wing_pos(side: float) -> Callable:
	return func(sp: float, s: float, h: float) -> Vector3:
		return Vector3(side * sp, -0.05 + h - sp * 0.012, _z(s))


func _span_range(a: float, b: float, n: int) -> PackedFloat32Array:
	var out := PackedFloat32Array()
	for i in n + 1:
		out.append(lerpf(a, b, float(i) / n))
	return out


func _build_wing(mesh: ArrayMesh) -> void:
	var st := _begin(true)
	var le := func(x: float) -> float: return _wing_le(x)
	var te := func(x: float) -> float: return _wing_te(x)
	for side in [-1.0, 1.0]:
		var pos := _wing_pos(side)
		_slab(st, _span_range(1.2, 1.95, 2), le, te, pos, 0.05, 0.0, 1.0)
		_slab(st, _span_range(1.95, 4.55, 5), le, te, pos, 0.048, 0.0, 0.76)
		_slab(st, _span_range(4.55, 4.75, 1), le, te, pos, 0.045, 0.0, 1.0)
		_slab(st, _span_range(4.75, 6.25, 3), le, te, pos, 0.042, 0.0, 0.74)
		_slab(st, _span_range(6.25, SEMISPAN, 1), le, te, pos, 0.04, 0.0, 1.0)
	_commit(st, mesh, mat_skin)


func _add_wing_surface(root: Node3D, n: String, side: float, a: float, b: float) -> void:
	var c0 := 0.76 if a < 4.6 else 0.74
	var hinge_a := Vector3(side * a, -0.05 - a * 0.012, _z(lerpf(_wing_le(a), _wing_te(a), c0)))
	var hinge_b := Vector3(side * b, -0.05 - b * 0.012, _z(lerpf(_wing_le(b), _wing_te(b), c0)))
	var st := _begin(true)
	var le := func(x: float) -> float: return _wing_le(x)
	var te := func(x: float) -> float: return _wing_te(x)
	_slab(st, _span_range(a, b, 4), le, te, _wing_pos(side), 0.045, c0 + 0.005, 1.0)
	_pivot_from(root, n, st, hinge_a, hinge_b, mat_skin)


func _pivot_from(root: Node3D, n: String, st: SurfaceTool, hinge_a: Vector3, hinge_b: Vector3, mat: Material) -> void:
	# pivot basis: X along the hinge (towards +x world for horizontal surfaces, upward for rudders), Z aft, Y = Z x X
	var x := (hinge_b - hinge_a).normalized()
	if x.x < -0.3 or (absf(x.x) <= 0.3 and x.y < 0.0):
		x = -x
	var zc := Vector3(0, 0, 1)
	var z := (zc - x * zc.dot(x)).normalized()
	var y := z.cross(x)
	var pivot := Node3D.new()
	pivot.name = n
	pivot.transform = Transform3D(Basis(x, y, z), (hinge_a + hinge_b) * 0.5)
	_add(root, pivot)
	var mesh := ArrayMesh.new()
	st.index()
	st.generate_normals()
	st.set_material(mat)
	var arrays := st.commit_to_arrays()
	var inv := pivot.transform.affine_inverse()
	var verts: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
	var norms: PackedVector3Array = arrays[Mesh.ARRAY_NORMAL]
	for i in verts.size():
		verts[i] = inv * verts[i]
		norms[i] = (inv.basis * norms[i]).normalized()
	arrays[Mesh.ARRAY_VERTEX] = verts
	arrays[Mesh.ARRAY_NORMAL] = norms
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	mesh.surface_set_material(0, mat)
	var mi := MeshInstance3D.new()
	mi.name = "Mesh"
	mi.mesh = mesh
	_add(pivot, mi)


func _stab_planform(side: float) -> Dictionary:
	return {"side": side}


func _stab_le(x: float) -> float:
	return 12.15 + maxf(absf(x) - 1.0, 0.0) * tan(deg_to_rad(40.0))


func _stab_te(x: float) -> float:
	return 15.05 - maxf(absf(x) - 1.0, 0.0) * 0.02


func _add_pivot_surface(root: Node3D, n: String, cfg: Dictionary, _unused: bool) -> void:
	# all-moving stabilator; pivot at ~40 % root chord
	var side: float = cfg.side
	var tip := 3.55
	var st := _begin(true)
	var le := func(x: float) -> float: return _stab_le(x)
	var te := func(x: float) -> float:
		var l := _stab_le(x)
		var tt := _stab_te(x)
		return maxf(tt, l + 0.9)
	var pos := func(sp: float, s: float, h: float) -> Vector3:
		return Vector3(side * sp, -0.08 + h - (sp - 1.0) * 0.03, _z(s))
	_slab(st, _span_range(1.0, tip, 6), le, te, pos, 0.045, 0.0, 1.0)
	var hz := _z(12.15 + 0.42 * (15.05 - 12.15))
	_pivot_from(root, n, st, Vector3(side * 1.0, -0.08, hz), Vector3(side * tip, -0.08 - (tip - 1.0) * 0.03, hz), mat_skin)


func _tail_frame(side: float) -> Array:
	# origin at the tail root line, spanwise axis canted 25° outboard
	var cant := deg_to_rad(25.0)
	var span_dir := Vector3(side * sin(cant), cos(cant), 0.0)
	var root := Vector3(side * 1.18, 0.32, 0.0)
	return [root, span_dir]


func _tail_le(h: float) -> float:
	return 11.0 + h * tan(deg_to_rad(42.0))


func _tail_te(h: float) -> float:
	return 14.15 + h * 0.06


func _tail_pos(side: float) -> Callable:
	var fr := _tail_frame(side)
	var r0: Vector3 = fr[0]
	var sd: Vector3 = fr[1]
	var nrm := Vector3(side * cos(deg_to_rad(25.0)), -sin(deg_to_rad(25.0)), 0.0)
	return func(sp: float, s: float, h: float) -> Vector3:
		return r0 + sd * sp + nrm * h + Vector3(0, 0, _z(s))


func _build_tails(mesh: ArrayMesh) -> void:
	var st := _begin(true)
	var le := func(h: float) -> float: return _tail_le(h)
	var te := func(h: float) -> float: return _tail_te(h)
	var height := 2.25
	for side in [-1.0, 1.0]:
		var pos := _tail_pos(side)
		_slab(st, _span_range(0.0, 0.35, 1), le, te, pos, 0.05, 0.0, 1.0)
		_slab(st, _span_range(0.35, height - 0.2, 6), le, te, pos, 0.05, 0.0, 0.72)
		_slab(st, _span_range(height - 0.2, height, 1), le, te, pos, 0.05, 0.0, 1.0)
	_commit(st, mesh, mat_skin)


func _add_rudder(root: Node3D, n: String, side: float) -> void:
	var height := 2.25
	var pos := _tail_pos(side)
	var le := func(h: float) -> float: return _tail_le(h)
	var te := func(h: float) -> float: return _tail_te(h)
	var st := _begin(true)
	_slab(st, _span_range(0.35, height - 0.2, 5), le, te, pos, 0.05, 0.725, 1.0)
	var ha: Vector3 = pos.call(0.35, lerpf(_tail_le(0.35), _tail_te(0.35), 0.72), 0.0)
	var hb: Vector3 = pos.call(height - 0.2, lerpf(_tail_le(height - 0.2), _tail_te(height - 0.2), 0.72), 0.0)
	_pivot_from(root, n, st, ha, hb, mat_skin)


func _build_nozzle(mesh: ArrayMesh) -> void:
	# F135 LO axisymmetric nozzle with serrated trailing edge (REFERENCES §3)
	var outer := _begin(true)
	var inner := _begin(true)
	var seg := 48
	var z0 := _z(14.55)
	var z1 := _z(LENGTH)
	var rows_o: Array = []
	var rows_i: Array = []
	for i in 7:
		var t := float(i) / 6.0
		var r := lerpf(0.66, 0.575, t)
		var ro: Array = []
		var ri: Array = []
		for k in seg + 1:
			var th := TAU * float(k) / seg
			var serr := 0.0
			if i == 6:
				var ph := fposmod(float(k) * 12.0 / seg, 1.0)
				serr = -0.07 * (1.0 - absf(ph * 2.0 - 1.0))
			var z := lerpf(z0, z1, t) + serr
			ro.append(Vector3(cos(th) * r, sin(th) * r * 0.97 + 0.0, z))
			ri.append(Vector3(cos(th) * (r - 0.05), sin(th) * (r - 0.05) * 0.97, z))
		rows_o.append(ro)
		rows_i.append(ri)
	var axis := func(p: Vector3) -> Vector3: return Vector3(0, 0, p.z)
	var axis_out := func(p: Vector3) -> Vector3:
		return Vector3(p.x, p.y, p.z) * Vector3(2.0, 2.0, 1.0)
	_grid(outer, rows_o, axis)
	_grid(inner, rows_i, axis_out)
	# lip ring and turbine face
	var last_o: Array = rows_o[6]
	var last_i: Array = rows_i[6]
	for k in seg:
		var ins: Vector3 = (last_o[k] + last_i[k]) * 0.5 - Vector3(0, 0, 0.3)
		_tri(outer, last_o[k], last_o[k + 1], last_i[k + 1], ins)
		_tri(outer, last_o[k], last_i[k + 1], last_i[k], ins)
	var face_z := _z(14.9)
	var c := Vector3(0, 0, face_z)
	for k in seg:
		var th0 := TAU * float(k) / seg
		var th1 := TAU * float(k + 1) / seg
		_tri(inner, c, Vector3(cos(th0) * 0.6, sin(th0) * 0.58, face_z), Vector3(cos(th1) * 0.6, sin(th1) * 0.58, face_z), c - Vector3(0, 0, 1))
	_commit(outer, mesh, mat_nozzle)
	_commit(inner, mesh, mat_black)


# ---------------------------------------------------------------- gear

func _cyl(st: SurfaceTool, a: Vector3, b: Vector3, r: float, seg: int) -> void:
	var ax := (b - a).normalized()
	var ref := Vector3.UP if absf(ax.y) < 0.9 else Vector3.RIGHT
	var u := ax.cross(ref).normalized()
	var v := ax.cross(u)
	for k in seg:
		var t0 := TAU * float(k) / seg
		var t1 := TAU * float(k + 1) / seg
		var d0 := (u * cos(t0) + v * sin(t0)) * r
		var d1 := (u * cos(t1) + v * sin(t1)) * r
		var mid := (a + b) * 0.5
		_tri(st, a + d0, a + d1, b + d1, mid)
		_tri(st, a + d0, b + d1, b + d0, mid)
		_tri(st, a, a + d0, a + d1, mid)
		_tri(st, b, b + d1, b + d0, mid)


func _add_gear(root: Node3D) -> void:
	var gear := Node3D.new()
	gear.name = "Gear"
	gear.visible = false
	_add(root, gear)
	var metal := _begin(true)
	var rubber := _begin(true)
	var nz := _z(3.35)
	_cyl(metal, Vector3(0, -0.6, nz), Vector3(0, -1.85, nz + 0.25), 0.09, 10)
	_cyl(metal, Vector3(0, -1.1, nz - 0.1), Vector3(0, -1.8, nz + 0.25), 0.05, 8)
	for sx in [-0.2, 0.2]:
		_cyl(rubber, Vector3(sx - 0.09, -1.88, nz + 0.25), Vector3(sx + 0.09, -1.88, nz + 0.25), 0.30, 16)
	for side in [-1.0, 1.0]:
		var mz := _z(9.3)
		var top := Vector3(side * 1.45, -0.55, mz)
		var hub := Vector3(side * 1.72, -1.72, mz + 0.2)
		_cyl(metal, top, hub, 0.11, 10)
		_cyl(metal, Vector3(side * 1.2, -0.6, mz - 0.5), hub + Vector3(0, 0.4, 0), 0.05, 8)
		_cyl(rubber, hub + Vector3(-0.17, 0, 0), hub + Vector3(0.17, 0, 0), 0.43, 18)
	var mesh := ArrayMesh.new()
	_commit(metal, mesh, mat_gear)
	_commit(rubber, mesh, mat_tire)
	var mi := MeshInstance3D.new()
	mi.name = "GearMesh"
	mi.mesh = mesh
	_add(gear, mi)
