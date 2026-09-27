# Third-person chase camera: lagged attitude follow biased toward the flight path, right-stick orbit with auto-recentre, speed FOV, G/AoA/transonic/low-level shake, terrain clearance.
class_name ChaseCamera
extends Camera3D

const DISTANCE := 19.0
const HEIGHT := 3.4
const AIM_HEIGHT := 1.6
const ORBIT_YAW_MAX := deg_to_rad(150.0)
const ORBIT_PITCH_MAX := deg_to_rad(55.0)
const RECENTRE_DELAY := 0.8
const FOV_SLOW := 62.0
const FOV_FAST := 76.0
const CLEARANCE := 2.5

var aircraft: Aircraft
var look_input := Vector2.ZERO       # set from Controls each frame (or by tests)
var look_back := false
var shake_scale := 1.0

var _frame := Quaternion.IDENTITY
var _orbit := Vector2.ZERO
var _orbit_target := Vector2.ZERO
var _look_idle := 0.0
var _shake_t := 0.0
var _shake_amp := 0.0
var _dist := DISTANCE
var _boom := 1.0
var _crash_point := Vector3.ZERO
var _crash_t := 0.0


func setup(a: Aircraft) -> void:
	aircraft = a
	top_level = true
	current = true
	near = 0.5
	far = 60000.0
	snap()


func snap() -> void:
	if aircraft == null:
		return
	_frame = _follow_rotation()
	_orbit = Vector2.ZERO
	_orbit_target = Vector2.ZERO
	_dist = DISTANCE
	_place(0.0)


func _follow_rotation() -> Quaternion:
	# forward halfway between the nose and the velocity vector, so AoA shows as the jet pitching against the path
	var b := aircraft.global_transform.basis.orthonormalized()
	var nose := -b.z
	var vel := aircraft.model.velocity
	var fwd := nose
	if vel.length() > 30.0 and aircraft.alive:
		fwd = nose.slerp(vel.normalized(), 0.55).normalized()
	var up := b.y
	var right := fwd.cross(up)
	if right.length_squared() < 1e-6:
		return b.get_rotation_quaternion()
	right = right.normalized()
	up = right.cross(fwd).normalized()
	return Basis(right, up, -fwd).get_rotation_quaternion()


func _process(delta: float) -> void:
	if aircraft == null:
		return
	var ctl := get_node_or_null(^"/root/Controls")
	if ctl and aircraft.use_controls:
		var inp: ControlInput = ctl.input
		look_input = inp.look
		look_back = inp.look_back
	_place(delta)


func _place(delta: float) -> void:
	if not aircraft.alive:
		_crash_view(delta)
		return
	_crash_t = 0.0
	var target_rot := _follow_rotation()
	var tas := aircraft.model.tas
	var k := 1.0 - exp(-delta * 7.0) if delta > 0.0 else 1.0
	_frame = _frame.slerp(target_rot, k).normalized()

	if look_back:
		_orbit_target = Vector2(PI, deg_to_rad(8.0))
		_look_idle = 0.0
	elif look_input.length() > 0.05:
		_orbit_target = Vector2(-look_input.x * ORBIT_YAW_MAX, look_input.y * ORBIT_PITCH_MAX)
		_look_idle = 0.0
	else:
		_look_idle += delta
		if _look_idle > RECENTRE_DELAY:
			_orbit_target = Vector2.ZERO
	var ko := 1.0 - exp(-delta * (10.0 if _look_idle == 0.0 else 3.5)) if delta > 0.0 else 1.0
	_orbit = _orbit.lerp(_orbit_target, ko)

	var basis_f := Basis(_frame)
	var orbit_b := basis_f * Basis(Vector3.UP, _orbit.x) * Basis(Vector3.RIGHT, -_orbit.y)
	var pivot := aircraft.global_transform.origin
	var offset := orbit_b * Vector3(0.0, HEIGHT, _dist)
	var cam_pos := pivot + offset
	var aim := pivot + basis_f.y * AIM_HEIGHT

	cam_pos = _terrain_clear(pivot, cam_pos, delta)
	var up := basis_f.y
	var xf := Transform3D(Basis.IDENTITY, cam_pos)
	var fwd := (aim - cam_pos).normalized()
	if absf(fwd.dot(up)) > 0.98:
		up = basis_f.z
	xf = xf.looking_at(aim, up)

	_shake_amp = lerpf(_shake_amp, _shake_level(), 1.0 - exp(-delta * 4.0) if delta > 0.0 else 1.0)
	_shake_t += delta
	if _shake_amp > 0.001:
		var t := _shake_t
		var sx := sin(t * 71.0) * 0.5 + sin(t * 113.0 + 1.3) * 0.3 + sin(t * 23.0 + 0.4) * 0.2
		var sy := sin(t * 83.0 + 2.1) * 0.5 + sin(t * 131.0 + 0.7) * 0.3 + sin(t * 29.0 + 2.9) * 0.2
		var a := deg_to_rad(_shake_amp) * shake_scale
		xf.basis = xf.basis * Basis(Vector3.RIGHT, sy * a) * Basis(Vector3.UP, sx * a)
	global_transform = xf
	var speed_t := smoothstep(90.0, 420.0, tas)
	var ab_kick := 3.0 * aircraft.model.ab
	fov = lerpf(fov, lerpf(FOV_SLOW, FOV_FAST, speed_t) + ab_kick, 1.0 - exp(-delta * 2.0) if delta > 0.0 else 1.0)


func _shake_level() -> float:
	var m := aircraft.model
	var s := 0.0
	s += smoothstep(4.5, 7.5, m.nz) * 0.35
	s += smoothstep(deg_to_rad(16.0), deg_to_rad(35.0), m.alpha) * 0.45
	s += (1.0 - smoothstep(0.0, 0.07, absf(m.mach - 1.0))) * 0.3
	var agl := m.position.y - aircraft.surface_height(m.position.x, m.position.z)
	s += (1.0 - smoothstep(20.0, 150.0, agl)) * smoothstep(150.0, 300.0, m.tas) * 0.22
	return s


func _terrain_clear(pivot: Vector3, cam_pos: Vector3, delta: float) -> Vector3:
	# pull the camera in along the boom when terrain is in the way (instant in, slow out), then keep it above the surface
	var dir := cam_pos - pivot
	var free := 1.0
	for i in range(1, 9):
		var f := float(i) / 8.0
		var q := pivot + dir * f
		if q.y < aircraft.surface_height(q.x, q.z) + CLEARANCE:
			free = maxf(float(i - 1) / 8.0, 0.25)
			break
	if free < _boom:
		_boom = free
	else:
		_boom = lerpf(_boom, free, 1.0 - exp(-delta * 2.0)) if delta > 0.0 else free
	var p := pivot + dir * _boom
	var g_end := aircraft.surface_height(p.x, p.z) + CLEARANCE
	if p.y < g_end:
		p.y = g_end
	return p


func _crash_view(delta: float) -> void:
	if _crash_t == 0.0:
		_crash_point = aircraft.global_transform.origin
	_crash_t += delta
	var ang := _crash_t * 0.15
	var d := 90.0 + _crash_t * 4.0
	var pos := _crash_point + Vector3(sin(ang) * d, 35.0 + _crash_t * 2.0, cos(ang) * d)
	pos.y = maxf(pos.y, aircraft.surface_height(pos.x, pos.z) + 10.0)
	global_transform = Transform3D(Basis.IDENTITY, pos).looking_at(_crash_point + Vector3(0, 15, 0), Vector3.UP)
