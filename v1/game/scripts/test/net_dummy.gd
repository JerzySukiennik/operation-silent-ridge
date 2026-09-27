# Net test stand-in for Aircraft: flies a deterministic circle as a function of network time so receivers can check timing and clock sync.
class_name NetDummy
extends Node3D

const RADIUS := 800.0
# 0.25 rad/s on an 800 m circle = 200 m/s ground speed
const OMEGA := 0.25

var is_local := true
var peer_id := 1
var pilot_name := "Pilot"
var last_remote := {}
var last_sent_time := 0.0


static func circle_pos(id: int, t: float) -> Vector3:
	return Vector3(id * 3000.0 + RADIUS * cos(OMEGA * t), 1000.0 + id * 10.0, RADIUS * sin(OMEGA * t))


static func circle_vel(t: float) -> Vector3:
	return Vector3(-RADIUS * OMEGA * sin(OMEGA * t), 0.0, RADIUS * OMEGA * cos(OMEGA * t))


func _physics_process(_delta: float) -> void:
	if is_local:
		position = circle_pos(peer_id, Net.net_time())


func get_net_state() -> Dictionary:
	var t := Net.net_time()
	var v := circle_vel(t)
	var basis := Basis.looking_at(v.normalized(), Vector3.UP).rotated(v.normalized(), 0.6)
	return {"p": circle_pos(peer_id, t), "q": basis.get_rotation_quaternion(), "v": v, "w": Vector3(0.0, OMEGA, 0.0),
		"thr": 0.8, "ab": peer_id % 2 == 0, "gear": false, "alive": true}


func apply_remote_state(state: Dictionary, sent_time: float) -> void:
	last_remote = state
	last_sent_time = sent_time
	position = state.p
