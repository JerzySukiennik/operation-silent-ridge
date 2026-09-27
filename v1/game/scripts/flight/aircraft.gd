# Aircraft root: runs F35FlightModel for the local jet or interpolates network snapshots for a remote copy; terrain crash checks, control-surface animation, VFX and the chase camera.
class_name Aircraft
extends Node3D

signal crashed(position: Vector3)

const SPAWN_SPEED := 210.0          # m/s (~410 KTAS) air start
const INTERP_DELAY := 0.0           # s render delay; 0 = predict remote jets to "now" (aircraft motion extrapolates well), raise for a jitter buffer
const MAX_EXTRAPOLATION := 0.6      # s of dead reckoning before freezing
const SNAP_DISTANCE := 120.0        # m: larger errors teleport instead of blending
const CLOCK_TRUST_WINDOW := 5.0     # s: |now - sent_time| below this means sent_time is already in our clock
const SURFACE_RATE := deg_to_rad(80.0)   # control-surface actuator rate
const COLLISION_POINTS := [
	Vector3(0.0, 0.0, -8.0), Vector3(0.0, 0.2, 7.2), Vector3(-6.4, -0.1, 3.4), Vector3(6.4, -0.1, 3.4),
	Vector3(0.0, -0.8, 0.0), Vector3(-2.0, 2.3, 5.6), Vector3(2.0, 2.3, 5.6),
]
const FT := 0.3048
const KT := 0.514444

var is_local := true
var peer_id := 1
var pilot_name := "Pilot"
var world: Node                     # has height_at/surface_at; null = sea level only
var model := F35FlightModel.new()
var alive := true
var gear_down := false
var brake := false
var use_controls := true            # false: drive model inputs yourself (tests)
var time_to_impact := 99.0          # s along the current velocity until terrain/sea (10 Hz)

@onready var visual: Node3D = $Visual
@onready var vfx: AircraftVfx = $Vfx
@onready var camera: ChaseCamera = $ChaseCamera

var _prev := Transform3D.IDENTITY
var _curr := Transform3D.IDENTITY
var _snaps: Array[Dictionary] = []
var _clock_offset := 0.0
var _remote_pos := Vector3.ZERO
var _remote_rot := Quaternion.IDENTITY
var _remote_vel := Vector3.ZERO
var _remote_w := Vector3.ZERO
var _remote_thr := 0.0
var _remote_ab := 0.0
var _remote_nz := 1.0
var _remote_prev_vel := Vector3.ZERO
var _remote_started := false
var _surfaces: Dictionary = {}
var _surface_angle: Dictionary = {}
var _gear_node: Node3D
var _gear_anim := 0.0
var _warn_timer := 0.0
var _pitch_vis := 0.0
var _roll_vis := 0.0
var _yaw_vis := 0.0


func _ready() -> void:
	physics_interpolation_mode = Node.PHYSICS_INTERPOLATION_MODE_OFF
	_bind_visual()
	vfx.setup(self)
	if not is_local:
		camera.current = false
		camera.queue_free()
		camera = null
	else:
		camera.setup(self)
	_curr = global_transform
	_prev = _curr
	if is_local and model.tas < 1.0:
		respawn(global_transform)


func _bind_visual() -> void:
	_surfaces.clear()
	for n in ["ElevonL", "ElevonR", "FlaperonL", "FlaperonR", "AileronL", "AileronR", "RudderL", "RudderR"]:
		var node := visual.find_child(n, true, false) as Node3D
		if node:
			_surfaces[n] = [node, node.transform.basis]
			_surface_angle[n] = 0.0
	_gear_node = visual.find_child("Gear", true, false) as Node3D


func marker(n: String) -> Transform3D:
	var m := visual.find_child(n, true, false) as Node3D
	if m == null:
		return Transform3D.IDENTITY
	return visual.transform * _relative_to_visual(m)


func _relative_to_visual(n: Node3D) -> Transform3D:
	var t := n.transform
	var p := n.get_parent()
	while p != null and p != visual:
		if p is Node3D:
			t = (p as Node3D).transform * t
		p = p.get_parent()
	return t


func respawn(at: Transform3D, speed: float = SPAWN_SPEED) -> void:
	model.reset(at, speed)
	alive = true
	gear_down = false
	_gear_anim = 0.0
	brake = false
	time_to_impact = 99.0
	_curr = model.get_transform()
	_prev = _curr
	global_transform = _curr
	visual.visible = true
	vfx.reset()
	var ctl := _controls()
	if ctl and is_local:
		ctl.set_lever(0.8)
	if camera:
		camera.snap()


