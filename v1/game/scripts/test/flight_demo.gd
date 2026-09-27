# Flight demo/autotest: stand-in world, local F-35C flown by a scripted autopilot (cruise, 7 g turn, supersonic dash with 3 interpolated wingmen, 30 m canyon pass, gear-down slow pass, crash); saves chase-cam shots and REPORT lines.
extends Node3D

const AircraftScene := preload("res://scenes/aircraft/f35c.tscn")
const HudScene := preload("res://scenes/ui/hud.tscn")
const TestWorld := preload("res://scripts/test/flight_test_world.gd")
const KT := 0.514444
const FT := 0.3048

const PHASES := [
	{"name": "cruise", "dur": 6.0},
	{"name": "turn7g", "dur": 8.0},
	{"name": "supersonic", "dur": 15.0},
	{"name": "lowpass", "dur": 9.0},
	{"name": "gear", "dur": 9.0},
	{"name": "crash", "dur": 8.5},
]
const REAL_PHASES := [
	{"name": "canyon", "dur": 48.0},
]
const MATE_OFFSETS := [Vector3(32.0, -3.0, 30.0), Vector3(-32.0, -3.0, 30.0), Vector3(64.0, -6.0, 60.0)]

var world: Node3D
var phases: Array = PHASES
var real_world := false
var path: PackedVector3Array
var path_i := 0
var jet: Aircraft
var mates: Array[Aircraft] = []
var hud: FlightHud
var ap := ControlInput.new()
var phase := -1
var phase_t := 0.0
var total_t := 0.0
var shots_dir := ""
var headless := false
var stats: Array = []
var taken: Dictionary = {}
var rng := RandomNumberGenerator.new()
var send_acc := 0.0
var pitch_i := 0.0
var lever := 0.7
var vfx_off := false
var gpu_on: Array[float] = []
var gpu_off: Array[float] = []
var mate_err: Dictionary = {}
var notes: Dictionary = {}
var crash_time := -1.0
var vp_rid: RID
var skip_frames := 0
var in_flight: Array = []
var hitch_cool := 0.0
var hitches := 0


func _ready() -> void:
	rng.seed = 7
	var args: Dictionary = Engine.get_meta("boot_args", {}) if Engine.has_meta("boot_args") else {}
	shots_dir = str(args.get("shots", ""))
	headless = DisplayServer.get_name() == "headless"
	if not headless:
		DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
		vp_rid = get_viewport().get_viewport_rid()
		RenderingServer.viewport_set_measure_render_time(vp_rid, true)
	real_world = str(args.get("world", "")) == "real"
	if real_world:
		world = load("res://scenes/world/world.tscn").instantiate()
		add_child(world)
		phases = REAL_PHASES
		path = world.canyon_path()
	else:
		world = TestWorld.new()
		add_child(world)
		world.build()
	jet = AircraftScene.instantiate()
	_swap_visual(jet, str(args.get("visual", "")))
	jet.world = world
	jet.pilot_name = "Lead"
	add_child(jet)
	jet.crashed.connect(_on_crashed)
	for i in 3:
		var m: Aircraft = AircraftScene.instantiate()
		_swap_visual(m, str(args.get("visual", "")))
		m.is_local = false
		m.peer_id = i + 2
		m.pilot_name = "Dash %d" % (i + 2)
		m.world = world
		add_child(m)
		mates.append(m)
	hud = HudScene.instantiate()
	add_child(hud)
	hud.set_aircraft(jet)
	hud.set_camera(jet.camera)
	var ctl := get_node_or_null(^"/root/Controls")
	if ctl:
		ctl.override = ap
	jet.use_controls = ctl != null
	for p in phases:
		stats.append({"frames": 0, "time": 0.0, "max_dt": 0.0})
	print("REPORT flight_demo start gpu=%s headless=%s" % [RenderingServer.get_video_adapter_name(), headless])
	_next_phase()


func _swap_visual(a: Aircraft, which: String) -> void:
	# --visual=real tests the VFX/marker contract against the main agent's imported model
	if which != "real" or not ResourceLoader.exists("res://scenes/aircraft/f35c_visual_real.tscn"):
		return
	var ps := load("res://scenes/aircraft/f35c_visual_real.tscn") as PackedScene
	if ps == null:
		return
	var old := a.get_node("Visual")
	var nv: Node3D = ps.instantiate()
	a.remove_child(old)
	old.free()
	nv.name = "Visual"
	a.add_child(nv)
	a.move_child(nv, 0)


func _spawn(pos: Vector3, heading_deg: float, pitch_deg: float, speed: float) -> void:
	var b := Basis(Vector3.UP, -deg_to_rad(heading_deg)) * Basis(Vector3.RIGHT, deg_to_rad(pitch_deg))
	jet.respawn(Transform3D(b, pos), speed)
	pitch_i = 0.0


