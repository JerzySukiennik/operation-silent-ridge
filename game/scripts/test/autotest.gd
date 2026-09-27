# Integration autotest (--autotest): ocean spawn shot, 45 s autopilot canyon run with the real model, world, HUD and audio, a deliberate crash + respawn; REPORT lines with frame-time stats and memory, then quit.
extends RefCounted

const CANYON_TIME := 45.0
const SHOT_EVERY := 5.0

var g: Node
var jet: Aircraft
var world: World
var ap := ControlInput.new()
var path: PackedVector3Array
var path_i := 0
var lever := 0.85
var phase := "spawn"
var t := 0.0
var total := 0.0
var shot_t := 0.0
var shots := 0
var frames: PackedFloat32Array = []
var crashed_seen := false
var respawned_seen := false
var dir := ""
var hitches := 0
var net_t := 0.0


func _init(game: Node) -> void:
	g = game
	jet = game.jet
	world = game.world
	path = world.canyon_path()
	dir = str(game.args.get("shots", "user://"))
	Controls.override = ap
	if not game.args.has("vsync"):
		DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	jet.crashed.connect(func(_p): crashed_seen = true)
	ap.throttle = 0.85
	print("REPORT autotest start gpu=%s path_points=%d" % [RenderingServer.get_video_adapter_name(), path.size()])


func step(delta: float) -> void:
	t += delta
	total += delta
	match phase:
		"spawn":
			ap.pitch = 0.0
			ap.roll = 0.0
			if t > 3.0:
				_shot("spawn_ocean")
				_start_canyon()
		"canyon":
			if t > 1.5:
				frames.append(delta)
				if delta > 0.05 and shot_t > 0.2:
					hitches += 1
					if hitches <= 25:
						print("REPORT hitch t=%.2f ms=%.0f path_i=%d pos=%s" % [t, delta * 1000.0, path_i, jet.global_position.round()])
			var pr := _canyon_pilot()
			ap.pitch = pr.x
			ap.roll = pr.y
			ap.throttle = minf(lever, 1.0)
			ap.afterburner = clampf(lever - 1.0, 0.0, 1.0)
			shot_t += delta
			if shot_t > SHOT_EVERY:
				shot_t = 0.0
				_shot("canyon_%02d" % shots)
				shots += 1
			if t > float(g.args.get("canyon_time", CANYON_TIME)):
				_report_perf()
				phase = "crash"
				t = 0.0
		"crash":
			ap.pitch = -1.0
			ap.roll = 0.0
			if crashed_seen and t > 1.2 and shots < 100:
				_shot("crash")
				shots = 100
			if crashed_seen and jet.alive and not respawned_seen:
				respawned_seen = true
				t = 0.0
				phase = "after"
		"after":
			ap.pitch = 0.0
			if t > 2.0:
				_shot("respawned")
				print("REPORT crash_detected=%s respawned=%s total_s=%.1f" % [crashed_seen, respawned_seen, total])
				print("REPORT AUTOTEST DONE")
				g.get_tree().quit()
	net_t += delta
	if net_t > 10.0:
		net_t = 0.0
		var far := -1.0
		for id in g.remotes:
			far = jet.global_position.distance_to(g.remotes[id].global_position)
		print("REPORT net t=%.0f state=%s peers=%d remotes=%d dist_to_remote=%.0f" % [total, Net.state, Net.names.size(), g.remotes.size(), far])
	if total > 65.0 + float(g.args.get("canyon_time", CANYON_TIME)):
		print("REPORT AUTOTEST TIMEOUT phase=%s" % phase)
		g.get_tree().quit()


func _start_canyon() -> void:
	var i := 0
	for k in path.size():
		if world.height_at(path[k].x, path[k].z) > 5.0:
			i = maxi(k - 2, 0)
			break
	path_i = i
	var a := path[i]
	var b := path[mini(i + 2, path.size() - 1)]
	var d := Vector3(b.x - a.x, 0.0, b.z - a.z).normalized()
	var p := a
	p.y = world.surface_at(a.x, a.z) + 90.0
	jet.respawn(Transform3D(Basis.looking_at(d, Vector3.UP), p), 215.0)
	lever = 0.85
	phase = "canyon"
	t = 0.0


func _canyon_pilot() -> Vector2:
	var m := jet.model
	var pos := m.position
	var best := path_i
	var best_d := INF
	for k in range(path_i, mini(path_i + 12, path.size())):
		var d := Vector2(path[k].x - pos.x, path[k].z - pos.z).length()
		if d < best_d:
			best_d = d
			best = k
	path_i = best
	if path_i >= path.size() - 4:
		_start_canyon()
		return Vector2.ZERO
	var look := path[mini(path_i + 3, path.size() - 1)]
	var to := Vector3(look.x - pos.x, 0.0, look.z - pos.z).normalized()
	var fwd := Vector3(m.velocity.x, 0.0, m.velocity.z).normalized()
	var err := atan2(fwd.cross(to).y * -1.0, fwd.dot(to))
	var omega := clampf(err * 1.6, -0.3, 0.3)
	var bank_des := clampf(atan(omega * m.tas / 9.81), deg_to_rad(-78.0), deg_to_rad(78.0))
	var roll := clampf((bank_des - m.bank) * 2.5 - m.rates.z * 0.2, -1.0, 1.0)
	var floor_h := 0.0
	for k in 8:
		var q := pos + m.velocity * (k * 0.2)
		floor_h = maxf(floor_h, world.surface_at(q.x, q.z))
	var gamma_des := clampf((floor_h + 70.0 - pos.y) * 0.012, -0.12, 0.3)
	var cb := cos(m.bank)
	var n0 := cos(m.path_angle) * (1.0 / cb if cb >= 0.5 else 4.0 * cb)
	var n_need := cos(m.path_angle) / maxf(cb, 0.2)
	var ff := (n_need - n0) / maxf(7.5 - n0, 0.5) if n_need > n0 else 0.0
	var pitch := clampf(ff + (gamma_des - m.path_angle) * 3.0 - m.rates.x * 0.3, -0.7, 1.0)
	lever = clampf(lever + (215.0 - m.tas) * 0.0015, 0.3, 1.6)
	return Vector2(pitch, roll)


func _report_perf() -> void:
	var s := Array(frames)
	s.sort()
	var n := s.size()
	var sum := 0.0
	for f in s:
		sum += f
	var low1 := 0.0
	var k := maxi(int(n * 0.01), 1)
	for j in k:
		low1 += s[n - 1 - j]
	low1 /= k
	print("REPORT canyon hitches=%d frames=%d avg_fps=%.1f low1_fps=%.1f min_fps=%.1f p50_ms=%.1f vram_mb=%.0f static_mb=%.0f alive=%s" % [
		hitches, n, n / maxf(sum, 0.001), 1.0 / maxf(low1, 0.0001), 1.0 / maxf(s[n - 1], 0.0001), s[n / 2] * 1000.0,
		Performance.get_monitor(Performance.RENDER_VIDEO_MEM_USED) / 1e6, Performance.get_monitor(Performance.MEMORY_STATIC) / 1e6, jet.alive])
	var tel := jet.telemetry()
	print("REPORT canyon_end ias=%.0f radar_alt_ft=%.0f path_i=%d/%d" % [tel.ias_kt, tel.radar_alt_ft, path_i, path.size()])


func _shot(name: String) -> void:
	g.get_viewport().get_texture().get_image().save_png(dir.path_join(name + ".png"))