func _controls() -> Node:
	return get_node_or_null(^"/root/Controls")


# ---------------------------------------------------------------- local simulation

func _physics_process(delta: float) -> void:
	if not is_local or not alive:
		return
	if use_controls:
		var ctl := _controls()
		if ctl:
			var inp: ControlInput = ctl.input
			if inp.gear_toggle:
				gear_down = not gear_down
			brake = inp.brake
			model.set_controls(inp.pitch, inp.roll, inp.yaw, inp.throttle, inp.afterburner, inp.brake)
	model.gear_down = gear_down
	_prev = _curr
	model.step(delta)
	_curr = model.get_transform()
	_check_collision()
	_warn_timer -= delta
	if _warn_timer <= 0.0:
		_warn_timer = 0.1
		time_to_impact = _predict_impact()


func surface_height(x: float, z: float) -> float:
	if world and world.has_method("surface_at"):
		return world.surface_at(x, z)
	return 0.0


func _check_collision() -> void:
	var p := model.position
	var ground := surface_height(p.x, p.z)
	if p.y - ground > 40.0:
		return
	var xf := _curr
	for lp: Vector3 in COLLISION_POINTS:
		var wp := xf * lp
		if wp.y < surface_height(wp.x, wp.z):
			_crash(wp)
			return


func _predict_impact() -> float:
	# straight-line time to terrain/sea; ignores tall walls flown into level (you steer round those), keeps ground and slopes
	var p := model.position
	var v := model.velocity
	var t := 0.0
	while t < 10.0:
		t += 0.25 if t < 4.0 else 1.0
		var q := p + v * t
		var g := surface_height(q.x, q.z)
		if q.y < g + 5.0:
			if v.y < -3.0 or g - q.y < 150.0:
				return t
			return 99.0
	return 99.0


func _crash(at: Vector3) -> void:
	if not alive:
		return
	alive = false
	visual.visible = false
	vfx.explode(at, model.velocity)
	crashed.emit(at)


# ---------------------------------------------------------------- network

func get_net_state() -> Dictionary:
	return {
		"p": model.position, "q": model.orientation, "v": model.velocity,
		"w": model.angular_velocity_world(), "thr": model.engine, "ab": model.ab > 0.0,
		"abl": model.ab, "gear": gear_down, "alive": alive,
	}


func apply_remote_state(state: Dictionary, sent_time: float) -> void:
	# Snapshot prediction lives here. sent_time is expected in OUR clock (Time.get_ticks_usec() * 1e-6, NetSync applies its offset);
	# if it is clearly in another time base, fall back to estimating the offset from the fastest packets (then latency is not compensated).
	var now := Time.get_ticks_usec() * 1e-6
	var off := now - sent_time
	if absf(off) < CLOCK_TRUST_WINDOW:
		_clock_offset = 0.0
	elif off < _clock_offset or absf(_clock_offset) < 1e-9:
		_clock_offset = off
	else:
		_clock_offset += (off - _clock_offset) * 0.01
	var snap := {
		"t": sent_time,
		"p": state.get("p", Vector3.ZERO),
		"q": state.get("q", Quaternion.IDENTITY),
		"v": state.get("v", Vector3.ZERO),
		"w": state.get("w", Vector3.ZERO),
		"thr": float(state.get("thr", 0.8)),
		"ab": float(state.get("abl", 1.0 if state.get("ab", false) else 0.0)),
		"gear": bool(state.get("gear", false)),
		"alive": bool(state.get("alive", true)),
	}
	if not _snaps.is_empty() and sent_time <= _snaps[_snaps.size() - 1].t:
		if sent_time < _snaps[_snaps.size() - 1].t - 1.0:
			_snaps.clear()
		else:
			return
	_snaps.append(snap)
	if _snaps.size() > 24:
		_snaps.pop_front()
	if not _remote_started:
		_remote_started = true
		_remote_pos = snap.p
		_remote_rot = snap.q
		_remote_vel = snap.v
		_remote_prev_vel = snap.v
		global_transform = Transform3D(Basis(_remote_rot), _remote_pos)