func _next_phase() -> void:
	phase += 1
	phase_t = 0.0
	if phase >= phases.size():
		_finish()
		return
	var n: String = phases[phase].name
	ap.look = Vector2.ZERO
	match n:
		"cruise":
			_spawn(Vector3(-14000, 1500, 0), 90.0, 0.0, 240.0)
			lever = 0.72
		"supersonic":
			_spawn(Vector3(-16000, 250, 3500), 90.0, 0.0, 0.9 * 340.3)
			lever = 2.0
		"lowpass":
			_spawn(Vector3(1600, 30, 0), 90.0, 0.0, 280.0)
			lever = 1.0
		"gear":
			_spawn(Vector3(-6000, 200, -3000), 90.0, 0.0, 80.0)
			jet.gear_down = true
			lever = 0.6
		"crash":
			_spawn(Vector3(-6000, 420, 0), 90.0, -28.0, 250.0)
			lever = 1.0
		"canyon":
			_spawn_on_path(0)
	for m in mates:
		m.visible = n in ["cruise", "turn7g", "supersonic", "canyon"]
	print("REPORT phase %s start t=%.1f" % [n, total_t])


func _physics_process(delta: float) -> void:
	if phase < 0 or phase >= phases.size():
		return
	var m := jet.model
	var n: String = phases[phase].name
	var pitch := 0.0
	var roll := 0.0
	match n:
		"cruise":
			pitch = _alt_hold(1500.0, 0.004)
			roll = _bank_hold(0.0)
		"turn7g":
			lever = 1.0
			roll = _bank_hold(deg_to_rad(80.0))
			if m.bank > deg_to_rad(55.0):
				pitch_i = clampf(pitch_i + (7.0 - m.nz) * delta * 1.2, 0.0, 1.0)
				pitch = pitch_i
			else:
				pitch = _alt_hold(1500.0, 0.004)
		"supersonic":
			pitch = _alt_hold(250.0, 0.006)
			roll = _bank_hold(0.0)
			vfx_off = phase_t > 7.5 and phase_t < 9.5
			if phase_t > 11.0 and phase_t < 13.5:
				ap.look = Vector2(0.62, 0.12)
			else:
				ap.look = Vector2.ZERO
		"lowpass":
			pitch = _alt_hold(30.0, 0.012)
			roll = _bank_hold(0.0) + clampf(-m.position.z * 0.004, -0.3, 0.3)
		"gear":
			pitch = _alt_hold(200.0, 0.006)
			roll = _bank_hold(0.0)
			lever = clampf(lever + (72.0 - m.tas) * delta * 0.05, 0.1, 1.0)
			if phase_t > 5.0:
				ap.look = Vector2(0.55, 0.1)
		"crash":
			pitch = 0.0
			roll = 0.0
		"canyon":
			var pr := _canyon_pilot()
			pitch = pr.x
			roll = pr.y
	ap.pitch = pitch
	ap.roll = roll
	ap.yaw = 0.0
	ap.throttle = minf(lever, 1.0)
	ap.afterburner = clampf(lever - 1.0, 0.0, 1.0) if lever > 1.0 else 0.0
	var ctl := get_node_or_null(^"/root/Controls")
	if ctl == null:
		m.set_controls(ap.pitch, ap.roll, ap.yaw, ap.throttle, ap.afterburner)
	_feed_mates(delta)


func _spawn_on_path(i: int) -> void:
	i = clampi(i, 0, path.size() - 3)
	var a := path[i]
	var b := path[i + 2]
	var dir := Vector3(b.x - a.x, 0.0, b.z - a.z).normalized()
	var p := a
	p.y = world.surface_at(a.x, a.z) + 120.0
	jet.respawn(Transform3D(Basis.looking_at(dir, Vector3.UP), p), 215.0)
	path_i = i
	lever = 0.85


func _canyon_pilot() -> Vector2:
	# pure pursuit along the canyon centreline, ~70 m above the highest ground in the next 1.5 s, speed ~215 m/s
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
		_spawn_on_path(0)
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


func _alt_hold(target: float, k: float) -> float:
	var m := jet.model
	var gamma_des := clampf((target - m.position.y) * k, -0.12, 0.12)
	return clampf((gamma_des - m.path_angle) * 3.5 - m.rates.x * 0.4, -0.6, 0.6)


func _bank_hold(target: float) -> float:
	var m := jet.model
	return clampf((target - m.bank) * 2.2 - m.rates.z * 0.2, -1.0, 1.0)


