# In-game replication: sends the local aircraft state at 30 Hz (38-byte packets), spawns/removes remote players, maps timestamps to the local clock, carries reliable game events.
class_name NetSync
extends Node

signal player_spawned(id: int, pilot_name: String)
signal player_left(id: int)
signal remote_state(id: int, state: Dictionary, sent_time: float)
signal event_received(event_name: String, data: Variant, from_id: int)

const RATE := 30.0
const PACKET_SIZE := 38
# smallest-three quaternion components lie in [-1/sqrt(2), 1/sqrt(2)]
const Q_RANGE := 0.70710678
const V_SCALE := 0.05
const W_SCALE := 0.0005

var local_aircraft: Node
var players := {}
var sent_packets := 0
var _bound := {}
var _last_t := {}
var _acc := 0.0


func _ready() -> void:
	process_physics_priority = 100
	Net.peer_joined.connect(_on_peer_joined)
	Net.peer_left.connect(_on_peer_left)
	Net.state_packet.connect(_on_packet)
	Net.event_packet.connect(func(n: String, d: Variant, from_id: int) -> void: event_received.emit(n, d, from_id))
	_sync_existing.call_deferred()


func _exit_tree() -> void:
	Net.attach_sync(false)


func register_local(aircraft: Node) -> void:
	local_aircraft = aircraft


func bind_remote(id: int, node: Node) -> void:
	if node == null:
		_bound.erase(id)
	else:
		_bound[id] = node
		if Net.latest_state.has(id):
			_on_packet(id, Net.latest_state[id], true)


func send_event(event_name: String, data: Variant = null, to := Net.ALL) -> void:
	Net.send_event(event_name, data, to)


func now() -> float:
	return Net.local_time()


func player_name(id: int) -> String:
	return str(Net.names.get(id, "Pilot"))


func _sync_existing() -> void:
	var me := Net.my_id()
	for id: int in Net.names:
		if id != me and not players.has(id):
			_spawn(id)
	for n: Array in Net.attach_sync(true):
		event_received.emit(n[0], n[1], n[2])


func _spawn(id: int) -> void:
	players[id] = player_name(id)
	player_spawned.emit(id, players[id])
	if Net.latest_state.has(id):
		_on_packet(id, Net.latest_state[id], true)


func _on_peer_joined(id: int) -> void:
	if id != Net.my_id() and not players.has(id):
		_spawn(id)


func _on_peer_left(id: int) -> void:
	_last_t.erase(id)
	_bound.erase(id)
	if players.has(id):
		players.erase(id)
		player_left.emit(id)


func _physics_process(delta: float) -> void:
	if not Net.is_online() or not (Net.state == "hosting" or Net.state == "connected"):
		_acc = 0.0
		return
	if not is_instance_valid(local_aircraft) or (Net.is_client() and Net.clock_samples == 0):
		return
	_acc = minf(_acc + delta, 2.0 / RATE)
	if _acc < 1.0 / RATE:
		return
	_acc -= 1.0 / RATE
	var st: Dictionary = local_aircraft.call("get_net_state")
	Net.send_state(pack_state(Net.my_id(), Net.net_time(), st))
	sent_packets += 1


func _on_packet(owner: int, pkt: PackedByteArray, replay := false) -> void:
	if owner == Net.my_id() or not players.has(owner) or (Net.is_client() and Net.clock_samples == 0):
		return
	var u := unpack_state(pkt, Net.net_time())
	if u.is_empty():
		return
	var t: float = u.t
	if not replay and _last_t.has(owner) and t <= float(_last_t[owner]):
		return
	_last_t[owner] = t
	var sent_time := Net.to_local_time(t)
	remote_state.emit(owner, u.state, sent_time)
	var node: Node = _bound.get(owner)
	if is_instance_valid(node) and node.has_method("apply_remote_state"):
		node.call("apply_remote_state", u.state, sent_time)


# Layout (little-endian, 38 B): u8 owner | u32 net time ms | 3×f32 position | u8 quat index + 3×s16 smallest-three
# | 3×s16 velocity (0.05 m/s) | 3×s16 angular velocity (0.0005 rad/s) | u8 throttle | u8 flags (1 ab, 2 gear, 4 alive).
static func pack_state(owner: int, t_net: float, s: Dictionary) -> PackedByteArray:
	var b := PackedByteArray()
	b.resize(PACKET_SIZE)
	b.encode_u8(0, owner)
	b.encode_u32(1, int(floor(t_net * 1000.0)) & 0xFFFFFFFF)
	var p: Vector3 = s.get("p", Vector3.ZERO)
	b.encode_float(5, p.x)
	b.encode_float(9, p.y)
	b.encode_float(13, p.z)
	var q: Quaternion = (s.get("q", Quaternion.IDENTITY) as Quaternion).normalized()
	var c := [q.x, q.y, q.z, q.w]
	var big := 0
	for i in range(1, 4):
		if absf(c[i]) > absf(c[big]):
			big = i
	var sg := -1.0 if c[big] < 0.0 else 1.0
	b.encode_u8(17, big)
	var o := 18
	for i in 4:
		if i != big:
			b.encode_s16(o, clampi(roundi(c[i] * sg / Q_RANGE * 32767.0), -32767, 32767))
			o += 2
	var v: Vector3 = s.get("v", Vector3.ZERO)
	var w: Vector3 = s.get("w", Vector3.ZERO)
	for i in 3:
		b.encode_s16(24 + i * 2, clampi(roundi(v[i] / V_SCALE), -32767, 32767))
		b.encode_s16(30 + i * 2, clampi(roundi(w[i] / W_SCALE), -32767, 32767))
	b.encode_u8(36, clampi(roundi(float(s.get("thr", 0.0)) * 255.0), 0, 255))
	var flags := (1 if s.get("ab", false) else 0) | (2 if s.get("gear", false) else 0) | (4 if s.get("alive", true) else 0)
	b.encode_u8(37, flags)
	return b


static func unpack_state(b: PackedByteArray, t_ref: float) -> Dictionary:
	if b.size() < PACKET_SIZE:
		return {}
	var ref_ms := int(floor(t_ref * 1000.0))
	var diff := (b.decode_u32(1) - (ref_ms & 0xFFFFFFFF)) & 0xFFFFFFFF
	if diff >= 0x80000000:
		diff -= 0x100000000
	var t := (ref_ms + diff) / 1000.0
	var big := b.decode_u8(17)
	var c := [0.0, 0.0, 0.0, 0.0]
	var o := 18
	var sq := 0.0
	for i in 4:
		if i != big:
			c[i] = b.decode_s16(o) / 32767.0 * Q_RANGE
			sq += c[i] * c[i]
			o += 2
	c[mini(big, 3)] = sqrt(maxf(0.0, 1.0 - sq))
	var v := Vector3(b.decode_s16(24), b.decode_s16(26), b.decode_s16(28)) * V_SCALE
	var w := Vector3(b.decode_s16(30), b.decode_s16(32), b.decode_s16(34)) * W_SCALE
	var flags := b.decode_u8(37)
	return {"owner": b.decode_u8(0), "t": t, "state": {
		"p": Vector3(b.decode_float(5), b.decode_float(9), b.decode_float(13)),
		"q": Quaternion(c[0], c[1], c[2], c[3]).normalized(),
		"v": v, "w": w, "thr": b.decode_u8(36) / 255.0,
		"ab": flags & 1 != 0, "gear": flags & 2 != 0, "alive": flags & 4 != 0}}
