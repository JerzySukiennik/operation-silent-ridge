# Pure, deterministic F-35C flight model: ISA atmosphere, F135 thrust lapse, Mach/AoA aero tables and fly-by-wire control laws, stepped at a fixed tick.
class_name F35FlightModel
extends RefCounted

const G0 := 9.80665
const R_AIR := 287.053
const GAMMA_AIR := 1.4
const GRAVITY := Vector3(0.0, -G0, 0.0)

# LM F-35C fact sheet / Wikipedia specs (REFERENCES §1.2)
const WING_AREA := 62.1          # m², 668 ft²
const EMPTY_MASS := 15686.0      # kg, 34,581 lb
const FUEL_MAX := 8958.0         # kg, 19,750 lb internal (Wikipedia; LM sheet says 19,200 lb)
const PAYLOAD_MASS := 450.0      # kg, pilot + gun pod + ammo + CM [estimate]
const SPAWN_FUEL := 0.6          # fraction of internal fuel at air start [game choice]

# F135-PW-100, P&W product card 2022
const THRUST_MIL_SL := 124550.0  # N, 28,000 lbf intermediate
const THRUST_AB_SL := 191270.0   # N, 43,000 lbf max afterburner
const THRUST_IDLE_SL := 7000.0   # N, ~5 % of MIL [estimate]
const THROTTLE_RATIO := 1.07     # Mattingly 2002 low-bypass mixed-flow lapse break point [estimate]
const WET_LAPSE := 2.8           # Mattingly uses 3.5; lowered so M 1.06 at sea level and M 1.6 at altitude close with one drag polar [derived]
const DRY_LAPSE := 3.8           # Mattingly dry lapse slope
const THRUST_CURVE := 1.8        # lever -> dry thrust exponent, puts cruise near 45 % lever like rpm-based throttles [game tuning]
const TSFC_MIL := 0.0903         # kg/(N·h) = 0.886 lb/(lbf·h), F119/F135-class dry [estimate]
const TSFC_AB_INC := 0.255       # kg/(N·h) = 2.5 lb/(lbf·h) for the afterburner thrust increment [estimate]
const FUEL_IDLE_FLOW := 0.2      # kg/s floor [estimate]
const INLET_LOSS_EXP := 2.5      # thrust sensitivity to fixed DSI inlet normal-shock recovery above M 1 [estimate]

# Load and AoA limits (REFERENCES §1.2, §3)
const N_MAX := 7.5               # F-35C +7.5 g
const N_MIN := -3.0              # -3 g [estimate, typical FBW fighter]
const ALPHA_MAX_DEG := 50.0      # trimmed AoA capability
const ALPHA_MIN_DEG := -15.0     # [estimate]
const ALPHA_PEAK_DEG := 35.0     # CL max of the table below

# Low-speed lift curve (deg -> CL): AR 2.76 Helmbold slope 3.2/rad plus chine/LEX vortex lift, F-16-like shape [estimate]
const CL_ALPHA_DEG := [0.0, 5.0, 10.0, 15.0, 20.0, 25.0, 30.0, 35.0, 40.0, 45.0, 50.0, 60.0, 70.0, 80.0, 90.0]
const CL_TABLE := [0.0, 0.28, 0.57, 0.86, 1.13, 1.37, 1.55, 1.63, 1.60, 1.50, 1.38, 1.10, 0.78, 0.40, 0.0]
# Lift-slope Mach factor (relative to low speed) and CL max Mach factor [estimate, transonic rise then supersonic fall]
const MACH_X := [0.0, 0.6, 0.8, 0.9, 1.0, 1.1, 1.3, 1.6, 2.0]
const CLA_FACTOR := [1.0, 1.08, 1.17, 1.23, 1.19, 1.06, 0.91, 0.77, 0.63]
const CLMAX_FACTOR := [1.0, 0.95, 0.82, 0.74, 0.70, 0.72, 0.76, 0.80, 0.80]
# Zero-lift drag vs Mach, tuned to M 1.06 at sea level and M 1.6 at altitude with F135 thrust [derived]
const CD0_MACH := [0.0, 0.75, 0.85, 0.92, 0.97, 1.02, 1.08, 1.15, 1.3, 1.6, 2.0]
const CD0_TABLE := [0.0205, 0.0205, 0.0218, 0.0262, 0.0360, 0.0460, 0.0520, 0.0530, 0.0505, 0.0445, 0.0395]
# Induced drag factor K vs Mach [estimate: 1/(pi*e*AR) with e≈0.65 subsonic, rising supersonic]
const K_MACH := [0.0, 0.8, 0.95, 1.2, 1.6, 2.0]
const K_TABLE := [0.170, 0.172, 0.190, 0.225, 0.280, 0.330]

