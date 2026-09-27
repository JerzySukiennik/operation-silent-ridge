# Game scene glue: world, local F-35C, remote jets via NetSync, HUD, jet audio, crash/respawn, pause overlay and the --autotest canyon run.
extends Node3D

const AircraftScene := preload("res://scenes/aircraft/f35c.tscn")
const HudScene := preload("res://scenes/ui/hud.tscn")
const WorldScene := preload("res://scenes/world/world.tscn")
const RESPAWN_DELAY := 4.0
const SOUND_SPEED := 340.0

var world: World
var jet: Aircraft
var jet_audio: JetAudio
var hud: FlightHud
var sync: NetSync
var remotes := {}
var remote_audio := {}
var remote_prev := {}
var respawn_timer := -1.0
var pause_layer: CanvasLayer
var paused := false
var args := {}
var autotest: RefCounted
var _test_clock := 0.0


func _ready() -> void:
	args = Engine.get_meta("boot_args", {}) if Engine.has_meta("boot_args") else {}
	if not Net.is_in_session():
		if args.has("host"):
			Net.host(str(args.get("name", "Pilot")))
		else:
			Net.play_offline(str(args.get("name", "Pilot")))
	world = WorldScene.instantiate()
	add_child(world)
	jet = AircraftScene.instantiate()
	jet.is_local = true
	jet.world = world
	jet.peer_id = Net.my_id()
	jet.pilot_name = Net.my_name if Net.my_name != "" else "Pilot"
	add_child(jet)
	jet.camera.near = 0.5
	jet.camera.far = 120000.0
	jet.camera.current = true
	jet.crashed.connect(_on_crashed)
	jet_audio = JetAudio.new()
	jet.add_child(jet_audio)
	jet_audio.setup(true)
	hud = HudScene.instantiate()
	add_child(hud)
	hud.set_aircraft(jet)
	hud.set_camera(jet.camera)
	sync = NetSync.new()
	sync.name = "NetSync"
	add_child(sync)
	sync.register_local(jet)
	sync.player_spawned.connect(_on_player_spawned)
	sync.player_left.connect(_on_player_left)
	Net.state_changed.connect(_on_net_state)
	_load_settings()
	_build_pause()
	jet.respawn(_spawn_point())
	if args.has("autotest"):
		autotest = preload("res://scripts/test/autotest.gd").new(self)


func _spawn_point() -> Transform3D:
	var pts := world.spawn_points()
	var ids: Array = Net.names.keys()
	ids.sort()
	var slot := maxi(ids.find(Net.my_id()), 0)
	return pts[slot % pts.size()]


func _on_crashed(_pos: Vector3) -> void:
	respawn_timer = RESPAWN_DELAY


func _on_player_spawned(id: int, pilot: String) -> void:
	if id == Net.my_id() or remotes.has(id):
		return
	var a: Aircraft = AircraftScene.instantiate()
	a.is_local = false
	a.world = world
	a.peer_id = id
	a.pilot_name = pilot
	add_child(a)
	var au := JetAudio.new()
	a.add_child(au)
	au.setup(false)
	remotes[id] = a
	remote_audio[id] = au
	remote_prev[id] = Vector3.ZERO
	sync.bind_remote(id, a)


func _on_player_left(id: int) -> void:
	if remotes.has(id):
		remotes[id].queue_free()
		remotes.erase(id)
		remote_audio.erase(id)
		remote_prev.erase(id)


func _on_net_state(s: String, _detail: String) -> void:
	if s == "failed":
		_leave()


func _process(delta: float) -> void:
	world.set_focus(jet.global_position)
	if respawn_timer >= 0.0:
		respawn_timer -= delta
		if respawn_timer < 0.0:
			jet.respawn(_spawn_point())
	jet_audio.update(jet.telemetry(), delta)
	var mates: Array = []
	for id in remotes:
		var a: Aircraft = remotes[id]
		var t := a.telemetry()
		var p: Vector3 = a.global_position
		var prev: Vector3 = remote_prev[id]
		if prev != Vector3.ZERO and delta > 0.0:
			t["mach"] = p.distance_to(prev) / delta / SOUND_SPEED
		remote_prev[id] = p
		remote_audio[id].update(t, delta)
		if a.alive:
			mates.append({"name": a.pilot_name, "position": p})
	hud.set_teammates(mates)
	if autotest:
		autotest.call("step", delta)
	elif args.has("test_seconds"):
		_test_clock += delta
		if _test_clock > float(args.test_seconds):
			for id in remotes:
				var r: Aircraft = remotes[id]
				print("REPORT client remote id=%d name=%s pos=%s alive=%s" % [id, r.pilot_name, r.global_position.round(), r.alive])
			print("REPORT client done remotes=%d state=%s rtt=%s" % [remotes.size(), Net.state, Net.rtt])
			get_tree().quit()