func _feed_mates(delta: float) -> void:
	# simulated network: 30 Hz snapshots, 40-70 ms one-way latency with jitter, 5 % loss, delivered in order
	var now := Time.get_ticks_usec() * 1e-6
	while not in_flight.is_empty() and in_flight[0].at <= now:
		var pkt: Dictionary = in_flight.pop_front()
		if mates[0].visible:
			mates[pkt.i].apply_remote_state(pkt.s, pkt.sent)
	send_acc += delta
	if send_acc < 1.0 / 30.0:
		return
	send_acc -= 1.0 / 30.0
	if not mates[0].visible:
		return
	var st := jet.get_net_state()
	var lat := 0.04 + rng.randf() * 0.03
	for i in mates.size():
		if rng.randf() < 0.05:
			continue
		var s := st.duplicate()
		s.p = st.p + Basis(st.q) * MATE_OFFSETS[i]
		var at := now + lat
		if not in_flight.is_empty():
			at = maxf(at, in_flight[in_flight.size() - 1].at)
		in_flight.append({"at": at, "sent": now, "i": i, "s": s})


func _process(delta: float) -> void:
	if phase < 0 or phase >= phases.size():
		return
	total_t += delta
	phase_t += delta
	var st: Dictionary = stats[phase]
	if skip_frames > 0:
		skip_frames -= 1
	elif phase_t > 0.7:
		st.frames += 1
		st.time += delta
		st.max_dt = maxf(st.max_dt, delta)
	if not headless and phase_t > 0.7:
		var gpu := RenderingServer.viewport_get_measured_render_time_gpu(vp_rid)
		if phases[phase].name == "supersonic" and phase_t > 5.5:
			if vfx_off and phase_t > 8.0:
				gpu_off.append(gpu)
			elif not vfx_off and phase_t < 7.5:
				gpu_on.append(gpu)
	for a in [jet] + mates:
		(a as Aircraft).get_node("Vfx").visible = not vfx_off
		(a as Aircraft).visual.visible = (not vfx_off) and (a as Aircraft).alive
	var list: Array = []
	for m in mates:
		if m.visible:
			list.append({"name": m.pilot_name, "position": m.global_position})
	hud.set_teammates(list)
	# a hitch longer than max_physics_steps drops local sim time while remote copies stay on wall-clock time; skip those moments
	if delta > 0.05:
		hitch_cool = 0.4
		hitches += 1
	hitch_cool -= delta
	if phase_t > 1.5 and mates[0].visible and hitch_cool <= 0.0:
		var ideal: Vector3 = jet.global_transform * MATE_OFFSETS[0]
		var key: String = phases[phase].name
		if not mate_err.has(key):
			mate_err[key] = []
		var e := mates[0].global_position.distance_to(ideal)
		mate_err[key].append(e)
		if e > 20.0 and not taken.has("spike_" + key):
			taken["spike_" + key] = true
			print("REPORT mate_err_spike phase=%s t=%.2f err=%.1f dt=%.4f snaps=%d" % [key, phase_t, e, delta, mates[0]._snaps.size()])
	_checkpoints()
	if phase_t >= phases[phase].dur:
		_next_phase()


func _checkpoints() -> void:
	var n: String = phases[phase].name
	var t := jet.telemetry()
	match n:
		"cruise":
			_once("cruise_tele", phase_t > 4.0, func(): _tele("cruise", t))
			_shot_at("01_cruise", phase_t > 4.2)
		"turn7g":
			notes["turn_gmax"] = maxf(notes.get("turn_gmax", 0.0), t.g)
			_shot_at("02_turn_vapour", phase_t > 3.6)
			_shot_at("03_turn_late", phase_t > 6.8)
			_once("turn_tele", phase_t > 7.8, func(): _tele("turn_end", t))
		"supersonic":
			notes["mach_max"] = maxf(notes.get("mach_max", 0.0), t.mach)
			_shot_at("04_transonic", t.mach > 0.975 and phase_t > 1.0)
			_shot_at("05_formation_ab", phase_t > 6.5)
			_shot_at("06_plume_side", phase_t > 13.0)
			_once("ss_tele", phase_t > 14.8, func(): _tele("supersonic_end", t))
		"lowpass":
			if phase_t > 2.0:
				notes["low_min"] = minf(notes.get("low_min", 999.0), t.radar_alt_ft * FT)
				notes["low_max"] = maxf(notes.get("low_max", 0.0), t.radar_alt_ft * FT)
			_shot_at("07_canyon_low", phase_t > 3.5)
			_once("low_tele", phase_t > 8.0, func(): _tele("lowpass", t))
		"gear":
			_once("help_on", phase_t > 2.0, func(): hud.toggle_help())
			_shot_at("08b_help", phase_t > 2.5)
			_once("help_off", phase_t > 3.0, func(): hud.toggle_help())
			_shot_at("08_gear_slow", phase_t > 7.6)
			_once("gear_tele", phase_t > 8.5, func(): _tele("gear_pass", t))
		"canyon":
			if world.has_method("set_focus"):
				world.set_focus(jet.global_position)
			notes["canyon_min_agl"] = minf(notes.get("canyon_min_agl", 9999.0), t.radar_alt_ft * FT)
			for k in [6, 14, 22, 30, 38, 46]:
				_shot_at("canyon_%02d" % k, phase_t > k)
			_once("canyon_t1", phase_t > 20.0, func(): _tele("canyon_20s", t))
			_once("canyon_t2", phase_t > 40.0, func(): _tele("canyon_40s", t))
		"crash":
			if crash_time > 0.0:
				_shot_at("09_crash", total_t - crash_time > 1.2)
				_shot_at("10_crash_smoke", total_t - crash_time > 3.8)