const CY_BETA := -0.9            # side force per rad of sideslip [estimate]
const CD_BRAKE := 0.035          # FCS "speedbrake" (rudders toe-in + flaps), F-16-class decel [estimate]
const CD_GEAR := 0.028           # gear + powered-approach flaps [estimate]
const CL_FLAPS := 0.22           # powered-approach flap lift: puts on-speed AoA near 12.3° at ~135 kt [derived]

# Control law gains and actuator/airframe time constants [game tuning]
const K_ALPHA := 3.0
const K_BETA := 3.0
const K_PATH := 0.6
const ROLL_RATE_MAX := deg_to_rad(200.0)
const PITCH_RATE_UP := deg_to_rad(45.0)
const PITCH_RATE_DOWN := deg_to_rad(30.0)
const TAU_ROLL := 0.09
const TAU_YAW := 0.15

# State
var position := Vector3.ZERO
var orientation := Quaternion.IDENTITY
var velocity := Vector3.ZERO
var rates := Vector3.ZERO        # x pitch q, y yaw r (right +), z roll p about the velocity vector (right wing down +), rad/s
var fuel := FUEL_MAX * SPAWN_FUEL
var engine := 0.8                # spooled dry power, 0 idle .. 1 MIL
var ab := 0.0                    # spooled afterburner, 0 off .. 1 max
var gear_down := false
var sim_time := 0.0

# Pilot inputs (already shaped by Controls)
var in_pitch := 0.0              # -1 push .. +1 pull
var in_roll := 0.0               # -1 left .. +1 right
var in_yaw := 0.0                # -1 left .. +1 right
var in_throttle := 0.8           # 0 idle .. 1 MIL
var in_ab := 0.0                 # 0 off, (0..1] afterburner min..max
var in_brake := false

# Derived air data (updated every step)
var mass := EMPTY_MASS + PAYLOAD_MASS + fuel
var rho := 1.225
var pressure := 101325.0
var temperature := 288.15
var sound_speed := 340.3
var tas := 0.0
var mach := 0.0
var qbar := 0.0
var cas := 0.0
var alpha := 0.0
var beta := 0.0
var thrust := 0.0
var fuel_flow := 0.0
var nz := 1.0                    # body-normal load factor (what the pilot feels)
var nz_max := 1.0
var cl := 0.0
var cd := 0.0
var path_angle := 0.0
var bank := 0.0
var alpha_cmd := 0.0
var n_cmd := 1.0
var authority := 1.0

var _ab_light := 0.0
var _path_hold := false
var _path_ref := 0.0
var _side_accel := 0.0
var _fwd := Vector3.FORWARD
var _up := Vector3.UP
var _right := Vector3.RIGHT
var _vhat := Vector3.FORWARD
var _lift_dir := Vector3.UP


func reset(xform: Transform3D, speed: float, fuel_fraction: float = SPAWN_FUEL) -> void:
	position = xform.origin
	orientation = xform.basis.get_rotation_quaternion().normalized()
	velocity = -xform.basis.z.normalized() * speed
	rates = Vector3.ZERO
	fuel = FUEL_MAX * clampf(fuel_fraction, 0.0, 1.0)
	engine = 0.8
	ab = 0.0
	_ab_light = 0.0
	in_pitch = 0.0
	in_roll = 0.0
	in_yaw = 0.0
	in_throttle = 0.8
	in_ab = 0.0
	in_brake = false
	_path_hold = false
	nz = 1.0
	nz_max = 1.0
	sim_time = 0.0
	_update_air()