func _unhandled_input(event: InputEvent) -> void:
	var start: bool = event is InputEventJoypadButton and event.pressed and event.button_index == JOY_BUTTON_START
	var esc: bool = event is InputEventKey and event.pressed and not event.echo and event.keycode == KEY_ESCAPE
	if start or esc:
		_set_paused(not paused)
		get_viewport().set_input_as_handled()


func _build_pause() -> void:
	pause_layer = CanvasLayer.new()
	pause_layer.layer = 50
	pause_layer.visible = false
	add_child(pause_layer)
	var dim := ColorRect.new()
	dim.color = Color(0.0, 0.02, 0.01, 0.72)
	dim.set_anchors_preset(Control.PRESET_FULL_RECT)
	pause_layer.add_child(dim)
	var box := VBoxContainer.new()
	box.set_anchors_preset(Control.PRESET_CENTER)
	box.custom_minimum_size = Vector2(420, 0)
	box.position = Vector2(-210, -120)
	box.add_theme_constant_override("separation", 14)
	dim.add_child(box)
	var title := Label.new()
	title.text = "PAUSED — the mission keeps running"
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	title.add_theme_color_override("font_color", Color(0.45, 1.0, 0.55))
	title.add_theme_font_size_override("font_size", 26)
	box.add_child(title)
	var room := Label.new()
	room.name = "Room"
	room.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	room.add_theme_color_override("font_color", Color(0.45, 1.0, 0.55, 0.7))
	box.add_child(room)
	for spec in [["Resume", func(): _set_paused(false)], ["Respawn", func(): _set_paused(false); jet.respawn(_spawn_point())], ["Leave to menu", _leave]]:
		var b := Button.new()
		b.text = spec[0]
		b.custom_minimum_size = Vector2(420, 54)
		b.add_theme_font_size_override("font_size", 22)
		b.pressed.connect(spec[1])
		box.add_child(b)
	var inv := Button.new()
	inv.name = "Invert"
	inv.custom_minimum_size = Vector2(420, 54)
	inv.add_theme_font_size_override("font_size", 22)
	inv.pressed.connect(func():
		Controls.invert_pitch = not Controls.invert_pitch
		_save_settings()
		_update_invert_label())
	box.add_child(inv)
	box.move_child(inv, box.get_child_count() - 2)
	_update_invert_label()


func _update_invert_label() -> void:
	var inv: Button = pause_layer.find_child("Invert", true, false)
	inv.text = "Stick up = nose %s" % ("UP (inverted)" if Controls.invert_pitch else "DOWN (flight sim)")


func _load_settings() -> void:
	var cfg := ConfigFile.new()
	if cfg.load("user://settings.cfg") == OK:
		Controls.invert_pitch = bool(cfg.get_value("controls", "invert_pitch", false))


func _save_settings() -> void:
	var cfg := ConfigFile.new()
	cfg.load("user://settings.cfg")
	cfg.set_value("controls", "invert_pitch", Controls.invert_pitch)
	cfg.save("user://settings.cfg")


func _set_paused(on: bool) -> void:
	paused = on
	pause_layer.visible = on
	Controls.enabled = not on
	if on:
		var room: Label = pause_layer.find_child("Room", true, false)
		room.text = "%d pilot(s) in this mission" % maxi(Net.names.size(), 1)
		var first: Button = pause_layer.find_children("*", "Button", true, false)[0]
		first.grab_focus()


func _leave() -> void:
	Controls.enabled = true
	Net.leave()
	get_tree().change_scene_to_file("res://scenes/ui/menu.tscn")
