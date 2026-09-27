# Title menu: pilot name, host a public mission, live room list (select a row to join), offline solo fallback, settings placeholder, quit; fully gamepad navigable.
class_name MainMenu
extends Control

signal enter_game

const BG := Color(0.02, 0.027, 0.035)
const GREEN := Color(0.36, 1.0, 0.58)
const TEXT := Color(0.86, 0.91, 0.88)
const DIM := Color(0.5, 0.57, 0.54)
const AMBER := Color(0.96, 0.73, 0.32)
const RED := Color(1.0, 0.45, 0.38)
const PROFILE := "user://profile.cfg"
const BG_SHADER := """
shader_type canvas_item;
uniform vec3 green = vec3(0.36, 1.0, 0.58);
uniform vec2 px = vec2(1920.0, 1080.0);
float h1(float x) { return fract(sin(x * 127.1) * 43758.5453); }
float n1(float x) { float i = floor(x); float f = fract(x); f = f * f * (3.0 - 2.0 * f); return mix(h1(i), h1(i + 1.0), f); }
float fbm(float x) { float a = 0.5; float s = 0.0; for (int i = 0; i < 5; i++) { s += a * n1(x); x *= 2.03; a *= 0.5; } return s; }
void fragment() {
	vec2 uv = UV;
	vec2 p = uv * px;
	vec3 col = mix(vec3(0.028, 0.038, 0.044), vec3(0.016, 0.02, 0.026), uv.y);
	vec2 g = abs(fract(p / 96.0 - 0.5) - 0.5) * 96.0;
	col += green * (1.0 - smoothstep(0.0, 1.0, min(g.x, g.y))) * 0.018;
	for (int i = 0; i < 6; i++) {
		float fi = float(i);
		float hgt = 0.60 + fi * 0.06 + (fbm(uv.x * (2.2 + fi * 0.4) + fi * 11.7 + TIME * 0.006 * (fi + 1.0)) - 0.5) * (0.30 - fi * 0.03);
		float d = (uv.y - hgt) * px.y;
		col += green * (1.0 - smoothstep(0.0, 1.4, abs(d))) * (0.11 - fi * 0.014);
		col *= mix(1.0, 0.93, step(0.0, d));
	}
	float v = smoothstep(1.3, 0.3, length((uv - 0.5) * vec2(1.5, 1.0)));
	col *= mix(0.45, 1.0, v);
	col *= 0.95 + 0.05 * sin(p.y * 3.14159);
	COLOR = vec4(col, 1.0);
}
"""

@export var game_scene := "res://scenes/game.tscn"
@export var auto_enter := true
@export var browse := true

var font_ui: Font
var font_title: Font
var font_mono: Font
var _name: LineEdit
var _host_btn: Button
var _solo_btn: Button
var _settings_btn: Button
var _quit_btn: Button
var _rows_box: VBoxContainer
var _rows := {}
var _empty: Label
var _count: Label
var _link: Label
var _status: Label
var _clock: Label
var _settings: Control
var _settings_back: Button
var _row_styles := {}
var _entering := false
var _join_target := ""


func _ready() -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)
	process_mode = Node.PROCESS_MODE_ALWAYS
	_ensure_gamepad_ui_actions()
	_make_fonts()
	_build()
	Net.state_changed.connect(_on_net_state)
	Net.rooms_updated.connect(_on_rooms)
	Net.lobby_status_changed.connect(_on_lobby)
	if Net.state == "failed" and Net.last_error != "":
		_set_status(Net.last_error, RED)
	elif Net.is_online() or Net.state == "solo":
		Net.leave()
	if browse:
		Net.start_browsing()
		_on_lobby_pending()
	_host_btn.grab_focus.call_deferred()


func _exit_tree() -> void:
	if browse:
		Net.stop_browsing()


func _make_fonts() -> void:
	var sys := SystemFont.new()
	sys.font_names = PackedStringArray(["Bahnschrift", "DIN Alternate", "DIN Condensed", "Arial Narrow", "Segoe UI", "Helvetica Neue", "sans-serif"])
	sys.font_weight = 500
	sys.font_stretch = 87
	var ui := FontVariation.new()
	ui.base_font = sys
	ui.spacing_glyph = 1
	font_ui = ui
	var tsys := SystemFont.new()
	tsys.font_names = sys.font_names
	tsys.font_weight = 600
	tsys.font_stretch = 75
	var title := FontVariation.new()
	title.base_font = tsys
	title.spacing_glyph = 6
	font_title = title
	var mono := SystemFont.new()
	mono.font_names = PackedStringArray(["Consolas", "Menlo", "SF Mono", "monospace"])
	font_mono = mono