func set_controls(pitch: float, roll: float, yaw: float, throttle: float, afterburner: float, brake: bool = false) -> void:
	in_pitch = clampf(pitch, -1.0, 1.0)
	in_roll = clampf(roll, -1.0, 1.0)
	in_yaw = clampf(yaw, -1.0, 1.0)
	in_throttle = clampf(throttle, 0.0, 1.0)
	in_ab = clampf(afterburner, 0.0, 1.0)
	in_brake = brake


func step(dt: float) -> void:
	_update_engine(dt)
	_update_air()
	_control_laws(dt)
	_rotate(dt)
	_update_air()
	_integrate(dt)
	sim_time += dt


func refresh_air_data() -> void:
	# recompute derived air data after position/orientation/velocity were set from outside (remote copies)
	_update_air()


func get_transform() -> Transform3D:
	return Transform3D(Basis(orientation), position)


func angular_velocity_world() -> Vector3:
	return Basis(orientation) * _local_omega()


func heading_deg() -> float:
	var f := _fwd
	return fposmod(rad_to_deg(atan2(f.x, -f.z)), 360.0)


func pitch_deg() -> float:
	return rad_to_deg(asin(clampf(_fwd.y, -1.0, 1.0)))


func roll_deg() -> float:
	var flat := Vector3(_fwd.x, 0.0, _fwd.z)
	if flat.length_squared() < 1e-6:
		return 0.0
	var level_right := flat.normalized().cross(Vector3.UP)
	return rad_to_deg(atan2(-_right.y, _right.dot(level_right)))


func throttle_display() -> float:
	return engine


# ---------------------------------------------------------------- engine

func _update_engine(dt: float) -> void:
	var target := in_throttle
	var want_ab := in_ab > 0.0 and fuel > 0.0
	if want_ab:
		target = 1.0
	if fuel <= 0.0:
		target = 0.0
	if target > engine:
		# idle -> MIL in ~3 s (F135-class spool-up) [estimate]
		engine = minf(target, engine + (0.18 + 0.35 * engine) * dt)
	else:
		engine = maxf(target, engine - 0.45 * dt)
	if want_ab and engine > 0.95:
		_ab_light += dt
		if _ab_light > 0.25:
			var ab_target := 0.15 + 0.85 * in_ab
			ab = move_toward(ab, ab_target, 1.2 * dt)
	else:
		_ab_light = 0.0
		ab = move_toward(ab, 0.0, 2.5 * dt)
	fuel = maxf(0.0, fuel - fuel_flow * dt)
	mass = EMPTY_MASS + PAYLOAD_MASS + fuel


static func isa(h: float) -> Vector3:
	# returns (temperature K, pressure Pa, density kg/m³), ISA to 20 km
	var hh := clampf(h, -500.0, 20000.0)
	var t: float
	var p: float
	if hh <= 11000.0:
		t = 288.15 - 0.0065 * hh
		p = 101325.0 * pow(t / 288.15, 5.25588)
	else:
		t = 216.65
		p = 22632.06 * exp(-G0 / (R_AIR * t) * (hh - 11000.0))
	return Vector3(t, p, p / (R_AIR * t))


static func normal_shock_recovery(m: float) -> float:
	if m <= 1.0:
		return 1.0
	var m2 := m * m
	var g := GAMMA_AIR
	var a := pow((g + 1.0) * m2 / ((g - 1.0) * m2 + 2.0), g / (g - 1.0))
	var b := pow((g + 1.0) / (2.0 * g * m2 - (g - 1.0)), 1.0 / (g - 1.0))
	return a * b


static func thrust_limits(h: float, m: float) -> Vector3:
	# (idle, MIL, max AB) thrust in N at altitude h (m) and Mach m: Mattingly lapse + fixed-inlet recovery
	var atm := isa(h)
	var theta := atm.x / 288.15
	var delta := atm.y / 101325.0
	var tr := 1.0 + 0.2 * m * m
	var theta0 := theta * tr
	var delta0 := delta * pow(tr, 3.5)
	var dry := delta0
	var wet := delta0
	if theta0 > THROTTLE_RATIO:
		dry = delta0 * (1.0 - DRY_LAPSE * (theta0 - THROTTLE_RATIO) / theta0)
		wet = delta0 * (1.0 - WET_LAPSE * (theta0 - THROTTLE_RATIO) / theta0)
	var rec := pow(normal_shock_recovery(m), INLET_LOSS_EXP)
	return Vector3(THRUST_IDLE_SL * delta0, THRUST_MIL_SL * maxf(dry, 0.0) * rec, THRUST_AB_SL * maxf(wet, 0.0) * rec)


