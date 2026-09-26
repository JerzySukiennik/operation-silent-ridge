# Real F-35C visual: merges the imported per-part gear animations into one "gear" track set and exposes set_gear(0 = up, 1 = down).
extends Node3D

const GEAR_TIME := 175.0 / 24.0

var _anim: AnimationPlayer
var _gear := 0.0


func _ready() -> void:
	var players := find_children("*", "AnimationPlayer", true, false)
	if players.is_empty():
		return
	_anim = players[0]
	var merged := Animation.new()
	merged.length = 0.0
	var best := {}
	for lib_name in _anim.get_animation_library_list():
		var lib := _anim.get_animation_library(lib_name)
		for n in lib.get_animation_list():
			var a := lib.get_animation(n)
			merged.length = maxf(merged.length, a.length)
			for t in a.get_track_count():
				var key := "%s|%d" % [a.track_get_path(t), a.track_get_type(t)]
				if not best.has(key) or a.track_get_key_count(t) > best[key][0].track_get_key_count(best[key][1]):
					best[key] = [a, t]
	for key in best:
		best[key][0].copy_track(best[key][1], merged)
	var gl := AnimationLibrary.new()
	gl.add_animation("gear", merged)
	_anim.add_animation_library("merged", gl)
	_anim.play("merged/gear")
	_anim.pause()
	set_gear(_gear)


func set_gear(f: float) -> void:
	_gear = clampf(f, 0.0, 1.0)
	if _anim and _anim.has_animation("merged/gear"):
		_anim.seek(_gear * _anim.get_animation("merged/gear").length, true)


func get_gear() -> float:
	return _gear