func _label(text: String, size: int, color: Color, font: Font = null) -> Label:
	var l := Label.new()
	l.text = text
	l.add_theme_font_override("font", font if font else font_ui)
	l.add_theme_font_size_override("font_size", size)
	l.add_theme_color_override("font_color", color)
	return l


func _box(bg: Color, border: Color, bw := 1, left_bar := 0) -> StyleBoxFlat:
	var sb := StyleBoxFlat.new()
	sb.bg_color = bg
	sb.border_color = border
	sb.set_border_width_all(bw)
	if left_bar > 0:
		sb.border_width_left = left_bar
	sb.set_corner_radius_all(1)
	sb.content_margin_left = 22
	sb.content_margin_right = 16
	return sb


func _button_styles() -> Dictionary:
	return {
		"normal": _box(Color(GREEN, 0.02), Color(GREEN, 0.18)),
		"hover": _box(Color(GREEN, 0.07), Color(GREEN, 0.45)),
		"focus": _box(Color(GREEN, 0.0), Color(GREEN, 0.95), 1, 4),
		"pressed": _box(Color(GREEN, 0.2), GREEN, 1, 4),
		"disabled": _box(Color(0, 0, 0, 0), Color(DIM, 0.12)),
		"hover_pressed": _box(Color(GREEN, 0.2), GREEN, 1, 4),
	}


func _menu_button(text: String, primary := false) -> Button:
	var b := Button.new()
	b.text = text
	b.alignment = HORIZONTAL_ALIGNMENT_LEFT
	b.custom_minimum_size = Vector2(0, 64 if primary else 54)
	b.add_theme_font_override("font", font_ui)
	b.add_theme_font_size_override("font_size", 26 if primary else 21)
	b.add_theme_color_override("font_color", TEXT if primary else Color(TEXT, 0.82))
	b.add_theme_color_override("font_focus_color", GREEN)
	b.add_theme_color_override("font_hover_color", Color(0.95, 1.0, 0.96))
	b.add_theme_color_override("font_pressed_color", Color(0.95, 1.0, 0.96))
	b.add_theme_color_override("font_disabled_color", Color(DIM, 0.5))
	var st := _button_styles()
	if primary:
		st.normal = _box(Color(GREEN, 0.07), Color(GREEN, 0.5))
	for k: String in st:
		b.add_theme_stylebox_override(k, st[k])
	return b