func _compute_thrust() -> void:
	if fuel <= 0.0:
		thrust = 0.0
		fuel_flow = 0.0
		return
	var lim := thrust_limits(position.y, mach)
	var dry := lerpf(lim.x, lim.y, pow(engine, THRUST_CURVE))
	thrust = dry
	var ab_part := 0.0
	if ab > 0.0:
		ab_part = maxf(0.0, lim.z - lim.y) * ab
		thrust = dry + ab_part
	fuel_flow = maxf(FUEL_IDLE_FLOW, (dry * TSFC_MIL + ab_part * TSFC_AB_INC) / 3600.0)


# ---------------------------------------------------------------- aero

static func _table(xs: Array, ys: Array, x: float) -> float:
	var n := xs.size()
	if x <= xs[0]:
		return ys[0]
	if x >= xs[n - 1]:
		return ys[n - 1]
	for i in range(1, n):
		if x <= xs[i]:
			var t: float = (x - xs[i - 1]) / (xs[i] - xs[i - 1])
			return lerpf(ys[i - 1], ys[i], t)
	return ys[n - 1]


static func lift_coefficient(a: float, m: float, flaps: bool = false) -> float:
	var sgn := 1.0 if a >= 0.0 else -1.0
	var ad := absf(rad_to_deg(a))
	var reverse := 1.0
	if ad > 90.0:
		ad = 180.0 - ad
		reverse = -0.6
	var base: float = _table(CL_ALPHA_DEG, CL_TABLE, ad)
	var x := base * _table(MACH_X, CLA_FACTOR, m)
	var cap: float = CL_TABLE[7] * _table(MACH_X, CLMAX_FACTOR, m)
	if x > 0.7 * cap:
		x = cap * (0.7 + 0.3 * tanh((x / cap - 0.7) / 0.3))
	if sgn < 0.0:
		x *= 0.75
	var c := sgn * reverse * x
	if flaps and ad < 30.0:
		c += CL_FLAPS
	return c


static func drag_coefficient(a: float, m: float, c_l: float, gear: bool, brake: bool) -> float:
	var cd0: float = _table(CD0_MACH, CD0_TABLE, m)
	if gear:
		cd0 += CD_GEAR
	if brake:
		cd0 += CD_BRAKE
	var k: float = _table(K_MACH, K_TABLE, m)
	var aa := absf(a)
	if aa > PI * 0.5:
		aa = PI - aa
	var induced := k * c_l * c_l
	var separated := maxf(absf(c_l) * tan(minf(aa, deg_to_rad(60.0))), 1.85 * sin(aa) * sin(aa))
	var s := smoothstep(deg_to_rad(12.0), deg_to_rad(30.0), aa)
	return cd0 + lerpf(induced, separated, s)


static func alpha_limit(m: float) -> float:
	return deg_to_rad(lerpf(ALPHA_MAX_DEG, 25.0, smoothstep(0.6, 1.0, m)))


func alpha_for_cl(c_l: float, m: float, flaps: bool) -> float:
	var a_lim := alpha_limit(m)
	var a_peak := minf(deg_to_rad(ALPHA_PEAK_DEG), a_lim)
	var a_min := deg_to_rad(ALPHA_MIN_DEG)
	var cl_peak := lift_coefficient(a_peak, m, flaps)
	var cl_min := lift_coefficient(a_min, m, flaps)
	if c_l >= cl_peak:
		var over := clampf((c_l / maxf(cl_peak, 0.05) - 1.0) / 0.5, 0.0, 1.0)
		return a_peak + over * (a_lim - a_peak)
	if c_l <= cl_min:
		return a_min
	var lo := a_min
	var hi := a_peak
	for i in 14:
		var mid := 0.5 * (lo + hi)
		if lift_coefficient(mid, m, flaps) < c_l:
			lo = mid
		else:
			hi = mid
	return 0.5 * (lo + hi)