func _once(key: String, cond: bool, f: Callable) -> void:
	if cond and not taken.has(key):
		taken[key] = true
		f.call()


func _tele(tag: String, t: Dictionary) -> void:
	print("REPORT tele %s ias=%d kt mach=%.2f alt=%d ft ra=%d ft aoa=%.1f g=%.2f gmax=%.2f hdg=%d thr=%.2f ab=%s fuel=%d kg vs=%d fpm" % [
		tag, int(t.ias_kt), t.mach, int(t.alt_ft), int(t.radar_alt_ft), t.aoa_deg, t.g, t.g_max, int(t.heading_deg), t.throttle, t.ab, int(t.fuel_kg), int(t.vs_fpm)])


func _shot_at(name: String, cond: bool) -> void:
	if cond and not taken.has(name):
		taken[name] = true
		_shot(name)


func _shot(name: String) -> void:
	if headless or shots_dir == "":
		print("REPORT shot %s (skipped)" % name)
		if headless:
			OS.delay_msec(110)
		return
	await RenderingServer.frame_post_draw
	skip_frames = 2
	var img := get_viewport().get_texture().get_image()
	var path := shots_dir.path_join(name + ".png")
	img.save_png(path)
	print("REPORT shot %s -> %s" % [name, path])


func _on_crashed(at: Vector3) -> void:
	crash_time = total_t
	if real_world:
		get_tree().create_timer(2.5).timeout.connect(func(): _spawn_on_path(path_i + 3))
	print("REPORT crash signal at (%.0f, %.0f, %.0f) t=%.2f" % [at.x, at.y, at.z, total_t])


func _finish() -> void:
	for i in phases.size():
		var st: Dictionary = stats[i]
		var avg: float = st.frames / maxf(st.time, 1e-3)
		var mn: float = 1.0 / maxf(st.max_dt, 1e-3)
		print("REPORT fps phase=%s avg=%.1f min=%.1f frames=%d" % [phases[i].name, avg, mn, st.frames])
	if not gpu_on.is_empty() and not gpu_off.is_empty():
		var a := _mean(gpu_on)
		var b := _mean(gpu_off)
		print("REPORT gpu_ms 4 jets AB+vfx=%.2f  hidden=%.2f  aircraft+vfx cost=%.2f ms" % [a, b, a - b])
	print("REPORT frames over 50 ms: %d" % hitches)
	for key: String in mate_err:
		var errs: Array = mate_err[key]
		print("REPORT wingman remote-copy error phase=%s mean=%.2f m max=%.2f m (30 Hz, 40-70 ms latency, 5%% loss)" % [key, _mean(errs), errs.max()])
	if real_world:
		print("REPORT canyon min AGL=%.0f m, path index reached=%d/%d" % [notes.get("canyon_min_agl", -1.0), path_i, path.size()])
	print("REPORT turn gmax=%.2f mach_max=%.3f lowpass agl min=%.1f max=%.1f m" % [notes.get("turn_gmax", 0.0), notes.get("mach_max", 0.0), notes.get("low_min", -1.0), notes.get("low_max", -1.0)])
	print("REPORT mem static=%d MB vram=%d MB draw_calls=%d" % [OS.get_static_memory_usage() / 1048576, Performance.get_monitor(Performance.RENDER_VIDEO_MEM_USED) / 1048576, Performance.get_monitor(Performance.RENDER_TOTAL_DRAW_CALLS_IN_FRAME)])
	print("REPORT flight_demo done t=%.1f" % total_t)
	var ctl := get_node_or_null(^"/root/Controls")
	if ctl:
		ctl.override = null
	get_tree().quit()


static func _mean(a: Array) -> float:
	var s := 0.0
	for v in a:
		s += v
	return s / maxf(a.size(), 1)
