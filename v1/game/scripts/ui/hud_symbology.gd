# HMD symbology drawn every frame: flight path marker, conformal pitch ladder, speed/altitude boxes, heading tape, G/AoA, throttle, fuel, warnings, teammate tags, help.
extends Control

const ControlsScript := preload("res://scripts/flight/controls.gd")
const GREEN := Color(0.36, 1.0, 0.52, 0.95)
const GREEN_DIM := Color(0.36, 1.0, 0.52, 0.55)
const SHADOW := Color(0.0, 0.07, 0.02, 0.5)
const WARN := Color(1.0, 0.86, 0.3, 1.0)
const FAR := 4000.0
const LADDER_SPAN := 12.5
const CONFORMAL_LIMIT := 18.0

var font: SystemFont
var s := 1.0
var centre := Vector2.ZERO


func _ready() -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	font = SystemFont.new()
	font.font_names = PackedStringArray(["Bahnschrift", "DIN Alternate", "DIN Condensed", "Segoe UI", "Helvetica Neue", "Arial"])
	font.antialiasing = TextServer.FONT_ANTIALIASING_GRAY
	font.subpixel_positioning = TextServer.SUBPIXEL_POSITIONING_AUTO


func _hud() -> FlightHud:
	return get_parent() as FlightHud


func _draw() -> void:
	var hud := _hud()
	if hud == null or hud.aircraft == null or not is_instance_valid(hud.aircraft):
		return
	var cam := hud.active_camera()
	if cam == null:
		return
	s = size.y / 1080.0
	centre = size * 0.5
	var a: Aircraft = hud.aircraft
	var t := a.telemetry()
	if not a.alive:
		_text(centre + Vector2(0, -40) * s, "CRASHED", 40, HORIZONTAL_ALIGNMENT_CENTER, WARN)
		_help(hud)
		return
	var v := a.model.velocity
	var vdir := v.normalized() if v.length() > 5.0 else -a.global_transform.basis.z
	if rad_to_deg((-cam.global_transform.basis.z).angle_to(vdir)) < CONFORMAL_LIMIT:
		var fpm := _flight_path(cam, a)
		_ladder(cam, a, fpm)
		_boresight(cam, a)
	_speed_box(t)
	_alt_box(t)
	_heading_tape(t)
	_lower_blocks(t)
	_warnings(t)
	_teammates(cam, a, hud.teammates)
	_help(hud)


# ---------------------------------------------------------------- primitives

func _line(p0: Vector2, p1: Vector2, w: float = 1.6, col: Color = GREEN) -> void:
	draw_line(p0, p1, SHADOW, (w + 2.2) * s, true)
	draw_line(p0, p1, col, w * s, true)


func _poly(pts: PackedVector2Array, w: float = 1.6, col: Color = GREEN) -> void:
	draw_polyline(pts, SHADOW, (w + 2.2) * s, true)
	draw_polyline(pts, col, w * s, true)


func _circle(c: Vector2, r: float, w: float = 1.6, col: Color = GREEN) -> void:
	var pts := PackedVector2Array()
	for i in 25:
		var a := TAU * i / 24.0
		pts.append(c + Vector2(cos(a), sin(a)) * r)
	_poly(pts, w, col)


func _text(pos: Vector2, txt: String, px: float, align: int = HORIZONTAL_ALIGNMENT_LEFT, col: Color = GREEN) -> void:
	var fs := int(px * s)
	var w := font.get_string_size(txt, HORIZONTAL_ALIGNMENT_LEFT, -1, fs).x
	var p := pos
	if align == HORIZONTAL_ALIGNMENT_CENTER:
		p.x -= w * 0.5
	elif align == HORIZONTAL_ALIGNMENT_RIGHT:
		p.x -= w
	p.y += fs * 0.36
	draw_string_outline(font, p, txt, HORIZONTAL_ALIGNMENT_LEFT, -1, fs, int(4 * s), SHADOW)
	draw_string(font, p, txt, HORIZONTAL_ALIGNMENT_LEFT, -1, fs, col)


func _box(c: Vector2, half: Vector2, col: Color = GREEN) -> void:
	var pts := PackedVector2Array([c + Vector2(-half.x, -half.y), c + Vector2(half.x, -half.y), c + half, c + Vector2(-half.x, half.y), c + Vector2(-half.x, -half.y)])
	_poly(pts, 1.6, col)


