# World test: hero shots + a 250 m/s, 60 m AGL camera run along canyon_path(); saves JPGs to --shots and prints REPORT perf lines, then quits.
extends Node3D

const SPEED := 250.0
const AGL := 60.0
const FLIGHT_S := 36.0
const SETTLE_FRAMES := 40

var world: World
var cam: Camera3D
var shots_dir := "user://"
var _path: PackedVector3Array
var _cum: PackedFloat32Array
var _phase := 0
var _frames := 0
var _t := 0.0
var _dts: PackedFloat32Array = []
var _heroes: Array = []
var _flight_start_s := 8200.0
var _snap_next := 4.5  # first flight frame still shows the previous hero shot
var _t_boot := 0.0
var quick := false
var _gpu: PackedFloat32Array = []
var _jet: Node3D
var _cpu: PackedFloat32Array = []
var _proc: PackedFloat32Array = []


func _ready() -> void:
	var args: Dictionary = Engine.get_meta("boot_args", {}) if Engine.has_meta("boot_args") else {}
	shots_dir = str(args.get("shots", "user://"))
	quick = args.has("quick")
	# measure raw frame cost: no vsync cap during the benchmark
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	RenderingServer.viewport_set_measure_render_time(get_viewport().get_viewport_rid(), true)
	_t_boot = Time.get_ticks_msec() / 1000.0
	world = preload("res://scenes/world/world.tscn").instantiate()
	add_child(world)
	cam = Camera3D.new()
	cam.fov = 68.0
	cam.near = 0.5
	cam.far = 150000.0
	add_child(cam)
	cam.make_current()
	_path = world.canyon_path()
	_cum = PackedFloat32Array([0.0])
	for i in range(1, _path.size()):
		_cum.append(_cum[i - 1] + Vector2(_path[i].x - _path[i - 1].x, _path[i].z - _path[i - 1].z).length())
	_heroes = _hero_list()
	_jet = _make_jet_proxy()
	_jet.visible = false
	add_child(_jet)
	print("REPORT display refresh=%.1f vsync=%d max_fps=%d gpu=%s" % [DisplayServer.screen_get_refresh_rate(), DisplayServer.window_get_vsync_mode(), Engine.max_fps, RenderingServer.get_video_adapter_name()])
	print("REPORT world_load_s=%.1f canyon_len=%.0f" % [Time.get_ticks_msec() / 1000.0 - _t_boot, _cum[_cum.size() - 1]])


func _hero_list() -> Array:
	var mouth := _at(300.0)
	var ahead := _at(3000.0)
	var dir := (ahead - mouth)
	dir.y = 0.0
	dir = dir.normalized()
	var entrance := _at(8400.0)
	var ent_look := _at(9400.0)
	var inside := _at(17000.0)
	var in_look := _at(17800.0)
	var t := world.target_position()
	var tv0 := _at(_cum[_cum.size() - 1] - 400.0)
	var list := []
	list.append({"name": "ocean_30m", "pos": mouth - dir * 2600.0 + Vector3(0, 30, 0), "look": mouth + dir * 3000.0 + Vector3(0, 250, 0)})
	list.append({"name": "canyon_entrance", "pos": entrance + Vector3(0, AGL, 0), "look": ent_look + Vector3(0, AGL + 10, 0)})
	list.append({"name": "canyon_inside", "pos": inside + Vector3(0, AGL, 0), "look": in_look + Vector3(0, AGL, 0)})
	list.append({"name": "overview_4000m", "pos": mouth + Vector3(-6000, 4000, 9000), "look": mouth + Vector3(14000, 600, -6000)})
	list.append({"name": "target_valley", "pos": tv0 + Vector3(0, 420, 0), "look": t + Vector3(0, 0, 0)})
	return list


