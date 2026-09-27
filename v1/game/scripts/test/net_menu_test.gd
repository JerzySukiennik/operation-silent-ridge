# Menu test: --mode=shot renders the menu with fake rooms and walks it with gamepad events (screenshots on HP); --mode=join joins a real host purely via gamepad input.
extends Node

var args := {}
var menu: MainMenu
var shots := ""
var entered_at := -1.0
var t := 0.0


func _ready() -> void:
	args = Engine.get_meta("boot_args", {})
	shots = str(args.get("shots", ""))
	menu = (load("res://scenes/ui/menu.tscn") as PackedScene).instantiate()
	menu.auto_enter = false
	menu.browse = str(args.get("mode", "shot")) == "join"
	menu.enter_game.connect(func() -> void:
		entered_at = t
		print("REPORT menu enter_game t=%.2f state=%s room=%s" % [t, Net.state, Net.room_id]))
	add_child(menu)
	if str(args.get("mode", "shot")) == "join":
		_run_join.call_deferred()
	else:
		_run_shot.call_deferred()


func _process(delta: float) -> void:
	t += delta


func _wait(s: float) -> void:
	await get_tree().create_timer(s).timeout


func _pad(button: JoyButton) -> void:
	for pressed in [true, false]:
		var ev := InputEventJoypadButton.new()
		ev.button_index = button
		ev.pressed = pressed
		ev.device = 0
		Input.parse_input_event(ev)
		await get_tree().process_frame
		await get_tree().process_frame


func _focus_name() -> String:
	var f := get_viewport().gui_get_focus_owner()
	if f == null:
		return "none"
	if f is MenuRoomRow:
		return "row:" + str((f as MenuRoomRow).room.get("host", "?"))
	if f is Button:
		return "btn:" + (f as Button).text
	return f.get_class()


func _shot(n: String) -> void:
	if shots == "" or DisplayServer.get_name() == "headless":
		return
	await RenderingServer.frame_post_draw
	get_viewport().get_texture().get_image().save_png(shots.path_join(n + ".png"))


func _fake_rooms() -> Array:
	return [
		{"id": "a1", "host": "Viper-2", "players": 2, "max": 4, "mission": "FREE FLIGHT", "version": Net.version, "age_s": 14, "uptime_s": 740, "full": false, "version_ok": true, "joinable": true},
		{"id": "b2", "host": "Nordwind", "players": 1, "max": 4, "mission": "FREE FLIGHT", "version": Net.version, "age_s": 62, "uptime_s": 95, "full": false, "version_ok": true, "joinable": true},
		{"id": "c3", "host": "Kestrel", "players": 4, "max": 4, "mission": "FREE FLIGHT", "version": Net.version, "age_s": 30, "uptime_s": 2410, "full": true, "version_ok": true, "joinable": false},
		{"id": "d4", "host": "OldBuild", "players": 1, "max": 4, "mission": "FREE FLIGHT", "version": "0.0.9", "age_s": 120, "uptime_s": 60, "full": false, "version_ok": false, "joinable": false},
	]


func _run_shot() -> void:
	var steps: Array[String] = []
	menu._on_lobby(true)
	menu._on_rooms(_fake_rooms())
	await _wait(1.5)
	steps.append("start=" + _focus_name())
	await _shot("menu_01_rooms")
	await _pad(JOY_BUTTON_DPAD_RIGHT)
	steps.append("right=" + _focus_name())
	await _pad(JOY_BUTTON_DPAD_DOWN)
	steps.append("down=" + _focus_name())
	await _shot("menu_02_row_focus")
	await _pad(JOY_BUTTON_DPAD_DOWN)
	await _pad(JOY_BUTTON_A)
	steps.append("A_on_full=\"" + menu.status_text() + "\"")
	await _shot("menu_03_full_msg")
	await _pad(JOY_BUTTON_B)
	steps.append("B=" + _focus_name())
	await _pad(JOY_BUTTON_DPAD_DOWN)
	await _pad(JOY_BUTTON_A)
	steps.append("settings_open=%s focus=%s" % [menu._settings.visible, _focus_name()])
	await _shot("menu_04_settings")
	await _pad(JOY_BUTTON_B)
	steps.append("settings_closed=%s focus=%s" % [not menu._settings.visible, _focus_name()])
	menu._on_rooms([])
	menu._on_lobby(false)
	await _wait(0.5)
	steps.append("offline_solo_visible=%s" % menu._solo_btn.visible)
	await _shot("menu_05_offline")
	for s in steps:
		print("REPORT menu ", s)
	var ok := steps[0] == "start=btn:HOST MISSION" and steps[1] == "right=row:Viper-2" and steps[2] == "down=row:Nordwind" \
		and steps[3].contains("full") and steps[4] == "B=btn:HOST MISSION" and steps[5].begins_with("settings_open=true") \
		and steps[6] == "settings_closed=true focus=btn:SETTINGS" and steps[7] == "offline_solo_visible=true"
	print("REPORT menu RESULT %s" % ("PASS" if ok else "FAIL"))
	await _wait(0.5)
	get_tree().quit()


func _run_join() -> void:
	menu._name.text = str(args.get("name", "PadPilot"))
	var want := str(args.get("host_name", ""))
	var deadline := t + 30.0
	while t < deadline:
		var rows := menu._row_list()
		if not rows.is_empty() and (want == "" or (rows[0] as MenuRoomRow).room.get("host", "") == want):
			break
		await get_tree().process_frame
	print("REPORT menu rows=%d focus=%s t=%.2f" % [menu._row_list().size(), _focus_name(), t])
	await _pad(JOY_BUTTON_DPAD_RIGHT)
	print("REPORT menu after_right focus=%s" % _focus_name())
	await _pad(JOY_BUTTON_A)
	deadline = t + 30.0
	while t < deadline and entered_at < 0.0 and Net.state != "failed":
		await get_tree().process_frame
	print("REPORT menu join state=%s status=\"%s\" names=%s" % [Net.state, menu.status_text(), Net.names])
	print("REPORT menu RESULT %s" % ("PASS" if entered_at >= 0.0 and Net.state == "connected" else "FAIL"))
	Net.leave()
	await _wait(1.0)
	get_tree().quit()