func _project(cam: Camera3D, dir: Vector3) -> Variant:
	var wp := cam.global_position + dir * FAR
	if cam.is_position_behind(wp):
		return null
	return cam.unproject_position(wp)


static func _thousands(v: int) -> String:
	var neg := v < 0
	var str_v := str(absi(v))
	var out := ""
	while str_v.length() > 3:
		out = "," + str_v.substr(str_v.length() - 3) + out
		str_v = str_v.substr(0, str_v.length() - 3)
	return ("-" if neg else "") + str_v + out


# ---------------------------------------------------------------- symbols

func _flight_path(cam: Camera3D, a: Aircraft) -> Vector2:
	var v := a.model.velocity
	var dir := v.normalized() if v.length() > 5.0 else -a.global_transform.basis.z
	var p: Variant = _project(cam, dir)
	var fpm := centre
	var caged := false
	if p != null:
		fpm = p
	var lim := Vector2(size.x * 0.32, size.y * 0.36)
	var d := fpm - centre
	if absf(d.x) > lim.x or absf(d.y) > lim.y or p == null:
		fpm = centre + Vector2(clampf(d.x, -lim.x, lim.x), clampf(d.y, -lim.y, lim.y))
		caged = true
	var r := 10.0 * s
	var col := GREEN_DIM if caged else GREEN
	_circle(fpm, r, 1.8, col)
	_line(fpm + Vector2(-r, 0), fpm + Vector2(-r - 15.0 * s, 0), 1.8, col)
	_line(fpm + Vector2(r, 0), fpm + Vector2(r + 15.0 * s, 0), 1.8, col)
	_line(fpm + Vector2(0, -r), fpm + Vector2(0, -r - 9.0 * s), 1.8, col)
	return fpm


func _ladder(cam: Camera3D, a: Aircraft, fpm: Vector2) -> void:
	var v := a.model.velocity
	var fwd := v if v.length() > 5.0 else -a.global_transform.basis.z
	var h := Vector3(fwd.x, 0.0, fwd.z)
	if h.length_squared() < 1e-6:
		h = -a.global_transform.basis.z
		h.y = 0.0
	h = h.normalized()
	var right := h.cross(Vector3.UP).normalized()
	var fpm_pitch := rad_to_deg(asin(clampf(fwd.normalized().y, -1.0, 1.0)))
	var hz: Variant = _project(cam, h)
	for deg in range(-90, 95, 5):
		if absf(deg - fpm_pitch) > LADDER_SPAN:
			continue
		var th := deg_to_rad(deg)
		var dir := h * cos(th) + Vector3.UP * sin(th)
		var c0: Variant = _project(cam, dir)
		var c1: Variant = _project(cam, (dir * FAR + right * 200.0).normalized())
		if c0 == null or c1 == null:
			continue
		var c: Vector2 = c0
		var ax: Vector2 = ((c1 as Vector2) - c).normalized()
		c += ax * ax.dot(fpm - c)
		if c.distance_to(centre) > size.y * 0.3:
			continue
		var gap := 34.0 * s
		var ln := (230.0 if deg == 0 else 78.0) * s
		var tick := Vector2.ZERO
		if deg != 0 and hz != null:
			tick = ((hz as Vector2) - c).normalized() * 9.0 * s
		for side in [-1.0, 1.0]:
			var p0: Vector2 = c + ax * gap * side
			var p1: Vector2 = c + ax * (gap + ln) * side
			if deg < 0:
				var n := 5
				for k in n:
					var f0 := float(k) / n
					var f1 := f0 + 0.6 / n
					_line(p0.lerp(p1, f0), p0.lerp(p1, f1), 1.5)
			else:
				_line(p0, p1, 1.6 if deg != 0 else 1.9)
			if deg != 0:
				_line(p1, p1 + tick, 1.5)
				var lab := str(absi(deg))
				var lp: Vector2 = p1 + ax * side * 18.0 * s - tick * 0.3
				_text(lp, lab, 17, HORIZONTAL_ALIGNMENT_CENTER)


func _boresight(cam: Camera3D, a: Aircraft) -> void:
	var p: Variant = _project(cam, -a.global_transform.basis.z)
	if p == null:
		return
	var c: Vector2 = p
	var k := s
	var pts := PackedVector2Array([c + Vector2(-20, 0) * k, c + Vector2(-10, 0) * k, c + Vector2(-5, 8) * k, c + Vector2(0, 0) * k, c + Vector2(5, 8) * k, c + Vector2(10, 0) * k, c + Vector2(20, 0) * k])
	_poly(pts, 1.4, GREEN_DIM)