func _update_remote(delta: float) -> void:
	if _snaps.is_empty():
		return
	var now := Time.get_ticks_usec() * 1e-6
	var rt := now - _clock_offset - INTERP_DELAY
	var target_p: Vector3
	var target_q: Quaternion
	var target_v: Vector3
	var last: Dictionary = _snaps[_snaps.size() - 1]
	if rt >= last.t:
		var e := minf(rt - last.t, MAX_EXTRAPOLATION)
		var acc_e := Vector3.ZERO
		if _snaps.size() >= 2:
			var prev: Dictionary = _snaps[_snaps.size() - 2]
			var dt_s: float = last.t - prev.t
			if dt_s > 0.005 and dt_s < 0.3:
				acc_e = ((last.v as Vector3) - (prev.v as Vector3)) / dt_s
				acc_e = acc_e.limit_length(100.0)
		target_v = last.v + acc_e * e
		target_p = last.p + last.v * e + acc_e * (0.5 * e * e)
		var w: Vector3 = last.w
		target_q = last.q
		if w.length() > 1e-5:
			target_q = (Quaternion(w.normalized(), w.length() * e) * target_q).normalized()
		_apply_remote_flags(last)
	else:
		var i := _snaps.size() - 1
		while i > 0 and _snaps[i - 1].t > rt:
			i -= 1
		if i == 0:
			var first: Dictionary = _snaps[0]
			target_p = first.p
			target_q = first.q
			target_v = first.v
			_apply_remote_flags(first)
		else:
			var a: Dictionary = _snaps[i - 1]
			var b: Dictionary = _snaps[i]
			var h: float = maxf(b.t - a.t, 1e-4)
			var u := clampf((rt - a.t) / h, 0.0, 1.0)
			target_p = _hermite(a.p, a.v * h, b.p, b.v * h, u)
			target_q = (a.q as Quaternion).slerp(b.q, u)
			target_v = (a.v as Vector3).lerp(b.v, u)
			_apply_remote_flags(b if u > 0.5 else a)
	if _remote_pos.distance_to(target_p) > SNAP_DISTANCE:
		_remote_pos = target_p
		_remote_rot = target_q
	else:
		var k := 1.0 - exp(-delta * 18.0)
		_remote_pos = (_remote_pos + target_v * delta).lerp(target_p, k)
		_remote_rot = _remote_rot.slerp(target_q, k)
	var acc := (target_v - _remote_prev_vel) / maxf(delta, 1e-3)
	_remote_prev_vel = target_v
	var up := Basis(_remote_rot).y
	var nz := (acc - F35FlightModel.GRAVITY).dot(up) / F35FlightModel.G0
	_remote_nz = lerpf(_remote_nz, clampf(nz, -4.0, 10.0), 1.0 - exp(-delta * 6.0))
	_remote_vel = target_v
	_remote_w = last.w
	global_transform = Transform3D(Basis(_remote_rot), _remote_pos)
	model.position = _remote_pos
	model.orientation = _remote_rot
	model.velocity = _remote_vel
	model.refresh_air_data()
	model.nz = _remote_nz


func _apply_remote_flags(s: Dictionary) -> void:
	_remote_thr = s.thr
	_remote_ab = s.ab
	gear_down = s.gear
	if alive and not s.alive:
		alive = false
		visual.visible = false
		vfx.explode(_remote_pos, _remote_vel)
		crashed.emit(_remote_pos)
	elif not alive and s.alive:
		alive = true
		visual.visible = true
		vfx.reset()


static func _hermite(p0: Vector3, m0: Vector3, p1: Vector3, m1: Vector3, u: float) -> Vector3:
	var u2 := u * u
	var u3 := u2 * u
	return p0 * (2 * u3 - 3 * u2 + 1) + m0 * (u3 - 2 * u2 + u) + p1 * (-2 * u3 + 3 * u2) + m1 * (u3 - u2)


# ---------------------------------------------------------------- per-frame visuals

func _process(delta: float) -> void:
	if is_local:
		if alive:
			global_transform = _prev.interpolate_with(_curr, Engine.get_physics_interpolation_fraction())
	else:
		_update_remote(delta)
	if alive:
		_animate_surfaces(delta)
		_animate_gear(delta)
	vfx.update_effects(delta, vfx_params())


func vfx_params() -> Dictionary:
	var thr: float = model.engine if is_local else _remote_thr
	var abl: float = model.ab if is_local else _remote_ab
	return {
		"alive": alive, "thr": thr, "ab": abl, "nz": model.nz, "alpha": model.alpha,
		"mach": model.mach, "alt": model.position.y, "tas": model.tas, "velocity": model.velocity,
		"agl": model.position.y - surface_height(model.position.x, model.position.z),
	}


