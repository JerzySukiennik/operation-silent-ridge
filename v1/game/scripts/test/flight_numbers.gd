# Headless flight-model check: flies scripted test-pilot manoeuvres with F35FlightModel and prints a table against F-35C reference numbers.
extends SceneTree

const Model := preload("res://scripts/flight/f35_flight_model.gd")
const DT := 1.0 / 120.0
const FT := 0.3048
const KT := 0.514444

var rows: Array = []
var failures := 0


func _init() -> void:
	var t0 := Time.get_ticks_msec()
	_trim_table()
	_approach()
	_sustained_turn()
	_instantaneous()
	_roll_rate()
	_top_speed(100.0, "top speed, max AB, sea level", "M 1.06 / 700 kt (W)")
	_top_speed(12200.0, "top speed, max AB, 40,000 ft", "M 1.6 (LM)")
	_top_speed_mil(100.0)
	_top_speed_mil(10668.0)
	_accel_transonic()
	_accel_sea_level()
	_climb()
	_energy_bleed()
	_zoom_climb()
	_stall()
	_response()
	_controls_logic()
	_determinism()
	_fuel()
	print("")
	print("F-35C flight model numbers  (sim dt = 1/120 s, 60 % fuel unless noted)")
	print("%-48s | %-26s | %s" % ["test", "model", "reference"])
	print("-".repeat(118))
	for r in rows:
		print("%-48s | %-26s | %s" % r)
	print("-".repeat(118))
	print("REPORT flight_numbers failures=%d elapsed_ms=%d" % [failures, Time.get_ticks_msec() - t0])
	quit(1 if failures > 0 else 0)


func row(name: String, value: String, ref: String) -> void:
	rows.append([name, value, ref])


func check(ok: bool, what: String) -> void:
	if not ok:
		failures += 1
		print("CHECK FAILED: " + what)


func new_model(alt_m: float, speed: float, fuel_frac: float = 0.6) -> F35FlightModel:
	var m: F35FlightModel = Model.new()
	m.reset(Transform3D(Basis.IDENTITY, Vector3(0, alt_m, 0)), speed, fuel_frac)
	return m


func mach_speed(alt_m: float, mach: float) -> float:
	var atm := Model.isa(alt_m)
	return mach * sqrt(1.4 * 287.053 * atm.x)


# power 0..1 = idle..MIL, 1..2 = AB min..max
func set_power(m: F35FlightModel, pitch: float, roll: float, power: float) -> void:
	var thr := clampf(power, 0.0, 1.0)
	var abv := clampf(power - 1.0, 0.0, 1.0)
	m.set_controls(pitch, roll, 0.0, thr, abv)


func fly(m: F35FlightModel, seconds: float, pitch: float, roll: float, power: float) -> void:
	for i in int(seconds / DT):
		set_power(m, pitch, roll, power)
		m.step(DT)


func hold_level_speed(m: F35FlightModel, v_target: float, seconds: float, max_power: float = 2.0) -> float:
	var power := 0.7
	var integ := 0.0
	for i in int(seconds / DT):
		var err := v_target - m.tas
		integ = clampf(integ + err * DT * 0.02, -1.0, 2.0)
		power = clampf(0.15 * err + integ + 0.5, 0.0, max_power)
		set_power(m, 0.0, 0.0, power)
		m.step(DT)
	return power


func _trim_table() -> void:
	for cfg in [[0.0, 250.0], [0.0, 350.0], [0.0, 450.0], [4572.0, 0.6], [4572.0, 0.8], [9144.0, 0.8]]:
		var alt: float = cfg[0]
		var v: float
		var label: String
		if cfg[1] > 2.0:
			v = cfg[1] * KT
			label = "level trim %d kt TAS, sea level" % int(cfg[1])
		else:
			v = mach_speed(alt, cfg[1])
			label = "level trim M %.1f, %d ft" % [cfg[1], int(alt / FT)]
		var m := new_model(maxf(alt, 150.0), v)
		var pw := hold_level_speed(m, v, 45.0)
		row(label, "AoA %.1f°  thr %s" % [rad_to_deg(m.alpha), _power_str(pw)], "cruise AoA 2–6° typical")
		check(absf(m.path_angle) < deg_to_rad(0.5), label + " path hold")


