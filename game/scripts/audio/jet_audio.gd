# F-35 engine/airframe audio from real recordings: throttle-crossfaded engine layers, wind, buffet, one-shots, and delayed/doppler remote jets.
class_name JetAudio
extends Node3D

const DIR := "res://assets/audio/"
const SPEED_OF_SOUND := 343.0            # m/s at 20 °C, sea level
const HISTORY_S := 30.0                  # 30 s of path history = sound delay from ~10 km
const HISTORY_DT := 0.05
const FLYBY_CPA_S := 3.05                # loudest point inside flyby_close.ogg (measured by the build script)
const NOZZLE_OFFSET := Vector3(0.0, 0.0, 7.0)  # exhaust ~7 m aft of CG (length 15.7 m)

# Remote-jet mix tuning.
const REMOTE_UNIT_SIZE := 60.0           # 0 dB point of the inverse-distance model, metres
const REMOTE_MAX_DISTANCE := 12000.0
const FAR_BLEND := Vector2(900.0, 3500.0)   # near layers -> distant rumble crossfade range, metres

var is_local := true
## Optional node whose global position is the listener (defaults to the viewport's current camera).
var listener_override: Node3D

var _layers := {}                        # name -> AudioStreamPlayer3D
var _spool := 0.0
var _ab := 0.0
var _ab_was_lit := false
var _gear_state := -1
var _alive := true
var _engine_fade := 1.0
var _last_crash_ms := -100000
var _last_gear_ms := -100000
var _time := 0.0

# Remote-only state.
var _hist_t := PackedFloat64Array()
var _hist_p := PackedVector3Array()
var _hist_next := 0.0
var _pitch := 1.0
var _prev_listener := Vector3.ZERO
var _listener_vel := Vector3.ZERO
var _jet_vel := Vector3.ZERO
var _prev_pos := Vector3.ZERO
var _have_prev := false
var _in_cone := false
var _flyby_cooldown := 0.0
var _duck := 0.0
var _emitter: Node3D
var _audible := false
var _pending_start := false
var _vel_samples := 0

## Public read-out for tests/HUD debugging.
var debug := {}


func setup(local: bool) -> void:
	is_local = local
	ensure_buses()
	for c in get_children():
		c.queue_free()
	_layers.clear()
	if is_local:
		_emitter = Node3D.new()
		_emitter.position = NOZZLE_OFFSET
		add_child(_emitter)
		for n in ["engine_idle", "engine_mil", "engine_ab"]:
			_layers[n] = _make_loop(n + "_loop", "Engines", true)
		for n in ["wind_low", "wind_high", "buffet"]:
			_layers[n] = _make_loop(n + "_loop", "Wind", true)
	else:
		_emitter = Node3D.new()
		_emitter.top_level = true
		add_child(_emitter)
		for n in ["engine_idle", "engine_mil", "engine_ab", "engine_far"]:
			_layers[n] = _make_loop(n + "_loop", "Remote", false)
	for p in _layers.values():
		(p as AudioStreamPlayer3D).volume_db = -80.0
	_pending_start = true
	if is_inside_tree():
		_start_layers()
	_spool = 0.0
	_ab = 0.0
	_alive = true
	_engine_fade = 1.0
	_hist_t.clear()
	_hist_p.clear()
	_have_prev = false
	_vel_samples = 0


func _enter_tree() -> void:
	if _pending_start:
		_start_layers.call_deferred()


func _start_layers() -> void:
	if not _pending_start or not is_inside_tree():
		return
	_pending_start = false
	for p in _layers.values():
		(p as AudioStreamPlayer3D).play(randf() * 3.0)