func _build() -> void:
	var bg := ColorRect.new()
	bg.set_anchors_preset(Control.PRESET_FULL_RECT)
	bg.color = BG
	bg.mouse_filter = Control.MOUSE_FILTER_IGNORE
	var sm := ShaderMaterial.new()
	var sh := Shader.new()
	sh.code = BG_SHADER
	sm.shader = sh
	bg.material = sm
	add_child(bg)

	var top := _label("CVN STRIKE GROUP  //  CO-OP 1–4  //  NET %s" % Net.version, 15, Color(GREEN, 0.55), font_mono)
	top.position = Vector2(64, 40)
	add_child(top)
	_clock = _label("", 15, Color(GREEN, 0.55), font_mono)
	_clock.set_anchors_preset(Control.PRESET_TOP_RIGHT)
	_clock.position = Vector2(-260, 40)
	_clock.custom_minimum_size = Vector2(200, 0)
	_clock.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	add_child(_clock)

	var col := VBoxContainer.new()
	col.position = Vector2(120, 150)
	col.custom_minimum_size = Vector2(540, 0)
	col.size = Vector2(540, 800)
	col.add_theme_constant_override("separation", 12)
	add_child(col)
	col.add_child(_label("CARRIER STRIKE  ·  LOW LEVEL INGRESS", 17, Color(GREEN, 0.75)))
	var t1 := _label("OPERATION", 60, Color(TEXT, 0.72), font_title)
	col.add_child(t1)
	var t2 := _label("SILENT RIDGE", 92, TEXT, font_title)
	t2.add_theme_constant_override("line_spacing", -12)
	col.add_child(t2)
	var rule := _Rule.new()
	rule.custom_minimum_size = Vector2(540, 10)
	col.add_child(rule)
	_spacer(col, 26)

	col.add_child(_label("PILOT", 16, Color(GREEN, 0.7)))
	_name = LineEdit.new()
	_name.placeholder_text = "ENTER NAME"
	_name.max_length = 16
	_name.custom_minimum_size = Vector2(0, 58)
	_name.add_theme_font_override("font", font_ui)
	_name.add_theme_font_size_override("font_size", 28)
	_name.add_theme_color_override("font_color", TEXT)
	_name.add_theme_color_override("font_placeholder_color", Color(DIM, 0.6))
	_name.add_theme_color_override("caret_color", GREEN)
	_name.add_theme_color_override("selection_color", Color(GREEN, 0.3))
	var n_normal := _box(Color(0, 0, 0, 0.35), Color(GREEN, 0.0))
	n_normal.border_width_bottom = 1
	n_normal.border_color = Color(GREEN, 0.35)
	var n_focus := n_normal.duplicate() as StyleBoxFlat
	n_focus.border_color = GREEN
	n_focus.set_border_width_all(1)
	n_focus.border_width_left = 4
	_name.add_theme_stylebox_override("normal", n_normal)
	_name.add_theme_stylebox_override("focus", n_focus)
	_name.text = _load_name()
	_name.text_changed.connect(func(_t: String) -> void: _save_name())
	_name.text_submitted.connect(func(_t: String) -> void: _host_btn.grab_focus())
	col.add_child(_name)
	_spacer(col, 18)

	_host_btn = _menu_button("HOST MISSION", true)
	_host_btn.pressed.connect(_on_host)
	col.add_child(_host_btn)
	_solo_btn = _menu_button("FLY SOLO  (OFFLINE)")
	_solo_btn.visible = false
	_solo_btn.pressed.connect(_on_solo)
	col.add_child(_solo_btn)
	_settings_btn = _menu_button("SETTINGS")
	_settings_btn.pressed.connect(_open_settings)
	col.add_child(_settings_btn)
	_quit_btn = _menu_button("QUIT")
	_quit_btn.pressed.connect(func() -> void: get_tree().quit())
	col.add_child(_quit_btn)

	var panel := PanelContainer.new()
	panel.position = Vector2(730, 230)
	panel.size = Vector2(1120, 640)
	var psb := StyleBoxFlat.new()
	psb.bg_color = Color(0.015, 0.024, 0.028, 0.86)
	psb.border_color = Color(GREEN, 0.14)
	psb.set_border_width_all(1)
	psb.content_margin_left = 28
	psb.content_margin_right = 28
	psb.content_margin_top = 22
	psb.content_margin_bottom = 22
	panel.add_theme_stylebox_override("panel", psb)
	add_child(panel)
	var brackets := _Brackets.new()
	brackets.set_anchors_preset(Control.PRESET_FULL_RECT)
	brackets.mouse_filter = Control.MOUSE_FILTER_IGNORE
	panel.add_child(brackets)
	var pv := VBoxContainer.new()
	pv.add_theme_constant_override("separation", 10)
	panel.add_child(pv)
	var head := HBoxContainer.new()
	head.add_theme_constant_override("separation", 18)
	pv.add_child(head)
	head.add_child(_label("OPEN MISSIONS", 26, TEXT, font_title))
	_count = _label("", 20, Color(GREEN, 0.8), font_mono)
	_count.custom_minimum_size = Vector2(90, 0)
	_count.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	head.add_child(_count)
	var fill := Control.new()
	fill.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	head.add_child(fill)
	_link = _label("", 16, DIM, font_mono)
	_link.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	head.add_child(_link)
	var hdr := MenuRoomRow.header(font_mono, Color(GREEN, 0.5))
	var hdr_wrap := MarginContainer.new()
	hdr_wrap.add_theme_constant_override("margin_left", 14)
	hdr_wrap.add_child(hdr)
	pv.add_child(hdr_wrap)
	var sep := ColorRect.new()
	sep.color = Color(GREEN, 0.18)
	sep.custom_minimum_size = Vector2(0, 1)
	pv.add_child(sep)
	var scroll := ScrollContainer.new()
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	scroll.follow_focus = true
	pv.add_child(scroll)
	_rows_box = VBoxContainer.new()
	_rows_box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_rows_box.add_theme_constant_override("separation", 6)
	scroll.add_child(_rows_box)
	_empty = _label("No open missions yet.\nHost one: every mission is public, so other pilots will see it here.", 19, DIM)
	_empty.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_empty.custom_minimum_size = Vector2(1000, 0)
	_rows_box.add_child(_empty)

	_row_styles = _button_styles()
	for k: String in _row_styles:
		(_row_styles[k] as StyleBoxFlat).content_margin_left = 0

	_status = _label("", 20, TEXT)
	_status.position = Vector2(120, 950)
	_status.size = Vector2(1400, 40)
	_status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	add_child(_status)
	var legend := HBoxContainer.new()
	legend.add_theme_constant_override("separation", 10)
	legend.set_anchors_preset(Control.PRESET_BOTTOM_RIGHT)
	legend.grow_horizontal = Control.GROW_DIRECTION_BEGIN
	legend.position = Vector2(-560, -76)
	legend.size = Vector2(490, 30)
	legend.alignment = BoxContainer.ALIGNMENT_END
	for pair: Array in [["A", "SELECT"], ["B", "BACK"], ["D-PAD", "MOVE"]]:
		var badge := _label(pair[0], 14, GREEN, font_mono)
		var bsb := StyleBoxFlat.new()
		bsb.bg_color = Color(GREEN, 0.06)
		bsb.border_color = Color(GREEN, 0.6)
		bsb.set_border_width_all(1)
		bsb.content_margin_left = 7
		bsb.content_margin_right = 7
		bsb.content_margin_top = 1
		bsb.content_margin_bottom = 1
		badge.add_theme_stylebox_override("normal", bsb)
		legend.add_child(badge)
		var lt := _label(pair[1], 15, Color(TEXT, 0.6), font_mono)
		lt.custom_minimum_size = Vector2(70, 0)
		legend.add_child(lt)
	add_child(legend)

	_build_settings()
	_wire_focus()