func _approach() -> void:
	var m := new_model(150.0, 140.0 * KT, 0.2)
	m.gear_down = true
	var pw := hold_level_speed(m, 135.0 * KT, 45.0)
	row("approach, gear down, 135 KTAS, 20 % fuel", "AoA %.1f°  thr %s  %.0f kg" % [rad_to_deg(m.alpha), _power_str(pw), m.mass], "on-speed AoA 12.3° @ 135–140 kt (sec.)")
	check(absf(rad_to_deg(m.alpha) - 12.3) < 3.0, "approach AoA")


func _power_str(pw: float) -> String:
	if pw <= 1.0:
		return "%d%%" % int(pw * 100.0)
	return "AB %d%%" % int((pw - 1.0) * 100.0)


func _sustained_turn() -> void:
	var alt := 4572.0
	var v := mach_speed(alt, 0.8)
	var m := new_model(alt, v, 0.5)
	fly(m, 3.0, 0.0, 0.0, 2.0)
	var bank_t := deg_to_rad(70.0)
	var acc := [0.0, 0.0, 0.0, 0.0]
	var n := 0
	for i in int(70.0 / DT):
		bank_t = clampf(bank_t + (m.tas - v) * 0.0006 * DT * 60.0, deg_to_rad(30.0), deg_to_rad(85.0))
		var roll := clampf((bank_t - m.bank) * 3.0, -1.0, 1.0)
		var n_need := 1.0 / maxf(cos(m.bank), 0.1)
		var pitch := clampf((n_need - 2.0) / (7.5 - 2.0) + (0.0 - m.path_angle) * 6.0 + (4572.0 - m.position.y) * 0.002, -1.0, 1.0)
		set_power(m, pitch, roll, 2.0)
		m.step(DT)
		if i * DT > 50.0:
			acc[0] += m.nz
			acc[1] += m.tas
			acc[2] += m.position.y
			var omega: float = G0() * sqrt(maxf(m.nz * m.nz - 1.0, 0.0)) / m.tas
			acc[3] += omega
			n += 1
	var nz: float = acc[0] / n
	var mach: float = (acc[1] / n) / sqrt(1.4 * 287.053 * Model.isa(alt).x)
	row("sustained turn, M 0.8, 15,000 ft, max AB, 50 % fuel", "%.2f g  %.1f°/s  (M %.2f)" % [nz, rad_to_deg(acc[3] / n), mach], "5.0 g (DOT&E FY2012 C spec)")
	check(nz > 4.3 and nz < 5.7, "sustained turn")


func G0() -> float:
	return Model.G0


func _instantaneous() -> void:
	for cfg in [[4572.0, 0.8, "full aft stick, M 0.8, 15,000 ft"], [150.0, 0.9, "full aft stick, M 0.9, sea level"], [150.0, 170.0, "full aft stick, 330 kt, sea level"]]:
		var alt: float = cfg[0]
		var v: float = mach_speed(alt, cfg[1]) if cfg[1] < 2.0 else cfg[1]
		var m := new_model(alt, v)
		fly(m, 1.0, 0.0, 0.0, 1.0)
		var max_rate := 0.0
		var max_a := 0.0
		for i in int(4.0 / DT):
			set_power(m, 1.0, 0.0, 1.0)
			m.step(DT)
			max_rate = maxf(max_rate, m.rates.x)
			max_a = maxf(max_a, m.alpha)
		row(cfg[2], "max %.2f g, %.0f°/s, AoA %.0f°" % [m.nz_max, rad_to_deg(max_rate), rad_to_deg(max_a)], "≤ 7.5 g (FBW limit)")
		check(m.nz_max < 7.8, cfg[2] + " g limit")
	var ms := new_model(150.0, 180.0)
	fly(ms, 1.0, 0.0, 0.0, 1.0)
	for i in int(3.0 / DT):
		set_power(ms, -1.0, 0.0, 1.0)
		ms.step(DT)
	var min_nz := 99.0
	var m2 := new_model(4572.0, mach_speed(4572.0, 0.8))
	fly(m2, 1.0, 0.0, 0.0, 1.0)
	for i in int(3.0 / DT):
		set_power(m2, -1.0, 0.0, 1.0)
		m2.step(DT)
		if i * DT > 0.3:
			min_nz = minf(min_nz, m2.nz)
	row("full forward stick, M 0.8, 15,000 ft", "min %.2f g" % min_nz, "≥ -3 g")
	check(min_nz > -3.4, "negative g limit")