func _at(s: float) -> Vector3:
	s = clampf(s, 0.0, _cum[_cum.size() - 1] - 0.01)
	var i := _cum.bsearch(s) - 1
	i = clampi(i, 0, _path.size() - 2)
	var f := (s - _cum[i]) / maxf(_cum[i + 1] - _cum[i], 0.001)
	var p0 := _path[maxi(i - 1, 0)]
	var p1 := _path[i]
	var p2 := _path[i + 1]
	var p3 := _path[mini(i + 2, _path.size() - 1)]
	var p := 0.5 * ((2.0 * p1) + (-p0 + p2) * f + (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * f * f + (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * f * f * f)
	p.y = maxf(world.height_at(p.x, p.z), 0.0)
	return p


func _process(delta: float) -> void:
	_frames += 1
	if _frames > 2:
		_gpu.append(RenderingServer.viewport_get_measured_render_time_gpu(get_viewport().get_viewport_rid()))
		_cpu.append(RenderingServer.viewport_get_measured_render_time_cpu(get_viewport().get_viewport_rid()) + RenderingServer.get_frame_setup_time_cpu())
		_proc.append(Performance.get_monitor(Performance.TIME_PROCESS) * 1000.0)
	if _phase < _heroes.size():
		var h: Dictionary = _heroes[_phase]
		if _frames == 1:
			_place(h.pos, h.look)
			_dts.clear()
			_gpu.clear()
			_cpu.clear()
			_proc.clear()
		else:
			_dts.append(delta)
		if _frames >= SETTLE_FRAMES:
			_snap(h.name)
			_report(h.name)
			_phase += 1
			_frames = 0
			if _phase == _heroes.size() and quick:
				get_tree().quit()
		return
	if _phase == _heroes.size():
		_t += delta
		if _frames > 1:
			_dts.append(delta)
		var s := _flight_start_s + _t * SPEED
		var p := _at(s)
		var q := _at(s + 450.0)
		var agl_p := maxf(p.y + AGL, world.height_at(p.x, p.z) + 35.0)
		var agl_q := maxf(q.y + AGL, world.height_at(q.x, q.z) + 35.0)
		p.y = agl_p
		q.y = agl_q
		# chase-camera framing: proxy jet ~45 m ahead of the camera so its ground shadow shows up in shots
		var fwd := (q - p).normalized()
		_jet.visible = true
		_jet.global_position = p + fwd * 45.0 - Vector3(0, 6, 0)
		_jet.look_at(_jet.global_position + fwd, Vector3.UP)
		_place(p, q)
		if _t >= _snap_next:
			_snap("flight_%02d" % int(_t))
			_snap_next += 9.0
		if _t >= FLIGHT_S:
			_report("flight")
			print("REPORT total_runtime_s=%.1f" % (Time.get_ticks_msec() / 1000.0 - _t_boot))
			_report_process_memory()
			_phase += 1
			get_tree().quit()


func _report_process_memory() -> void:
	if OS.get_name() != "Windows":
		return
	var out := []
	OS.execute("tasklist", ["/FI", "PID eq %d" % OS.get_process_id(), "/FO", "CSV", "/NH"], out)
	var raw := str(out[0]) if out.size() > 0 else ""
	# "OperationSilentRidge.exe","1234","Console","1","1,234,567 K"
	var cols := raw.strip_edges().split("\",\"")
	var kb := cols[cols.size() - 1].replace("K", "").replace("\"", "").replace(",", "").replace(".", "").replace(" ", "").replace("\u00a0", "") if cols.size() > 0 else "0"
	print("REPORT process_ram working_set_mb=%.0f raw=%s" % [float(kb) / 1024.0, raw.strip_edges().replace("\n", " ")])


func _make_jet_proxy() -> Node3D:
	# grey stand-in the size of an F-35C (15.7 m long, 13.1 m span) — only to judge its shadow on the terrain
	var root := Node3D.new()
	var mat := StandardMaterial3D.new()
	mat.albedo_color = Color(0.16, 0.17, 0.18)
	for part in [[Vector3(2.2, 1.6, 15.7), Vector3.ZERO], [Vector3(13.1, 0.3, 5.0), Vector3(0, 0, 1.5)], [Vector3(6.5, 0.2, 2.5), Vector3(0, 0.3, 6.5)]]:
		var mi := MeshInstance3D.new()
		var bm := BoxMesh.new()
		bm.size = part[0]
		mi.mesh = bm
		mi.position = part[1]
		mi.material_override = mat
		root.add_child(mi)
	return root


var _focus_us := 0.0
var _focus_n := 0


func _place(pos: Vector3, look: Vector3) -> void:
	cam.global_position = pos
	cam.look_at(look, Vector3.UP)
	var t0 := Time.get_ticks_usec()
	world.set_focus(pos)
	_focus_us += Time.get_ticks_usec() - t0
	_focus_n += 1


func _snap(n: String) -> void:
	var img := get_viewport().get_texture().get_image()
	img.save_jpg(shots_dir.path_join(n + ".jpg"), 0.9)


func _report(tag: String) -> void:
	if _dts.is_empty():
		return
	var sorted := _dts.duplicate()
	sorted.sort()
	var sum := 0.0
	for d in _dts:
		sum += d
	var avg_dt := sum / _dts.size()
	var gsum := 0.0
	for g in _gpu:
		gsum += g
	var gpu_ms := gsum / maxf(_gpu.size(), 1.0)
	var worst := sorted[sorted.size() - 1]
	var p99 := sorted[int(floor(sorted.size() * 0.99)) - 1] if sorted.size() > 100 else worst
	var csum := 0.0
	for c in _cpu:
		csum += c
	var psum := 0.0
	for c in _proc:
		psum += c
	print("REPORT %s gpu_ms=%.2f render_cpu_ms=%.2f process_ms=%.2f set_focus_ms=%.2f trees=%s" % [tag, gpu_ms, csum / maxf(_cpu.size(), 1.0), psum / maxf(_proc.size(), 1.0), _focus_us / maxf(_focus_n, 1) / 1000.0, world.forest.debug_stats() if world.forest else "-"])
	_focus_us = 0.0
	_focus_n = 0
	print("REPORT %s frames=%d avg_fps=%.1f min_fps=%.1f low1_fps=%.1f avg_ms=%.2f max_ms=%.2f static_mb=%.0f video_mb=%.0f tex_mb=%.0f buf_mb=%.0f draws=%d prims=%d" % [
		tag, _dts.size(), 1.0 / avg_dt, 1.0 / worst, 1.0 / p99, avg_dt * 1000.0, worst * 1000.0,
		Performance.get_monitor(Performance.MEMORY_STATIC) / 1048576.0,
		Performance.get_monitor(Performance.RENDER_VIDEO_MEM_USED) / 1048576.0,
		Performance.get_monitor(Performance.RENDER_TEXTURE_MEM_USED) / 1048576.0,
		Performance.get_monitor(Performance.RENDER_BUFFER_MEM_USED) / 1048576.0,
		Performance.get_monitor(Performance.RENDER_TOTAL_DRAW_CALLS_IN_FRAME),
		Performance.get_monitor(Performance.RENDER_TOTAL_PRIMITIVES_IN_FRAME)])
