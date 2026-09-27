# Integration test client: browses the (test) lobby, joins the first room, loads the game scene, and after 25 s reports remote players and quits.
extends Node

var joined := false
var t := 0.0


func _ready() -> void:
	Net.rooms_updated.connect(func(rooms: Array) -> void:
		if joined or rooms.is_empty():
			return
		var best: Dictionary = {}
		for r in rooms:
			print("REPORT join_test room %s host=%s age=%s uptime=%s joinable=%s" % [r.id, r.host, r.get("age_s"), r.get("uptime_s"), r.get("joinable")])
			if r.get("joinable", true) and float(r.get("uptime_s", 999)) < 90.0 and (best.is_empty() or float(r.get("uptime_s", 0)) < float(best.get("uptime_s", 0))):
				best = r
		if t < 8.0 or best.is_empty():
			return
		joined = true
		print("REPORT join_test joining %s host=%s" % [best.id, best.host])
		Net.join(best.id, "MacTester"))
	Net.state_changed.connect(func(s: String, d: String) -> void:
		print("REPORT join_test state=%s %s" % [s, d])
		if s == "connected":
			get_tree().change_scene_to_file.call_deferred("res://scenes/game.tscn"))
	Net.start_browsing()


func _process(delta: float) -> void:
	t += delta
	if t > 200.0:
		print("REPORT join_test TIMEOUT state=%s" % Net.state)
		get_tree().quit()