static func cas_from(m: float, p: float) -> float:
	# calibrated airspeed (m/s) from Mach and static pressure: subsonic isentropic, supersonic Rayleigh pitot
	var qc: float
	if m <= 1.0:
		qc = p * (pow(1.0 + 0.2 * m * m, 3.5) - 1.0)
	else:
		qc = p * (166.92158 * pow(m, 7.0) / pow(7.0 * m * m - 1.0, 2.5) - 1.0)
	var r := qc / 101325.0 + 1.0
	var x := 5.0 * (pow(r, 2.0 / 7.0) - 1.0)
	if x <= 1.0:
		return 340.294 * sqrt(maxf(x, 0.0))
	# supersonic CAS: iterate Rayleigh formula for pitot at sea level
	var v := 340.294 * sqrt(x)
	for i in 6:
		var mm := v / 340.294
		var ratio := 166.92158 * pow(mm, 7.0) / pow(7.0 * mm * mm - 1.0, 2.5)
		v *= pow(r / ratio, 0.5)
	return v


# ---------------------------------------------------------------- core

func _update_air() -> void:
	var b := Basis(orientation)
	_fwd = -b.z
	_up = b.y
	_right = b.x
	var atm := isa(position.y)
	temperature = atm.x
	pressure = atm.y
	rho = atm.z
	sound_speed = sqrt(GAMMA_AIR * R_AIR * temperature)
	tas = velocity.length()
	mach = tas / sound_speed
	qbar = 0.5 * rho * tas * tas
	cas = cas_from(mach, pressure)
	if tas > 1.0:
		_vhat = velocity / tas
		alpha = atan2(-velocity.dot(_up), velocity.dot(_fwd))
		beta = asin(clampf(velocity.dot(_right) / tas, -1.0, 1.0))
	else:
		_vhat = _fwd
		alpha = 0.0
		beta = 0.0
	var ld := _up - _vhat * _up.dot(_vhat)
	_lift_dir = ld.normalized() if ld.length_squared() > 1e-8 else _up
	path_angle = asin(clampf(_vhat.y, -1.0, 1.0))
	var vert := Vector3.UP - _vhat * _vhat.y
	if vert.length_squared() > 1e-6:
		vert = vert.normalized()
		var side := _vhat.cross(vert)
		bank = atan2(_lift_dir.dot(side), _lift_dir.dot(vert))
	else:
		bank = 0.0