func _speed_box(t: Dictionary) -> void:
	var c := centre + Vector2(-330, 0) * s
	_box(c, Vector2(52, 19) * s)
	_text(c, "%d" % int(round(t.ias_kt)), 26, HORIZONTAL_ALIGNMENT_CENTER)
	_text(c + Vector2(0, -34) * s, "KCAS", 15, HORIZONTAL_ALIGNMENT_CENTER, GREEN_DIM)
	_text(c + Vector2(0, 36) * s, "M %.2f" % t.mach, 20, HORIZONTAL_ALIGNMENT_CENTER)


func _alt_box(t: Dictionary) -> void:
	var c := centre + Vector2(330, 0) * s
	_box(c, Vector2(62, 19) * s)
	var alt := int(round(t.alt_ft / 10.0) * 10.0)
	_text(c, _thousands(alt), 26, HORIZONTAL_ALIGNMENT_CENTER)
	var vs: float = t.vs_fpm
	_text(c + Vector2(0, -34) * s, "%s%s" % ["+" if vs >= 0.0 else "", _thousands(int(round(vs / 10.0) * 10.0))], 15, HORIZONTAL_ALIGNMENT_CENTER, GREEN_DIM)
	var ra: float = t.radar_alt_ft
	if ra < 5000.0:
		_text(c + Vector2(0, 36) * s, "R %s" % _thousands(int(maxf(ra, 0.0))), 20, HORIZONTAL_ALIGNMENT_CENTER)


func _heading_tape(t: Dictionary) -> void:
	var y := 96.0 * s
	var half_w := 190.0 * s
	var span := 30.0
	var hdg: float = t.heading_deg
	var ppd := half_w / span
	var start := int(floor((hdg - span) / 5.0)) * 5
	for d in range(start, int(hdg + span) + 6, 5):
		var off := (d - hdg) * ppd
		if absf(off) > half_w:
			continue
		var x := centre.x + off
		var major := posmod(d, 10) == 0
		_line(Vector2(x, y), Vector2(x, y - (12.0 if major else 6.0) * s), 1.4)
		if major and absf(off) > 32.0 * s:
			_text(Vector2(x, y - 26.0 * s), "%03d" % posmod(d, 360), 16, HORIZONTAL_ALIGNMENT_CENTER, GREEN_DIM)
	_line(Vector2(centre.x - half_w, y), Vector2(centre.x + half_w, y), 1.2, GREEN_DIM)
	var c := Vector2(centre.x, y - 26.0 * s)
	_box(c, Vector2(30, 14) * s)
	_text(c, "%03d" % (int(round(hdg)) % 360), 20, HORIZONTAL_ALIGNMENT_CENTER)
	_line(Vector2(centre.x, y + 2.0 * s), Vector2(centre.x - 6.0 * s, y + 11.0 * s), 1.4)
	_line(Vector2(centre.x, y + 2.0 * s), Vector2(centre.x + 6.0 * s, y + 11.0 * s), 1.4)


func _lower_blocks(t: Dictionary) -> void:
	var l := centre + Vector2(-330, 150) * s
	_text(l, "G  %.1f" % t.g, 22, HORIZONTAL_ALIGNMENT_CENTER)
	_text(l + Vector2(0, 28) * s, "%.1f" % t.g_max, 17, HORIZONTAL_ALIGNMENT_CENTER, GREEN_DIM)
	_text(l + Vector2(0, 58) * s, "α  %.1f" % t.aoa_deg, 22, HORIZONTAL_ALIGNMENT_CENTER)
	var r := centre + Vector2(330, 150) * s
	var thr_txt: String
	if t.ab:
		thr_txt = "AB  %d" % int(round(t.ab_level * 100.0))
	else:
		thr_txt = "THR %d" % int(round(minf(t.lever, 1.0) * 100.0))
	_text(r, thr_txt, 22, HORIZONTAL_ALIGNMENT_CENTER)
	var ctl := get_node_or_null(^"/root/Controls")
	if ctl and ctl.has_method("detent_progress"):
		var dp: float = ctl.detent_progress()
		if dp > 0.0 and not t.ab:
			var b0 := r + Vector2(-40, 16) * s
			_line(b0, b0 + Vector2(80.0 * dp, 0) * s, 3.0)
	var fuel_lb := int(round(t.fuel_kg * 2.20462 / 10.0) * 10.0)
	_text(r + Vector2(0, 30) * s, "FUEL %s" % _thousands(fuel_lb), 17, HORIZONTAL_ALIGNMENT_CENTER, GREEN_DIM)
	var tags := PackedStringArray()
	if t.gear:
		tags.append("GEAR")
	if t.brake:
		tags.append("BRK")
	if not tags.is_empty():
		_text(r + Vector2(0, 58) * s, " ".join(tags), 20, HORIZONTAL_ALIGNMENT_CENTER)