func _build_settings() -> void:
	_settings = ColorRect.new()
	(_settings as ColorRect).color = Color(0.0, 0.0, 0.0, 0.72)
	_settings.set_anchors_preset(Control.PRESET_FULL_RECT)
	_settings.visible = false
	add_child(_settings)
	var box := PanelContainer.new()
	box.position = Vector2(660, 380)
	box.custom_minimum_size = Vector2(600, 0)
	var sb := StyleBoxFlat.new()
	sb.bg_color = Color(0.02, 0.03, 0.035, 0.97)
	sb.border_color = Color(GREEN, 0.4)
	sb.set_border_width_all(1)
	sb.content_margin_left = 36
	sb.content_margin_right = 36
	sb.content_margin_top = 30
	sb.content_margin_bottom = 30
	box.add_theme_stylebox_override("panel", sb)
	_settings.add_child(box)
	var v := VBoxContainer.new()
	v.add_theme_constant_override("separation", 16)
	box.add_child(v)
	v.add_child(_label("SETTINGS", 34, TEXT, font_title))
	var info := _label("Graphics, audio and controller options arrive in a later build.", 19, DIM)
	info.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	info.custom_minimum_size = Vector2(520, 0)
	v.add_child(info)
	_spacer(v, 8)
	_settings_back = _menu_button("BACK")
	_settings_back.pressed.connect(_close_settings)
	v.add_child(_settings_back)


func _spacer(parent: Control, h: int) -> void:
	var s := Control.new()
	s.custom_minimum_size = Vector2(0, h)
	parent.add_child(s)


func _ensure_gamepad_ui_actions() -> void:
	var pairs := [["ui_accept", JOY_BUTTON_A], ["ui_cancel", JOY_BUTTON_B]]
	for p: Array in pairs:
		var has := false
		for e in InputMap.action_get_events(p[0]):
			if e is InputEventJoypadButton and (e as InputEventJoypadButton).button_index == p[1]:
				has = true
		if not has:
			var ev := InputEventJoypadButton.new()
			ev.button_index = p[1]
			ev.device = -1
			InputMap.action_add_event(p[0], ev)


func _left_column() -> Array[Control]:
	var out: Array[Control] = [_name, _host_btn]
	if _solo_btn.visible:
		out.append(_solo_btn)
	out.append_array([_settings_btn, _quit_btn])
	return out


func _row_list() -> Array[Control]:
	var out: Array[Control] = []
	for c in _rows_box.get_children():
		if c is MenuRoomRow and not c.is_queued_for_deletion():
			out.append(c)
	return out