func _control_laws(dt: float) -> void:
	var qs := maxf(qbar * WING_AREA, 1.0)
	var weight := mass * G0
	authority = clampf(qbar / 1800.0, 0.0, 1.0)
	var v := maxf(tas, 40.0)
	var flaps := gear_down

	# Pitch: neutral stick holds the flight path (bank-compensated 1 g), stick commands load factor to +7.5/-3 g
	var cb := cos(bank)
	var comp: float
	if cb >= 0.5:
		comp = 1.0 / cb
	elif cb >= 0.0:
		comp = 4.0 * cb
	else:
		comp = cb
	var n0 := cos(path_angle) * comp
	var n_hi := N_MAX
	var n_lo := N_MIN
	if gear_down:
		n_hi = 4.0
		n_lo = -1.0
	var s := in_pitch
	var n := n0 + (s * (n_hi - n0) if s >= 0.0 else s * (n0 - n_lo))
	if absf(s) < 0.03 and absf(bank) < deg_to_rad(70.0) and authority > 0.5:
		if not _path_hold:
			_path_hold = true
			_path_ref = path_angle
		n += clampf(tas / G0 * K_PATH * (_path_ref - path_angle), -1.0, 1.0)
	else:
		_path_hold = false
	n = clampf(n, n_lo, n_hi)
	n_cmd = n
	var c_l := (n * weight - thrust * sin(alpha)) / qs
	alpha_cmd = clampf(alpha_for_cl(c_l, mach, flaps), deg_to_rad(ALPHA_MIN_DEG), alpha_limit(mach))
	for i in 2:
		var cla := lift_coefficient(alpha_cmd, mach, flaps)
		var nb := (cla * cos(alpha_cmd) + drag_coefficient(alpha_cmd, mach, cla, gear_down, in_brake) * sin(alpha_cmd)) * qs / weight
		if nb > n_hi:
			c_l = cla * n_hi / nb
		elif nb < n_lo:
			c_l = cla * n_lo / nb
		else:
			break
		alpha_cmd = clampf(alpha_for_cl(c_l, mach, flaps), deg_to_rad(ALPHA_MIN_DEG), alpha_limit(mach))
	var n_ach := (lift_coefficient(alpha_cmd, mach, flaps) * qs + thrust * sin(alpha_cmd)) / weight
	var q_ff := (n_ach * G0 + GRAVITY.dot(_lift_dir)) / v
	var stiff := clampf(qbar / 8000.0, 0.0, 1.0)
	var q_cmd := q_ff + K_ALPHA * lerpf(0.4, 1.0, stiff) * (alpha_cmd - alpha)
	if nz > n_hi:
		q_cmd -= 0.15 * (nz - n_hi)
	elif nz < n_lo:
		q_cmd += 0.15 * (n_lo - nz)
	q_cmd = lerpf(-1.5 * alpha, q_cmd, authority)
	q_cmd = clampf(q_cmd, -PITCH_RATE_DOWN, PITCH_RATE_UP)
	var tau_q := 0.08 + 0.17 * (1.0 - stiff)
	rates.x += (q_cmd - rates.x) * (1.0 - exp(-dt / tau_q))

	# Roll: roll-rate command about the velocity vector, authority fades at low q and high AoA
	var p_max := ROLL_RATE_MAX * clampf(qbar / 9000.0, 0.3, 1.0)
	p_max *= lerpf(1.0, 0.35, smoothstep(deg_to_rad(15.0), deg_to_rad(45.0), alpha))
	p_max *= 1.0 - 0.3 * smoothstep(1.0, 1.6, mach)
	if gear_down:
		p_max *= 0.6
	var p_cmd := in_roll * p_max * maxf(authority, 0.15)
	rates.z += (p_cmd - rates.z) * (1.0 - exp(-dt / TAU_ROLL))

	# Yaw: automatic sideslip suppression; pedals command a small sideslip
	var beta_cmd := -in_yaw * deg_to_rad(lerpf(8.0, 2.0, clampf(qbar / 40000.0, 0.0, 1.0)))
	var r_cmd := _side_accel / v + K_BETA * (beta - beta_cmd)
	r_cmd = clampf(r_cmd, -0.6, 0.6)
	rates.y += (r_cmd - rates.y) * (1.0 - exp(-dt / TAU_YAW))


func _local_omega() -> Vector3:
	var vloc := Basis(orientation).inverse() * _vhat if tas > 20.0 else Vector3.FORWARD
	return Vector3(rates.x, -rates.y, 0.0) + vloc * rates.z


func _rotate(dt: float) -> void:
	var w := _local_omega()
	var ang := w.length() * dt
	if ang > 1e-9:
		orientation = (orientation * Quaternion(w.normalized(), ang)).normalized()


func _integrate(dt: float) -> void:
	_compute_thrust()
	var flaps := gear_down
	cl = lift_coefficient(alpha, mach, flaps)
	cd = drag_coefficient(alpha, mach, cl, gear_down, in_brake)
	var qs := qbar * WING_AREA
	var side_dir := _vhat.cross(_lift_dir)
	var f_aero := (_lift_dir * cl - _vhat * cd + side_dir * (CY_BETA * beta)) * qs
	var f_thrust := _fwd * thrust
	var f_ng := f_aero + f_thrust
	nz = f_ng.dot(_up) / (mass * G0)
	if sim_time > 0.5:
		nz_max = maxf(nz_max, nz)
	var accel := f_ng / mass + GRAVITY
	_side_accel = accel.dot(side_dir)
	velocity += accel * dt
	position += velocity * dt