func update(t: Dictionary, delta: float) -> void:
	if _layers.is_empty() or delta <= 0.0:
		return
	_time += delta
	var thr := clampf(float(t.get("throttle", t.get("thr", 0.0))), 0.0, 1.0)
	var ab_target := float(t.get("ab_level", 1.0 if bool(t.get("ab", false)) else 0.0))
	if bool(t.get("ab", false)) and ab_target <= 0.0:
		ab_target = 1.0
	_spool = lerpf(_spool, thr, 1.0 - exp(-delta * 4.0))
	_ab = move_toward(_ab, clampf(ab_target, 0.0, 1.0), delta * 3.0)
	var lit := ab_target > 0.02
	if lit and not _ab_was_lit and _alive:
		if is_local:
			_play_oneshot("ab_lightup", 0.0, "SFX", _emitter)
		elif _audible and float(debug.get("tau", 0.0)) < HISTORY_S:
			# Remote: the light-up is heard after the sound travel time, from where the jet was.
			get_tree().create_timer(float(debug.get("tau", 0.0))).timeout.connect(func() -> void:
				if is_instance_valid(self) and _audible:
					_play_oneshot("ab_lightup", -2.0, "Remote", _emitter))
	_ab_was_lit = lit
	if t.has("gear"):
		var g := 1 if bool(t.gear) else 0
		if _gear_state != -1 and g != _gear_state:
			play_gear()
		_gear_state = g
	if t.has("alive"):
		var a := bool(t.alive)
		if _alive and not a:
			play_crash()
		elif a and not _alive:
			_alive = true
	_engine_fade = move_toward(_engine_fade, 1.0 if _alive else 0.0, delta * (0.8 if _alive else 4.0))
	if is_local:
		_update_local(t, delta)
	else:
		_update_remote(t, delta)


func play_crash() -> void:
	var now := Time.get_ticks_msec()
	if now - _last_crash_ms < 1500:
		return
	_last_crash_ms = now
	_alive = false
	var p := _play_oneshot("crash_explosion", 6.0, "SFX", null)
	if p == null:
		return
	p.top_level = true
	p.global_position = global_position
	p.unit_size = 400.0
	if not is_local:
		# Sound travels: the listener hears the crash distance/c later.
		var d := global_position.distance_to(_listener_pos())
		p.stop()
		get_tree().create_timer(d / SPEED_OF_SOUND).timeout.connect(p.play)


func play_gear() -> void:
	var now := Time.get_ticks_msec()
	if now - _last_gear_ms < 800:
		return
	_last_gear_ms = now
	_play_oneshot("gear_clunk", -4.0 if is_local else -10.0, "SFX", self)


# ----------------------------------------------------------------- local jet

func _update_local(t: Dictionary, _delta: float) -> void:
	var ias := float(t.get("ias_kt", 0.0))
	var aoa := absf(float(t.get("aoa_deg", 0.0)))
	var g := float(t.get("g", 1.0))
	var n := _spool
	var ab := _ab
	# Throttle crossfade: idle whine fades as the dry roar builds; AB adds the deep crackling layer on top.
	var w_idle := 1.0 - smoothstep(0.2, 0.75, n) * 0.8
	var w_mil := smoothstep(0.05, 0.8, n)
	var w_ab := smoothstep(0.0, 0.35, ab)
	w_mil *= 1.0 - 0.45 * w_ab
	# Camera aspect: ahead of the nose the roar drops and the inlet whine dominates.
	var aspect := _camera_aspect()          # 1 = behind the jet, 0 = in front
	var roar := lerpf(0.45, 1.0, aspect)
	var fade := _engine_fade
	_set_layer("engine_idle", w_idle * lerpf(1.25, 0.8, aspect) * fade, 0.9 + 0.22 * n)
	_set_layer("engine_mil", w_mil * roar * fade, 0.88 + 0.16 * n + 0.03 * ab)
	_set_layer("engine_ab", w_ab * 1.25 * roar * fade, 0.95 + 0.06 * ab)
	# Airflow: low rush from ~100 kt, high tearing rush from ~300 kt, pitch follows speed.
	var w_lo := smoothstep(60.0, 260.0, ias) * (1.0 - 0.5 * smoothstep(350.0, 600.0, ias))
	var w_hi := smoothstep(220.0, 620.0, ias)
	_set_layer("wind_low", w_lo * 0.7, 0.85 + ias / 1400.0)
	_set_layer("wind_high", w_hi * 0.75, 0.8 + ias / 1500.0)
	# Buffet: high AoA or high G at speed.
	var buf := maxf(smoothstep(14.0, 28.0, aoa), smoothstep(5.0, 8.5, g)) * smoothstep(120.0, 220.0, ias)
	_set_layer("buffet", buf * 1.1, 0.9 + 0.2 * buf)
	debug.merge({"spool": n, "ab": ab, "aspect": aspect, "w_idle": w_idle, "w_mil": w_mil, "w_ab": w_ab, "wind_lo": w_lo, "wind_hi": w_hi, "buffet": buf}, true)