func _wire_focus() -> void:
	var left := _left_column()
	var rows := _row_list()
	for i in left.size():
		var c := left[i]
		c.focus_neighbor_top = c.get_path_to(left[maxi(i - 1, 0)])
		c.focus_neighbor_bottom = c.get_path_to(left[mini(i + 1, left.size() - 1)])
		c.focus_neighbor_left = c.get_path_to(c)
		c.focus_neighbor_right = c.get_path_to(rows[0] if not rows.is_empty() else c)
		c.focus_next = c.focus_neighbor_bottom
		c.focus_previous = c.focus_neighbor_top
	for i in rows.size():
		var r := rows[i]
		r.focus_neighbor_top = r.get_path_to(rows[maxi(i - 1, 0)])
		r.focus_neighbor_bottom = r.get_path_to(rows[mini(i + 1, rows.size() - 1)])
		r.focus_neighbor_left = r.get_path_to(_host_btn)
		r.focus_neighbor_right = r.get_path_to(r)
		r.focus_next = r.focus_neighbor_bottom
		r.focus_previous = r.focus_neighbor_top
	_settings_back.focus_neighbor_top = _settings_back.get_path_to(_settings_back)
	_settings_back.focus_neighbor_bottom = _settings_back.get_path_to(_settings_back)
	_settings_back.focus_neighbor_left = _settings_back.get_path_to(_settings_back)
	_settings_back.focus_neighbor_right = _settings_back.get_path_to(_settings_back)


func _unhandled_input(event: InputEvent) -> void:
	if event.is_action_pressed("ui_cancel"):
		get_viewport().set_input_as_handled()
		_back()
	elif get_viewport().gui_get_focus_owner() == null and (event.is_action_pressed("ui_down") or event.is_action_pressed("ui_up") or event.is_action_pressed("ui_accept")):
		get_viewport().set_input_as_handled()
		(_settings_back if _settings.visible else _host_btn).grab_focus()


func _back() -> void:
	if _settings.visible:
		_close_settings()
	elif Net.state == "joining" or Net.state == "connecting":
		Net.leave()
		_join_target = ""
		_set_status("Join cancelled.", DIM)
	elif get_viewport().gui_get_focus_owner() is MenuRoomRow:
		_host_btn.grab_focus()
	else:
		_quit_btn.grab_focus()


func _open_settings() -> void:
	_settings.visible = true
	_settings_back.grab_focus()


func _close_settings() -> void:
	_settings.visible = false
	_settings_btn.grab_focus()


func _on_lobby_pending() -> void:
	_link.text = "LOBBY LINK  ···  SEARCHING"
	_link.add_theme_color_override("font_color", DIM)


func _on_lobby(online: bool) -> void:
	_link.text = "LOBBY LINK  ●  ONLINE" if online else "LOBBY LINK  ○  OFFLINE"
	_link.add_theme_color_override("font_color", Color(GREEN, 0.85) if online else AMBER)
	var had_focus := _solo_btn.has_focus()
	_solo_btn.visible = not online
	if online:
		if _status.text.begins_with("Can't reach"):
			_set_status("", TEXT)
		if had_focus:
			_host_btn.grab_focus()
	else:
		_set_status("Can't reach the lobby server. Offline solo is available; hosting still works once it comes back.", AMBER)
		if _rows.is_empty():
			_empty.text = "Lobby server unreachable. Retrying in the background…"
	_wire_focus()


func _on_rooms(rooms: Array) -> void:
	var palette := {"green": GREEN, "text": TEXT, "dim": DIM, "amber": AMBER}
	var seen := {}
	var focused := get_viewport().gui_get_focus_owner()
	var focused_idx := _row_list().find(focused)
	for r: Dictionary in rooms:
		seen[r.id] = true
		var row: MenuRoomRow = _rows.get(r.id)
		if row == null:
			row = MenuRoomRow.new()
			row.setup(font_ui, font_mono, _row_styles)
			row.pressed.connect(_on_row_pressed.bind(row))
			_rows_box.add_child(row)
			_rows[r.id] = row
		row.update_room(r, palette)
	var order := 1
	for r: Dictionary in rooms:
		_rows_box.move_child(_rows[r.id], order)
		order += 1
	var removed_focus := false
	for id: String in _rows.keys():
		if not seen.has(id):
			var row: MenuRoomRow = _rows[id]
			if row == focused:
				removed_focus = true
			_rows_box.remove_child(row)
			row.queue_free()
			_rows.erase(id)
	_empty.visible = _rows.is_empty()
	if _empty.visible and Net.lobby_online:
		_empty.text = "No open missions yet.\nHost one: every mission is public, so other pilots will see it here."
	var open := 0
	for r: Dictionary in rooms:
		if r.joinable:
			open += 1
	_count.text = "[%d]" % rooms.size() if rooms.size() == open else "[%d/%d]" % [open, rooms.size()]
	_wire_focus()
	if removed_focus:
		var rows := _row_list()
		if rows.is_empty():
			_host_btn.grab_focus()
		else:
			rows[clampi(focused_idx, 0, rows.size() - 1)].grab_focus()