func _roll_rate() -> void:
	var m := new_model(150.0, 400.0 * KT)
	var mx := 0.0
	var t360 := -1.0
	var rolled := 0.0
	for i in int(4.0 / DT):
		set_power(m, 0.0, 1.0, 1.0)
		m.step(DT)
		mx = maxf(mx, m.rates.z)
		rolled += m.rates.z * DT
		if t360 < 0.0 and rolled >= TAU:
			t360 = i * DT
	row("roll rate, 400 kt, sea level, full stick", "%.0f°/s, 360° in %.2f s" % [rad_to_deg(mx), t360], "~200°/s class [estimate]")


func _top_speed(alt: float, label: String, ref: String) -> void:
	var m := new_model(alt, mach_speed(alt, 1.0 if alt < 1000.0 else 1.45), 1.0)
	var last := 0.0
	for i in int(400.0 / DT):
		set_power(m, 0.0, 0.0, 2.0)
		m.step(DT)
		if i % 1200 == 0:
			if absf(m.tas - last) < 0.05:
				break
			last = m.tas
	row(label, "M %.2f  %d KCAS  %d KTAS" % [m.mach, int(m.cas / KT), int(m.tas / KT)], ref)
	if alt < 1000.0:
		check(m.mach > 1.0 and m.mach < 1.25, "top speed SL")
	else:
		check(m.mach > 1.45 and m.mach < 1.75, "top speed alt")


func _top_speed_mil(alt: float) -> void:
	var m := new_model(alt, mach_speed(alt, 0.8))
	for i in int(300.0 / DT):
		set_power(m, 0.0, 0.0, 1.0)
		m.step(DT)
	row("top speed, MIL, %d ft" % int(alt / FT), "M %.2f  %d KCAS" % [m.mach, int(m.cas / KT)], "no supercruise (W)")


func _accel_transonic() -> void:
	var alt := 9144.0
	var m := new_model(alt, mach_speed(alt, 0.8), 0.5)
	var t := 0.0
	while m.mach < 1.2 and t < 400.0:
		set_power(m, 0.0, 0.0, 2.0)
		m.step(DT)
		t += DT
	row("accel M 0.8 → 1.2, 30,000 ft, max AB, 50 % fuel", "%.0f s" % t, "~100 s (C spec +43 s vs A, DOT&E; sec.)")


func _accel_sea_level() -> void:
	var m := new_model(150.0, 250.0 * KT)
	var t := 0.0
	var t500 := -1.0
	var t600 := -1.0
	while t < 120.0:
		set_power(m, 0.0, 0.0, 2.0)
		m.step(DT)
		t += DT
		var kt := m.cas / KT
		if t500 < 0.0 and kt >= 500.0:
			t500 = t
		if t600 < 0.0 and kt >= 600.0:
			t600 = t
	row("accel 250 → 500 / 600 KCAS, sea level, max AB", "%.1f s / %.1f s" % [t500, t600], "—")


func _climb() -> void:
	var best := 0.0
	var best_v := 0.0
	var m := new_model(150.0, 200.0)
	for kt in range(250, 700, 10):
		var v := kt * KT
		var ps := _specific_excess_power(m, 150.0, v, 1.0, true)
		if ps > best:
			best = ps
			best_v = kt
	var best_mil := 0.0
	for kt in range(250, 650, 10):
		best_mil = maxf(best_mil, _specific_excess_power(m, 150.0, kt * KT, 1.0, false))
	row("max rate of climb, sea level (Ps at 1 g)", "%d ft/min AB @ %d kt, %d MIL" % [int(best / FT * 60.0), int(best_v), int(best_mil / FT * 60.0)], "~45,000 ft/min class [estimate]")