func _camera_aspect() -> float:
	var cam := get_viewport().get_camera_3d() if is_inside_tree() else null
	if cam == null:
		return 1.0
	var to_cam := cam.global_position - global_position
	if to_cam.length_squared() < 0.01:
		return 1.0
	var fwd := -global_transform.basis.z
	return clampf(0.5 - 0.5 * fwd.dot(to_cam.normalized()), 0.0, 1.0)


# ----------------------------------------------------------------- remote jet

func _update_remote(t: Dictionary, delta: float) -> void:
	var pos := global_position
	var lis := _listener_pos()
	if _have_prev:
		var k := 1.0 if _vel_samples < 2 else 1.0 - exp(-delta * 10.0)
		_jet_vel = _jet_vel.lerp((pos - _prev_pos) / delta, k)
		_listener_vel = _listener_vel.lerp((lis - _prev_listener) / delta, k)
		_vel_samples += 1
	_prev_pos = pos
	_prev_listener = lis
	_have_prev = true
	if t.has("velocity"):
		_jet_vel = t.velocity
	_record_history(pos)
	# Retarded time: the listener hears where the jet was tau seconds ago, with |p(now-tau) - L| = c*tau.
	var ret := _retarded(lis)
	_audible = ret.found and _vel_samples >= 1
	var emit_p: Vector3 = ret.pos
	var emit_v: Vector3 = ret.vel
	_emitter.global_position = emit_p
	var n_dir := (lis - emit_p)
	var dist := n_dir.length()
	n_dir = n_dir / maxf(dist, 0.001)
	var den := maxf(SPEED_OF_SOUND - emit_v.dot(n_dir), SPEED_OF_SOUND * 0.3)
	var num := maxf(SPEED_OF_SOUND - _listener_vel.dot(n_dir), SPEED_OF_SOUND * 0.3)
	var target_pitch := clampf(num / den, 0.5, 2.5)
	_pitch = lerpf(_pitch, target_pitch, 1.0 - exp(-delta * 12.0))
	# Mach cone -> sonic boom when the listener crosses into it.
	var speed := _jet_vel.length()
	var mach := float(t.get("mach", speed / SPEED_OF_SOUND))
	var cone := false
	if mach > 1.0 and speed > 1.0:
		var mu := asin(1.0 / mach)
		var rel := lis - pos
		var ang := acos(clampf(rel.normalized().dot(-_jet_vel.normalized()), -1.0, 1.0))
		cone = ang < mu
		if cone and not _in_cone and rel.length() < 15000.0:
			var b := _play_oneshot("sonic_boom", 3.0 - 15.0 * log(maxf(rel.length(), 300.0) / 300.0) / log(10.0), "SFX", null)
			if b:
				b.attenuation_model = AudioStreamPlayer3D.ATTENUATION_DISABLED
				b.top_level = true
				b.global_position = lis + (pos - lis).normalized() * 30.0
	_in_cone = cone
	# Close pass: fire the real flyby recording so its loudest moment lands on the heard closest approach.
	_flyby_cooldown = maxf(0.0, _flyby_cooldown - delta)
	var v_rel := _jet_vel - _listener_vel
	if _flyby_cooldown <= 0.0 and v_rel.length() > 90.0 and _alive and mach < 1.0:
		var rel0 := pos - lis
		var t_cpa := -rel0.dot(v_rel) / v_rel.length_squared()
		if t_cpa > 0.0:
			var d_cpa := (rel0 + v_rel * t_cpa).length()
			var t_heard := t_cpa + d_cpa / SPEED_OF_SOUND
			if d_cpa < 350.0 and t_heard <= FLYBY_CPA_S and t_heard > FLYBY_CPA_S - 0.5:
				var loud := 1.0 - 20.0 * log(maxf(d_cpa, 40.0) / 80.0) / log(10.0)
				var f := _play_oneshot("flyby_close", loud, "Remote", null)
				if f:
					f.attenuation_model = AudioStreamPlayer3D.ATTENUATION_DISABLED
					f.top_level = true
					f.set_meta("follow", true)
					_flyby_follow(f)
				_flyby_cooldown = 9.0
				_duck = 1.0
	_duck = move_toward(_duck, 0.0, delta / 6.0)
	# Layer mix.
	var n := _spool
	var ab := _ab
	var w_idle := 1.0 - smoothstep(0.2, 0.75, n) * 0.85
	var w_mil := smoothstep(0.05, 0.8, n)
	var w_ab := smoothstep(0.0, 0.35, ab)
	var far := smoothstep(FAR_BLEND.x, FAR_BLEND.y, dist)
	var near := 1.0 - far
	var power := 0.35 + 0.45 * n + 0.6 * ab            # overall acoustic power
	var fade := _engine_fade * (1.0 if _audible else 0.0) * (1.0 - 0.75 * _duck)
	# Game-mix distance law: Godot's inverse-distance (-20 dB/decade) softened to about -12 dB/decade past unit_size,
	# so a jet in AB stays audible to ~8 km; AB adds +4 dB.
	var boost_db := 4.0 * ab + 8.0 * log(maxf(dist, REMOTE_UNIT_SIZE) / REMOTE_UNIT_SIZE) / log(10.0)
	_set_layer("engine_idle", w_idle * near * fade, (0.9 + 0.22 * n) * _pitch, boost_db)
	_set_layer("engine_mil", w_mil * near * fade, (0.88 + 0.16 * n) * _pitch, boost_db)
	_set_layer("engine_ab", w_ab * 1.2 * near * fade, 0.97 * _pitch, boost_db)
	_set_layer("engine_far", far * power * 1.6 * fade, clampf(_pitch, 0.7, 1.4), boost_db)
	debug.merge({"dist": dist, "tau": ret.tau, "pitch": _pitch, "audible": _audible, "far": far, "cone": cone, "duck": _duck}, true)