func _on_row_pressed(row: MenuRoomRow) -> void:
	if _entering or Net.state == "joining" or Net.state == "connecting":
		return
	var r := row.room
	if not r.version_ok:
		_set_status("Version mismatch: that mission runs v%s, you have v%s. Update the game to join." % [r.version, Net.version], AMBER)
		return
	if r.full:
		_set_status("That mission is full (%d/%d)." % [r.players, r.max], AMBER)
		return
	_join_target = str(r.host).to_upper()
	_set_status("Contacting %s…" % _join_target, TEXT)
	Net.join(r.id, _pilot_name())


func _on_host() -> void:
	if _entering or Net.state == "joining" or Net.state == "connecting":
		return
	_set_status("Opening a public mission…", TEXT)
	Net.host(_pilot_name())


func _on_solo() -> void:
	if _entering:
		return
	Net.play_offline(_pilot_name())


func _on_net_state(s: String, detail: String) -> void:
	match s:
		"joining":
			_set_status("Contacting %s… (B to cancel)" % (_join_target if _join_target != "" else "host"), TEXT)
		"connecting":
			_set_status("Host answered, opening a direct link…", TEXT)
		"connected":
			_set_status("Connected. Loading mission…", GREEN)
			_enter()
		"hosting":
			_set_status("Mission open. Loading…", GREEN)
			_enter()
		"solo":
			_set_status(detail if detail != "" else "Offline solo. Loading…", GREEN)
			_enter()
		"failed":
			_set_status(detail, RED)
			_join_target = ""


func _enter() -> void:
	if _entering:
		return
	_entering = true
	enter_game.emit()
	if not auto_enter:
		return
	if not ResourceLoader.exists(game_scene):
		_entering = false
		Net.leave()
		_set_status("Game scene %s isn't in this build yet." % game_scene, RED)
		return
	get_tree().change_scene_to_file.call_deferred(game_scene)


func reset_entering() -> void:
	_entering = false


func _set_status(text: String, color: Color) -> void:
	_status.text = text
	_status.add_theme_color_override("font_color", color)


func status_text() -> String:
	return _status.text


func _pilot_name() -> String:
	var n := _name.text.strip_edges()
	if n == "":
		n = "Pilot-%02d" % (randi() % 100)
		_name.text = n
		_save_name()
	return n.substr(0, 16)


func _load_name() -> String:
	var cfg := ConfigFile.new()
	if cfg.load(PROFILE) == OK:
		return str(cfg.get_value("pilot", "name", ""))
	return ""


func _save_name() -> void:
	var cfg := ConfigFile.new()
	cfg.load(PROFILE)
	cfg.set_value("pilot", "name", _name.text.strip_edges())
	cfg.save(PROFILE)


func _process(_delta: float) -> void:
	var d := Time.get_datetime_dict_from_system(true)
	_clock.text = "%02d:%02d:%02dZ" % [d.hour, d.minute, d.second]


class _Rule extends Control:
	func _draw() -> void:
		draw_line(Vector2(0, 5), Vector2(size.x, 5), Color(0.36, 1.0, 0.58, 0.22), 1.0)
		draw_line(Vector2(0, 5), Vector2(120, 5), Color(0.36, 1.0, 0.58, 0.9), 2.0)
		draw_rect(Rect2(size.x - 6, 3, 4, 4), Color(0.36, 1.0, 0.58, 0.6))


class _Brackets extends Control:
	func _draw() -> void:
		var c := Color(0.36, 1.0, 0.58, 0.7)
		var l := 22.0
		var w := size.x + 56.0
		var h := size.y + 44.0
		var o := Vector2(-28, -22)
		for corner in [Vector2(0, 0), Vector2(w, 0), Vector2(0, h), Vector2(w, h)]:
			var p: Vector2 = o + corner
			var sx := 1.0 if corner.x == 0.0 else -1.0
			var sy := 1.0 if corner.y == 0.0 else -1.0
			draw_line(p, p + Vector2(l * sx, 0), c, 2.0)
			draw_line(p, p + Vector2(0, l * sy), c, 2.0)

	func _notification(what: int) -> void:
		if what == NOTIFICATION_RESIZED:
			queue_redraw()