func _animate_surfaces(delta: float) -> void:
	if _surfaces.is_empty() and not visual.has_method("set_control_surfaces"):
		return
	var w_local := Basis(model.orientation).inverse() * (model.angular_velocity_world() if is_local else _remote_w)
	var pitch_in: float
	var roll_in: float
	var yaw_in: float
	if is_local:
		pitch_in = model.in_pitch * 0.6 + clampf(model.rates.x / 0.5, -1.0, 1.0) * 0.4
		roll_in = model.in_roll
		yaw_in = model.in_yaw
	else:
		pitch_in = clampf(w_local.x / 0.4, -1.0, 1.0)
		roll_in = clampf(-w_local.z / 3.0, -1.0, 1.0)
		yaw_in = 0.0
	var k := 1.0 - exp(-delta * 12.0)
	_pitch_vis = lerpf(_pitch_vis, pitch_in, k)
	_roll_vis = lerpf(_roll_vis, roll_in, k)
	_yaw_vis = lerpf(_yaw_vis, yaw_in, k)
	var a := model.alpha
	var droop := deg_to_rad(20.0) if gear_down else deg_to_rad(15.0) * smoothstep(deg_to_rad(8.0), deg_to_rad(22.0), a)
	var toe := deg_to_rad(25.0) if brake else 0.0
	var targets := {
		"ElevonL": -deg_to_rad(18.0) * _pitch_vis + deg_to_rad(6.0) * _roll_vis,
		"ElevonR": -deg_to_rad(18.0) * _pitch_vis - deg_to_rad(6.0) * _roll_vis,
		"FlaperonL": droop + deg_to_rad(16.0) * _roll_vis,
		"FlaperonR": droop - deg_to_rad(16.0) * _roll_vis,
		"AileronL": droop * 0.5 + deg_to_rad(22.0) * _roll_vis,
		"AileronR": droop * 0.5 - deg_to_rad(22.0) * _roll_vis,
		"RudderL": deg_to_rad(20.0) * _yaw_vis + toe,
		"RudderR": deg_to_rad(20.0) * _yaw_vis - toe,
	}
	if visual.has_method("set_control_surfaces"):
		visual.call("set_control_surfaces", targets)
	for n: String in _surfaces:
		var cur: float = _surface_angle[n]
		cur = move_toward(cur, targets[n], SURFACE_RATE * delta)
		_surface_angle[n] = cur
		var entry: Array = _surfaces[n]
		(entry[0] as Node3D).transform.basis = (entry[1] as Basis) * Basis(Vector3.RIGHT, cur)


func _animate_gear(delta: float) -> void:
	_gear_anim = move_toward(_gear_anim, 1.0 if gear_down else 0.0, delta / 2.0)
	if visual.has_method("set_gear_extension"):
		visual.call("set_gear_extension", _gear_anim)
	if _gear_node == null:
		return
	_gear_node.visible = _gear_anim > 0.01
	var s := smoothstep(0.0, 1.0, _gear_anim)
	_gear_node.scale = Vector3(1.0, maxf(s, 0.01), 1.0)


# ---------------------------------------------------------------- telemetry

func telemetry() -> Dictionary:
	var m := model
	var ground := surface_height(m.position.x, m.position.z)
	var lever := 1.0 + m.in_ab if m.in_ab > 0.0 else m.in_throttle
	return {
		"ias_kt": m.cas / KT,
		"tas_kt": m.tas / KT,
		"mach": m.mach,
		"alt_ft": m.position.y / FT,
		"radar_alt_ft": (m.position.y - ground) / FT,
		"aoa_deg": rad_to_deg(m.alpha),
		"g": m.nz,
		"g_max": m.nz_max,
		"heading_deg": m.heading_deg(),
		"pitch_deg": m.pitch_deg(),
		"roll_deg": m.roll_deg(),
		"throttle": m.engine if is_local else _remote_thr,
		"lever": lever,
		"ab": (m.ab if is_local else _remote_ab) > 0.0,
		"ab_level": m.ab if is_local else _remote_ab,
		"fuel_kg": m.fuel,
		"vs_fpm": m.velocity.y / FT * 60.0,
		"gear": gear_down,
		"brake": brake,
		"alive": alive,
		"time_to_impact": time_to_impact,
		"speed_ms": m.tas,
	}