func _specific_excess_power(m: F35FlightModel, alt: float, v: float, n: float, ab: bool) -> float:
	var atm := Model.isa(alt)
	var a := sqrt(1.4 * 287.053 * atm.x)
	var mach := v / a
	var q := 0.5 * atm.z * v * v
	var w := m.mass * Model.G0
	var c_l := n * w / (q * Model.WING_AREA)
	var al := m.alpha_for_cl(c_l, mach, false)
	var c_d := Model.drag_coefficient(al, mach, c_l, false, false)
	var lim := Model.thrust_limits(alt, mach)
	var t: float = lim.z if ab else lim.y
	return (t - q * Model.WING_AREA * c_d) * v / w


func _energy_bleed() -> void:
	var m := new_model(300.0, 450.0 * KT)
	fly(m, 1.0, 0.0, 0.0, 1.0)
	var v0 := m.cas
	var v3 := 0.0
	for i in int(6.0 / DT):
		var roll := clampf((deg_to_rad(84.0) - m.bank) * 3.0, -1.0, 1.0)
		set_power(m, 1.0 if i * DT > 0.4 else 0.0, roll, 1.0)
		m.step(DT)
		if i == int(3.0 / DT):
			v3 = m.cas
	row("7.5 g turn from 450 KCAS, sea level, MIL", "%d → %d → %d KCAS (0/3/6 s)" % [int(v0 / KT), int(v3 / KT), int(m.cas / KT)], "big bleed: speed falls toward corner")
	check(m.cas < v0 - 20.0 * KT, "high-g bleed")


func _zoom_climb() -> void:
	var m := new_model(150.0, 600.0 * KT)
	var h0 := m.position.y
	var max_h := h0
	var phase := 0
	for i in int(80.0 / DT):
		var pitch := 0.0
		if phase == 0:
			pitch = 0.6
			if m.path_angle > deg_to_rad(60.0):
				phase = 1
		set_power(m, pitch, 0.0, 1.0)
		m.step(DT)
		max_h = maxf(max_h, m.position.y)
		if phase == 1 and m.cas < 150.0 * KT:
			break
	row("zoom 600 KCAS → 150 KCAS, 60° climb, MIL", "+%d ft" % int((max_h - h0) / FT), "energy height 600 kt ≈ 15,900 ft + thrust")


func _stall() -> void:
	var m := new_model(4572.0, 250.0 * KT)
	var min_kt := 999.0
	var max_a := 0.0
	var min_pitch := 90.0
	for i in int(40.0 / DT):
		set_power(m, 1.0, 0.0, 0.0)
		m.step(DT)
		min_kt = minf(min_kt, m.cas / KT)
		max_a = maxf(max_a, m.alpha)
		if i * DT > 15.0:
			min_pitch = minf(min_pitch, m.pitch_deg())
	row("idle + full aft stick 40 s, 15,000 ft", "AoA max %.1f°, min %d KCAS" % [rad_to_deg(max_a), int(min_kt)], "AoA limiter 50°, no departure")
	check(rad_to_deg(max_a) < 52.0, "AoA limit")
	var m2 := new_model(3000.0, 200.0)
	m2.orientation = Quaternion(Vector3.RIGHT, deg_to_rad(85.0))
	m2.velocity = (Basis(m2.orientation) * Vector3.FORWARD) * 120.0
	var min_v := 999.0
	for i in int(25.0 / DT):
		set_power(m2, 0.0, 0.0, 0.0)
		m2.step(DT)
		min_v = minf(min_v, m2.tas)
	row("vertical zoom to zero speed, idle, neutral", "min %d m/s → recovers at %d kt, pitch %d°" % [int(min_v), int(m2.cas / KT), int(m2.pitch_deg())], "nose falls through, no lock-up")
	check(m2.cas > 80.0 * KT, "tail slide recovery")