func _flyby_follow(p: AudioStreamPlayer3D) -> void:
	# Keep the flyby one-shot glued to the heard (retarded) position while it plays.
	var tw := create_tween()
	tw.tween_method(func(_x: float) -> void:
		if is_instance_valid(p) and is_instance_valid(_emitter):
			p.global_position = _listener_pos() + (_emitter.global_position - _listener_pos()).limit_length(60.0)
	, 0.0, 1.0, p.stream.get_length())


func _record_history(pos: Vector3) -> void:
	if _time < _hist_next and _hist_p.size() > 0:
		_hist_p[_hist_p.size() - 1] = pos
		_hist_t[_hist_t.size() - 1] = _time
		return
	_hist_next = _time + HISTORY_DT
	_hist_t.append(_time)
	_hist_p.append(pos)
	var keep := int(HISTORY_S / HISTORY_DT) + 2
	if _hist_p.size() > keep + 40:
		_hist_t = _hist_t.slice(_hist_t.size() - keep)
		_hist_p = _hist_p.slice(_hist_p.size() - keep)


func _retarded(lis: Vector3) -> Dictionary:
	var n := _hist_p.size()
	if n == 0:
		return {"found": false, "pos": global_position, "vel": Vector3.ZERO, "tau": 0.0}
	var g_prev := _hist_p[n - 1].distance_to(lis) - SPEED_OF_SOUND * (_time - _hist_t[n - 1])
	if g_prev <= 0.0:
		return {"found": true, "pos": _hist_p[n - 1], "vel": _jet_vel, "tau": _time - _hist_t[n - 1]}
	for i in range(n - 2, -1, -1):
		var g := _hist_p[i].distance_to(lis) - SPEED_OF_SOUND * (_time - _hist_t[i])
		if g <= 0.0:
			var f := g_prev / (g_prev - g)
			var p := _hist_p[i + 1].lerp(_hist_p[i], f)
			var dt := maxf(_hist_t[i + 1] - _hist_t[i], 0.001)
			var v := (_hist_p[i + 1] - _hist_p[i]) / dt
			return {"found": true, "pos": p, "vel": v, "tau": _time - lerpf(_hist_t[i + 1], _hist_t[i], f)}
		g_prev = g
	# Not heard yet: either supersonic and outside the cone, or history too short (just spawned) -> oldest point.
	var just_spawned := _time - _hist_t[0] < HISTORY_S - 1.0 and _jet_vel.length() < SPEED_OF_SOUND
	return {"found": just_spawned, "pos": _hist_p[0], "vel": _jet_vel, "tau": _time - _hist_t[0]}