func _warnings(t: Dictionary) -> void:
	var tti: float = t.time_to_impact
	var blink := fmod(Time.get_ticks_msec() * 0.001, 0.5) < 0.32
	var c := centre + Vector2(0, 118) * s
	if tti < 3.5:
		if blink:
			_box(c, Vector2(92, 24) * s, WARN)
			_text(c, "PULL UP", 32, HORIZONTAL_ALIGNMENT_CENTER, WARN)
	elif tti < 7.0:
		if blink:
			_text(c, "ALTITUDE", 28, HORIZONTAL_ALIGNMENT_CENTER, WARN)
	elif t.fuel_kg < 900.0:
		_text(c, "BINGO FUEL", 22, HORIZONTAL_ALIGNMENT_CENTER, WARN)
	if t.aoa_deg > 35.0 and blink:
		_text(c + Vector2(0, 40) * s, "AOA", 22, HORIZONTAL_ALIGNMENT_CENTER, WARN)


func _teammates(cam: Camera3D, a: Aircraft, list: Array) -> void:
	var ring := size.y * 0.45
	for mate: Dictionary in list:
		var p: Vector3 = mate.get("position", Vector3.ZERO)
		var n := str(mate.get("name", "?"))
		var dist := a.global_position.distance_to(p)
		var nm := dist / 1852.0
		var dtxt := ("%.1f NM" % nm) if nm >= 0.5 else ("%d FT" % int(dist / 0.3048))
		var sp := Vector2.ZERO
		var on := false
		if not cam.is_position_behind(p):
			sp = cam.unproject_position(p)
			on = sp.x > 40 * s and sp.x < size.x - 40 * s and sp.y > 60 * s and sp.y < size.y - 60 * s
		if on:
			var k := 9.0 * s
			var pts := PackedVector2Array([sp + Vector2(0, -k), sp + Vector2(k, 0), sp + Vector2(0, k), sp + Vector2(-k, 0), sp + Vector2(0, -k)])
			_poly(pts, 1.6)
			_text(sp + Vector2(0, -24) * s, n, 17, HORIZONTAL_ALIGNMENT_CENTER)
			_text(sp + Vector2(0, 24) * s, dtxt, 15, HORIZONTAL_ALIGNMENT_CENTER, GREEN_DIM)
		else:
			var local := cam.global_transform.basis.inverse() * (p - cam.global_position)
			var d2 := Vector2(local.x, -local.y)
			if d2.length() < 1e-3:
				d2 = Vector2(0, 1)
			d2 = d2.normalized()
			var tip := centre + d2 * ring
			var perp := Vector2(-d2.y, d2.x)
			_poly(PackedVector2Array([tip - d2 * 16.0 * s + perp * 8.0 * s, tip, tip - d2 * 16.0 * s - perp * 8.0 * s]), 1.8)
			_text(tip - d2 * 36.0 * s, "%s %s" % [n, dtxt], 15, HORIZONTAL_ALIGNMENT_CENTER, GREEN_DIM)


func _help(hud: FlightHud) -> void:
	if not hud.show_help:
		_text(Vector2(28, size.y / s - 28) * s, "BACK · CONTROLS", 14, HORIZONTAL_ALIGNMENT_LEFT, GREEN_DIM)
		return
	var tl := Vector2(60, 250) * s
	var sz := Vector2(720, 60 + ControlsScript.LAYOUT_HELP.size() * 34) * s
	draw_rect(Rect2(tl, sz), Color(0.0, 0.05, 0.02, 0.62))
	_box(tl + sz * 0.5, sz * 0.5)
	_text(tl + Vector2(24, 30) * s, "CONTROLS  (BACK to close)", 20)
	var y := 72.0
	for row: Array in ControlsScript.LAYOUT_HELP:
		_text(tl + Vector2(24, y) * s, row[0], 18)
		_text(tl + Vector2(190, y) * s, row[1], 18, HORIZONTAL_ALIGNMENT_LEFT, GREEN_DIM)
		y += 34.0