func _response() -> void:
	var m := new_model(1500.0, 180.0)
	fly(m, 1.0, 0.0, 0.0, 0.8)
	var t90 := -1.0
	var t := 0.0
	while t < 4.0 and t90 < 0.0:
		set_power(m, 0.0, 1.0, 0.8)
		m.step(DT)
		t += DT
		if m.bank >= deg_to_rad(90.0):
			t90 = t
	row("time to 90° bank, full stick, 350 kt", "%.2f s" % t90, "F-16 class ~0.6–0.8 s [estimate]")
	var m2 := new_model(1500.0, 206.0)
	fly(m2, 1.0, 0.0, 0.0, 1.0)
	var target := 5.0
	var stick := (target - 1.0) / (7.5 - 1.0)
	var t63 := -1.0
	var t90g := -1.0
	var over := 0.0
	t = 0.0
	while t < 3.0:
		set_power(m2, stick, 0.0, 1.0)
		m2.step(DT)
		t += DT
		if t63 < 0.0 and m2.nz >= 1.0 + 0.63 * (target - 1.0):
			t63 = t
		if t90g < 0.0 and m2.nz >= 1.0 + 0.9 * (target - 1.0):
			t90g = t
		over = maxf(over, m2.nz - target)
	row("5 g pull-up step, 400 kt: 63 % / 90 % / overshoot", "%.2f s / %.2f s / %+.2f g" % [t63, t90g, over], "crisp, <10 % overshoot")
	check(t90g > 0.0 and t90g < 1.5 and over < 0.5, "pitch response")
	var m3 := new_model(1500.0, 130.0)
	fly(m3, 1.0, 0.0, 0.0, 0.8)
	var h0 := m3.heading_deg()
	for i in int(3.0 / DT):
		m3.set_controls(0.0, 0.0, 1.0, 0.8, 0.0)
		m3.step(DT)
	row("full right pedal 3 s, 250 kt", "β %.1f°, heading %+.1f°, bank %.1f°" % [rad_to_deg(m3.beta), m3.heading_deg() - h0, rad_to_deg(m3.bank)], "nose right (β<0), small flat turn")
	check(m3.heading_deg() - h0 > 0.5 and m3.beta < 0.0, "rudder sign")


func _controls_logic() -> void:
	var ControlsScript: Script = load("res://scripts/flight/controls.gd")
	var c: Node = ControlsScript.new()
	c.set_lever(0.5)
	for i in 240:
		c._update_lever(DT, 1.0, 0.0)
	var at_mil: float = c.lever()
	c.set_lever(0.99)
	for i in 3:
		c._update_lever(DT, 1.0, 0.0)
	var short_hold: float = c.lever()
	for i in 60:
		c._update_lever(DT, 1.0, 0.0)
	var no_release: float = c.lever()
	for i in 5:
		c._update_lever(DT, 0.0, 0.0)
	for i in 60:
		c._update_lever(DT, 1.0, 0.0)
	var after_hold: float = c.lever()
	for i in 120:
		c._update_lever(DT, 0.0, 1.0)
	var back: float = c.lever()
	c.free()
	var ok := absf(at_mil - 1.0) < 1e-3 and short_hold <= 1.0 and no_release <= 1.0 + 1e-3 and after_hold > 1.3 and back < 0.9
	row("throttle detent: MIL stop / re-press+hold → AB / LT", "%.2f / %.2f / %.2f" % [no_release, after_hold, back], "1.00 / >1 (AB) / <1")
	check(ok, "throttle detent")
	var e: float = ControlsScript.expo(0.1, 0.55)
	row("stick shaping: 10 % / 50 % pitch stick", "%.3f / %.3f of full command" % [e, ControlsScript.expo(0.5, 0.55)], "fine near centre")


func _determinism() -> void:
	var a := new_model(3000.0, 220.0)
	var b := new_model(3000.0, 220.0)
	for i in int(20.0 / DT):
		var p := sin(i * 0.013) * 0.8
		var r := cos(i * 0.007) * 0.9
		a.set_controls(p, r, 0.1, 0.9, 0.0)
		b.set_controls(p, r, 0.1, 0.9, 0.0)
		a.step(DT)
		b.step(DT)
	var same := a.position == b.position and a.orientation == b.orientation
	row("determinism (two runs, 20 s random input)", "identical" if same else "DIFFERENT", "bit-identical")
	check(same, "determinism")


func _fuel() -> void:
	var m := new_model(150.0, 250.0)
	fly(m, 5.0, 0.0, 0.0, 1.0)
	var mil := m.fuel_flow
	fly(m, 5.0, 0.0, 0.0, 2.0)
	var abf := m.fuel_flow
	row("fuel flow, sea level, MIL / max AB", "%.1f / %.1f kg/s" % [mil, abf], "≈3 / ≈9 kg/s from TSFC [estimate]")
	row("full-fuel AB endurance at SL", "%.1f min" % (Model.FUEL_MAX / abf / 60.0), "—")
