# Boot router: parses user args (after "--") and loads the menu, the game, or any scene passed with --scene=res://... for per-module tests.
extends Node

var args := {}


func _ready() -> void:
	args = parse_args(OS.get_cmdline_user_args())
	Engine.set_meta("boot_args", args)
	var target := "res://scenes/ui/menu.tscn"
	if args.has("scene"):
		target = str(args.scene)
	elif args.has("game") or args.has("autotest"):
		target = "res://scenes/game.tscn"
	if not ResourceLoader.exists(target):
		push_error("Boot target missing: %s" % target)
		get_tree().quit(2)
		return
	get_tree().change_scene_to_file.call_deferred(target)


static func parse_args(raw: PackedStringArray) -> Dictionary:
	var out := {}
	for a in raw:
		var s := a.trim_prefix("--")
		var eq := s.find("=")
		if eq >= 0:
			out[s.substr(0, eq)] = s.substr(eq + 1)
		else:
			out[s] = true
	return out


static func boot_args() -> Dictionary:
	return Engine.get_meta("boot_args", {}) if Engine.has_meta("boot_args") else {}
