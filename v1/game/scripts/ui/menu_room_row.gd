# One focusable row of the public room list: freshness dot, host, mission, pilots n/max, version, uptime and a join/full/update tag.
class_name MenuRoomRow
extends Button

const COLS := [["", 26], ["HOST", 290], ["MISSION", 240], ["PILOTS", 120], ["VERSION", 140], ["UP", 100], ["", 110]]

var room := {}
var _labels: Array[Label] = []
var _dot: ColorRect


static func header(font: Font, color: Color) -> HBoxContainer:
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 0)
	for c: Array in COLS:
		var l := Label.new()
		l.text = c[0]
		l.custom_minimum_size = Vector2(c[1], 0)
		l.add_theme_font_override("font", font)
		l.add_theme_font_size_override("font_size", 15)
		l.add_theme_color_override("font_color", color)
		row.add_child(l)
	return row


func setup(font: Font, mono: Font, styles: Dictionary) -> void:
	custom_minimum_size = Vector2(0, 58)
	focus_mode = Control.FOCUS_ALL
	for k: String in styles:
		add_theme_stylebox_override(k, styles[k])
	var row := HBoxContainer.new()
	row.set_anchors_preset(Control.PRESET_FULL_RECT)
	row.offset_left = 14
	row.offset_right = -10
	row.add_theme_constant_override("separation", 0)
	row.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(row)
	for i in COLS.size():
		var cell := Control.new()
		cell.custom_minimum_size = Vector2(COLS[i][1], 0)
		cell.mouse_filter = Control.MOUSE_FILTER_IGNORE
		row.add_child(cell)
		if i == 0:
			_dot = ColorRect.new()
			_dot.size = Vector2(8, 8)
			_dot.position = Vector2(2, 25)
			_dot.mouse_filter = Control.MOUSE_FILTER_IGNORE
			cell.add_child(_dot)
			continue
		var l := Label.new()
		l.set_anchors_preset(Control.PRESET_FULL_RECT)
		l.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
		l.clip_text = true
		l.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
		l.mouse_filter = Control.MOUSE_FILTER_IGNORE
		l.add_theme_font_override("font", mono if i >= 3 and i <= 5 else font)
		l.add_theme_font_size_override("font_size", 22 if i == 1 else 19)
		cell.add_child(l)
		_labels.append(l)


func update_room(r: Dictionary, palette: Dictionary) -> void:
	room = r
	var host_txt := str(r.host).to_upper()
	_labels[0].text = host_txt
	_labels[1].text = str(r.mission).to_upper()
	_labels[2].text = "%d / %d" % [r.players, r.max]
	_labels[3].text = "v" + str(r.version)
	_labels[4].text = _fmt_uptime(int(r.uptime_s))
	var tag := "JOIN  ›"
	var tag_col: Color = palette.green
	if not r.version_ok:
		tag = "UPDATE"
		tag_col = palette.amber
	elif r.full:
		tag = "FULL"
		tag_col = palette.dim
	_labels[5].text = tag
	_labels[5].horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	var base: Color = palette.text if r.joinable else palette.dim
	for i in 5:
		_labels[i].add_theme_color_override("font_color", base)
	_labels[3].add_theme_color_override("font_color", palette.amber if not r.version_ok else base)
	_labels[5].add_theme_color_override("font_color", tag_col)
	# heartbeat every 90 s: older than ~100 s means one beat was missed
	_dot.color = palette.green if int(r.age_s) < 100 else palette.amber
	tooltip_text = "Last heartbeat %d s ago" % int(r.age_s)


static func _fmt_uptime(s: int) -> String:
	if s < 60:
		return "%ds" % s
	if s < 3600:
		return "%dm" % (s / 60)
	return "%dh%02d" % [s / 3600, (s % 3600) / 60]