# ----------------------------------------------------------------- helpers

func _listener_pos() -> Vector3:
	if listener_override and is_instance_valid(listener_override):
		return listener_override.global_position
	var cam := get_viewport().get_camera_3d() if is_inside_tree() else null
	return cam.global_position if cam else Vector3.ZERO


func _make_loop(file: String, bus: String, local: bool) -> AudioStreamPlayer3D:
	var p := AudioStreamPlayer3D.new()
	var s := load(DIR + file + ".ogg") as AudioStreamOggVorbis
	if s:
		s.loop = true
	p.stream = s
	p.bus = bus
	p.doppler_tracking = AudioStreamPlayer3D.DOPPLER_TRACKING_DISABLED
	if local:
		p.attenuation_model = AudioStreamPlayer3D.ATTENUATION_DISABLED
		p.panning_strength = 0.5
	else:
		p.attenuation_model = AudioStreamPlayer3D.ATTENUATION_INVERSE_DISTANCE
		p.unit_size = REMOTE_UNIT_SIZE
		p.max_db = 3.0
		p.max_distance = REMOTE_MAX_DISTANCE
		p.attenuation_filter_cutoff_hz = 5000.0     # air absorbs highs (~30 dB/km at 4 kHz); the far layer carries the rest
		p.attenuation_filter_db = -14.0
		p.panning_strength = 1.0
	_emitter.add_child(p)
	return p


func _set_layer(n: String, gain: float, pitch: float, extra_db := 0.0) -> void:
	var p: AudioStreamPlayer3D = _layers.get(n)
	if p == null:
		return
	p.volume_db = linear_to_db(maxf(gain, 0.00001)) + extra_db
	p.pitch_scale = clampf(pitch, 0.3, 3.5)
	debug["g_" + n] = gain


func _play_oneshot(file: String, vol_db: float, bus: String, parent: Node3D) -> AudioStreamPlayer3D:
	var s := load(DIR + file + ".ogg") as AudioStream
	if s == null:
		return null
	var p := AudioStreamPlayer3D.new()
	p.stream = s
	p.bus = bus
	p.volume_db = vol_db
	p.doppler_tracking = AudioStreamPlayer3D.DOPPLER_TRACKING_DISABLED
	if is_local:
		p.attenuation_model = AudioStreamPlayer3D.ATTENUATION_DISABLED
		p.panning_strength = 0.5
	else:
		p.attenuation_model = AudioStreamPlayer3D.ATTENUATION_INVERSE_DISTANCE
		p.unit_size = REMOTE_UNIT_SIZE
		p.max_distance = REMOTE_MAX_DISTANCE
		p.attenuation_filter_cutoff_hz = 5000.0
		p.attenuation_filter_db = -14.0
	(parent if parent else self).add_child(p)
	p.finished.connect(p.queue_free)
	p.play()
	return p


## Creates the Engines / Wind / SFX / Remote buses (all -> Master) and a limiter on Master, once.
static func ensure_buses() -> void:
	if AudioServer.get_bus_index("Engines") != -1:
		return
	var master := AudioServer.get_bus_index("Master")
	var has_limiter := false
	for i in AudioServer.get_bus_effect_count(master):
		if AudioServer.get_bus_effect(master, i) is AudioEffectHardLimiter:
			has_limiter = true
	if not has_limiter:
		var lim := AudioEffectHardLimiter.new()
		lim.ceiling_db = -1.0
		lim.pre_gain_db = 0.0
		lim.release = 0.12
		AudioServer.add_bus_effect(master, lim, 0)
	for b in [["Engines", 0.0], ["Wind", -3.0], ["SFX", 0.0], ["Remote", 0.0]]:
		var idx := AudioServer.bus_count
		AudioServer.add_bus(idx)
		AudioServer.set_bus_name(idx, b[0])
		AudioServer.set_bus_volume_db(idx, b[1])
		AudioServer.set_bus_send(idx, "Master")
