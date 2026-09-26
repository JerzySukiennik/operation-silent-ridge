# Game replication over Net: world snapshot on join, straw transforms at 20 Hz, player avatars, host-side action requests and events.
class_name NetSync
extends Node

const RATE := 0.05
const CHUNK := 280

var main: Node
var avatars := {}
var remote_state := {}
var remote_bodies := {}
var tanks := {}
var _clock := 0.0
var _sent_xf := {}
var _economy_dirty := false
var _economy_clock := 0.0
var _suppress := false


func stack() -> Haystack:
	return main.haystack


func _ready() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS
	Net.peer_joined.connect(_on_peer_joined)
	Net.peer_left.connect(_on_peer_left)


func _on_peer_joined(id: int) -> void:
	if Net.is_host():
		_send_welcome.call_deferred(id)
	_ensure_avatar(id)


func _on_peer_left(id: int) -> void:
	if avatars.has(id):
		(avatars[id] as Node).queue_free()
		avatars.erase(id)
	if remote_bodies.has(id):
		(remote_bodies[id] as Node).queue_free()
		remote_bodies.erase(id)
	remote_state.erase(id)
	if Net.is_host():
		for piece in stack().pieces:
			if int(piece.get_meta("holder", 0)) == id:
				_release_remote(piece, Vector3.ZERO)
	main.hud.toast("%s left the farm" % Net.names.get(id, "A farmer"), "info")


func _ensure_avatar(id: int) -> RemoteFarmer:
	if id == Net.my_id():
		return null
	if not avatars.has(id):
		var a := RemoteFarmer.new()
		a.name = "Farmer_%d" % id
		a.peer_id = id
		main.add_child(a)
		a.set_display_name(Net.names.get(id, "Farmer"))
		avatars[id] = a
		if Net.is_host():
			var body := AnimatableBody3D.new()
			body.collision_layer = 4
			body.collision_mask = 0
			body.sync_to_physics = false
			var shape := CapsuleShape3D.new()
			shape.radius = 0.34
			shape.height = 1.8
			var cs := CollisionShape3D.new()
			cs.shape = shape
			cs.position.y = 0.9
			body.add_child(cs)
			main.add_child(body)
			remote_bodies[id] = body
	return avatars[id]


func _process(delta: float) -> void:
	if not Net.is_online():
		return
	_clock += delta
	if _clock >= RATE:
		_clock = 0.0
		if Net.is_host():
			_broadcast_pieces()
			_broadcast_players()
		elif Net.state == "connected":
			_send_my_state()
	if Net.is_host() and _economy_dirty:
		_economy_clock -= delta
		if _economy_clock <= 0.0:
			_economy_dirty = false
			_economy_clock = 0.15
			rpc_economy.rpc(_economy())


func _physics_process(_delta: float) -> void:
	if not Net.is_online() or not Net.is_host():
		return
	for id in remote_state:
		var st: Dictionary = remote_state[id]
		if remote_bodies.has(id):
			(remote_bodies[id] as AnimatableBody3D).global_position = st.pos
		var hid: int = st.get("held", 0)
		if hid != 0 and stack().by_id.has(hid):
			var piece: HayPiece = stack().by_id[hid]
			if int(piece.get_meta("holder", 0)) == id:
				_drive_remote_held(piece, st)


func _cam_basis(st: Dictionary) -> Basis:
	return Basis(Vector3.UP, st.yaw) * Basis(Vector3.RIGHT, st.pitch)


func _drive_remote_held(piece: HayPiece, st: Dictionary) -> void:
	var cam_pos: Vector3 = st.pos + Vector3.UP * Player.EYE
	var basis := _cam_basis(st)
	var forward := -basis.z
	var reach: float = st.hold - float(st.get("charge", 0.0)) * 0.35
	var target := cam_pos + forward * maxf(reach, 0.3)
	var offset := target - piece.global_position
	var v := offset * 22.0
	if v.length() > 24.0:
		v = v.normalized() * 24.0
	piece.linear_velocity = piece.linear_velocity.lerp(v, 0.6)
	var want := basis * Basis.from_euler(Player.HOLD_POSE)
	var diff := (want * piece.global_basis.inverse()).get_rotation_quaternion()
	var axis := diff.get_axis()
	var angle := diff.get_angle()
	if angle > PI:
		angle -= TAU
	if axis.is_finite():
		piece.angular_velocity = axis * angle * 6.0


func economy_changed() -> void:
	if Net.is_online() and Net.is_host() and not _suppress:
		_economy_dirty = true


func _economy() -> Dictionary:
	return {"money": main.money, "levels": main.levels, "machines": main.machines, "meters": main.conveyor_meters, "remaining": stack().remaining, "earned": main.total_earned, "belt": main.belt_built}


@rpc("authority", "reliable")
func rpc_economy(e: Dictionary) -> void:
	_suppress = true
	main.money = int(e.money)
	main.levels = e.levels
	main.machines = e.machines
	main.conveyor_meters = int(e.meters)
	main.total_earned = int(e.get("earned", 0))
	main.belt_built = int(e.get("belt", 0))
	stack().remaining = int(e.remaining)
	main.apply_stats()
	main.inventory_changed.emit()
	_suppress = false


func _piece_row(p: HayPiece) -> Array:
	var q := p.global_basis.get_rotation_quaternion()
	var o := p.global_position
	return [p.net_id, o.x, o.y, o.z, q.x, q.y, q.z, q.w]


func piece_spawned(p: HayPiece) -> void:
	rpc_piece_spawn.rpc(_piece_spawn_data(p))


func _piece_spawn_data(p: HayPiece) -> Array:
	var r := _piece_row(p)
	return r + [p.net_color.r, p.net_color.g, p.net_color.b, p.length, p.is_needle, int(p.get_meta("holder", 0))]


func piece_needled(p: HayPiece) -> void:
	if Net.is_online() and Net.is_host():
		rpc_piece_needle.rpc(p.net_id)


@rpc("authority", "reliable")
func rpc_piece_needle(id: int) -> void:
	if stack().by_id.has(id):
		(stack().by_id[id] as HayPiece).make_needle(stack().needle_material())


@rpc("authority", "reliable")
func rpc_piece_spawn(d: Array) -> void:
	_apply_piece_spawn(d)


func _apply_piece_spawn(d: Array) -> HayPiece:
	var id: int = d[0]
	if stack().by_id.has(id):
		return stack().by_id[id]
	var xf := Transform3D(Basis(Quaternion(d[4], d[5], d[6], d[7])), Vector3(d[1], d[2], d[3]))
	var piece := stack().spawn_piece(xf.origin, xf.basis, Color(d[8], d[9], d[10]), d[11], id)
	piece.set_net_target(xf)
	if d[12]:
		piece.make_needle(stack().needle_material())
	var holder: int = d[13]
	piece.set_meta("holder", holder)
	if holder == Net.my_id():
		main.player.adopt_held(piece)
	return piece


func piece_removed(id: int) -> void:
	_sent_xf.erase(id)
	rpc_piece_despawn.rpc(id)


@rpc("authority", "reliable")
func rpc_piece_despawn(id: int) -> void:
	if stack().by_id.has(id):
		var p: HayPiece = stack().by_id[id]
		if main.player.held == p:
			main.player.held = null
		p.despawn()


func _broadcast_pieces() -> void:
	var rows := PackedFloat32Array()
	var count := 0
	for p in stack().pieces:
		if p.selling:
			continue
		var xf := p.global_transform
		var last: Variant = _sent_xf.get(p.net_id)
		if last != null:
			var l: Transform3D = last
			if l.origin.distance_squared_to(xf.origin) < 0.00004 and l.basis.get_rotation_quaternion().angle_to(xf.basis.get_rotation_quaternion()) < 0.01:
				continue
		_sent_xf[p.net_id] = xf
		var r := _piece_row(p)
		for v in r:
			rows.append(float(v))
		count += 1
		if count >= CHUNK:
			rpc_pieces.rpc(rows)
			rows = PackedFloat32Array()
			count = 0
	if count > 0:
		rpc_pieces.rpc(rows)


@rpc("authority", "unreliable_ordered")
func rpc_pieces(rows: PackedFloat32Array) -> void:
	var n := rows.size() / 8
	for i in n:
		var o := i * 8
		var id := int(rows[o])
		if not stack().by_id.has(id):
			continue
		var p: HayPiece = stack().by_id[id]
		if p.local_driven:
			continue
		p.set_net_target(Transform3D(Basis(Quaternion(rows[o + 4], rows[o + 5], rows[o + 6], rows[o + 7])), Vector3(rows[o + 1], rows[o + 2], rows[o + 3])))


func shell_taken(idx: int) -> void:
	rpc_shell.rpc(idx, -1.0)


func shell_restored(idx: int, depth: float) -> void:
	rpc_shell.rpc(idx, depth)


@rpc("authority", "reliable")
func rpc_shell(idx: int, depth: float) -> void:
	if depth < 0.0:
		stack().apply_shell_take(idx)
	else:
		stack().apply_shell_restore(idx, depth)


func needle_changed() -> void:
	rpc_needle.rpc(stack().needle_pos, stack().needle_xf, stack().needle_taken)


@rpc("authority", "reliable")
func rpc_needle(pos: Vector3, xf: Transform3D, taken: bool) -> void:
	stack().set_needle_state(pos, xf, taken)


func _send_my_state() -> void:
	var p: Player = main.player
	var held_id := p.held.net_id if is_instance_valid(p.held) else 0
	req_state.rpc_id(1, p.global_position, p.rotation.y, p.head.rotation.x, p.hold_distance, p.charge, held_id, main.belt.current)


@rpc("any_peer", "unreliable_ordered")
func req_state(pos: Vector3, yaw: float, pitch: float, hold: float, charge: float, held: int, tool: String) -> void:
	var id := multiplayer.get_remote_sender_id()
	remote_state[id] = {"pos": pos, "yaw": yaw, "pitch": pitch, "hold": hold, "charge": charge, "held": held, "tool": tool}
	var a := _ensure_avatar(id)
	if a:
		a.set_state(pos, yaw, pitch, tool, held)


func _broadcast_players() -> void:
	var p: Player = main.player
	var states := [[1, p.global_position, p.rotation.y, p.head.rotation.x, main.belt.current, p.held.net_id if is_instance_valid(p.held) else 0]]
	for id in remote_state:
		var st: Dictionary = remote_state[id]
		states.append([id, st.pos, st.yaw, st.pitch, st.tool, st.held])
	rpc_players.rpc(states, Net.names)


@rpc("authority", "unreliable_ordered")
func rpc_players(states: Array, names: Dictionary) -> void:
	for k in names:
		Net.names[int(k)] = names[k]
	for s in states:
		var id: int = s[0]
		if id == Net.my_id():
			continue
		var a := _ensure_avatar(id)
		if a:
			a.set_display_name(Net.names.get(id, "Farmer"))
			a.set_state(s[1], s[2], s[3], s[4], s[5])


func _send_welcome(id: int) -> void:
	var st := stack()
	var pieces := []
	for p in st.pieces:
		if not p.selling:
			pieces.append(_piece_spawn_data(p))
	var lines := []
	for l in get_tree().get_nodes_in_group("conveyor"):
		lines.append(_line_data(l as ConveyorLine))
	var machines := []
	for m in get_tree().get_nodes_in_group("machine"):
		var mm := m as Machine
		if not mm.preview:
			machines.append([mm.net_id, mm.id, mm.global_transform])
	var data := {
		"economy": _economy(),
		"needle": [st.needle_pos, st.needle_xf, st.needle_taken],
		"taken": st.taken_indices(),
		"pieces": pieces,
		"lines": lines,
		"machines": machines,
		"won": main.won,
		"names": Net.names,
	}
	rpc_welcome.rpc_id(id, data)
	main.hud.toast("%s joined the farm" % Net.names.get(id, "A farmer"), "success")


@rpc("authority", "reliable")
func rpc_welcome(data: Dictionary) -> void:
	for k in data.names:
		Net.names[int(k)] = data.names[k]
	var st := stack()
	var nd: Array = data.needle
	st.set_needle_state(nd[0], nd[1], nd[2])
	for idx in (data.taken as PackedInt32Array):
		st.apply_shell_take(idx)
	for d in data.pieces:
		_apply_piece_spawn(d)
	for ld in data.lines:
		_apply_line(ld)
	for ld in data.lines:
		_apply_links(ld)
	for md in data.machines:
		_apply_machine(md[0], md[1], md[2])
	rpc_economy(data.economy)
	if data.won:
		main.won = true
	main.on_joined_world()


func _line_data(l: ConveyorLine) -> Dictionary:
	var splits := []
	for s in l.splits:
		if is_instance_valid(s.line):
			splits.append([s.along, (s.line as ConveyorLine).net_id])
	var feeds := []
	if not l.feeds.is_empty() and is_instance_valid(l.feeds.line):
		feeds = [(l.feeds.line as ConveyorLine).net_id, l.feeds.along]
	return {"id": l.net_id, "points": l.points, "grounds": l.grounds, "meters": l.meters, "splits": splits, "feeds": feeds}


func line_built(l: ConveyorLine) -> void:
	if Net.is_online() and Net.is_host():
		rpc_line.rpc(_line_data(l))


func lines_relinked(lines: Array) -> void:
	if Net.is_online() and Net.is_host():
		for l in lines:
			if is_instance_valid(l):
				rpc_line.rpc(_line_data(l))


@rpc("authority", "reliable")
func rpc_line(d: Dictionary) -> void:
	_apply_line(d)
	_apply_links(d)


func find_line(id: int) -> ConveyorLine:
	for l in get_tree().get_nodes_in_group("conveyor"):
		if (l as ConveyorLine).net_id == id:
			return l
	return null


func _apply_line(d: Dictionary) -> void:
	var existing := find_line(d.id)
	if existing:
		if existing.points != (d.points as PackedVector3Array):
			for c in existing.get_children():
				existing.remove_child(c)
				c.free()
			existing.splits.clear()
			existing.feeds = {}
			existing.build(d.points, d.grounds)
		return
	var line := ConveyorLine.new()
	line.net_id = d.id
	main.add_child(line)
	line.meters = d.meters
	line.build(d.points, d.grounds)


func _apply_links(d: Dictionary) -> void:
	var line := find_line(d.id)
	if line == null:
		return
	line.splits.clear()
	for s in d.splits:
		var b := find_line(s[1])
		if b:
			line.splits.append({"along": s[0], "line": b})
	line.feeds = {}
	if (d.feeds as Array).size() == 2:
		var t := find_line(d.feeds[0])
		if t:
			line.feeds = {"line": t, "along": d.feeds[1]}
	line._build_junctions()


func line_removed(id: int) -> void:
	if Net.is_online() and Net.is_host():
		rpc_line_removed.rpc(id)


@rpc("authority", "reliable")
func rpc_line_removed(id: int) -> void:
	var l := find_line(id)
	if l:
		l.queue_free()


func machine_placed(m: Machine) -> void:
	if Net.is_online() and Net.is_host():
		rpc_machine.rpc(m.net_id, m.id, m.global_transform)


func machine_removed(id: int) -> void:
	if Net.is_online() and Net.is_host():
		rpc_machine_removed.rpc(id)


func find_machine(id: int) -> Machine:
	for m in get_tree().get_nodes_in_group("machine"):
		if (m as Machine).net_id == id and not (m as Machine).preview:
			return m
	return null


@rpc("authority", "reliable")
func rpc_machine(id: int, type: String, xf: Transform3D) -> void:
	_apply_machine(id, type, xf)


func _apply_machine(id: int, type: String, xf: Transform3D) -> void:
	if find_machine(id):
		return
	var m := Machine.make(type)
	m.main = main
	m.net_id = id
	main.add_child(m)
	m.global_transform = xf


@rpc("authority", "reliable")
func rpc_machine_removed(id: int) -> void:
	var m := find_machine(id)
	if m:
		m.queue_free()


func sell_fx(at: Vector3, price: int) -> void:
	if Net.is_online() and Net.is_host():
		rpc_sell_fx.rpc(at, price)


@rpc("authority", "unreliable")
func rpc_sell_fx(at: Vector3, price: int) -> void:
	Sfx.play_at("sell", at, -4.0, 0.06)
	if main.sell_machine:
		main.sell_machine._pop(at, price)


func toast_to(id: int, text: String, kind: String) -> void:
	if id == Net.my_id():
		main.hud.toast(text, kind)
	elif Net.is_online():
		rpc_toast.rpc_id(id, text, kind)


@rpc("authority", "reliable")
func rpc_toast(text: String, kind: String) -> void:
	main.hud.toast(text, kind)


func win(stats: Dictionary) -> void:
	if Net.is_online() and Net.is_host():
		rpc_win.rpc(stats)


@rpc("authority", "reliable")
func rpc_win(stats: Dictionary) -> void:
	main.show_win(stats)


func _sender() -> int:
	var id := multiplayer.get_remote_sender_id()
	return id if id != 0 else 1


@rpc("any_peer", "reliable")
func req_take(point: Vector3, xf: Transform3D, grab: bool) -> void:
	var id := _sender()
	var info := stack().begin_take(point)
	if info.is_empty():
		return
	var piece := stack().finish_take(info, xf)
	if piece and grab:
		_hold_remote(piece, id)


@rpc("any_peer", "reliable")
func req_grab(piece_id: int) -> void:
	var id := _sender()
	if not stack().by_id.has(piece_id):
		return
	var piece: HayPiece = stack().by_id[piece_id]
	if piece.held_by != null or int(piece.get_meta("holder", 0)) != 0:
		return
	_hold_remote(piece, id)


func _hold_remote(piece: HayPiece, id: int) -> void:
	piece.set_meta("holder", id)
	piece.set_held(self)
	if remote_state.has(id):
		remote_state[id].held = piece.net_id
	rpc_held.rpc(piece.net_id, id)


@rpc("authority", "reliable")
func rpc_held(piece_id: int, holder: int) -> void:
	if not stack().by_id.has(piece_id):
		return
	var piece: HayPiece = stack().by_id[piece_id]
	piece.set_meta("holder", holder)
	if holder == Net.my_id():
		main.player.adopt_held(piece)


@rpc("any_peer", "reliable")
func req_release(piece_id: int, vel: Vector3, basis: Basis, ang: Vector3) -> void:
	if not stack().by_id.has(piece_id):
		return
	var piece: HayPiece = stack().by_id[piece_id]
	if int(piece.get_meta("holder", 0)) != _sender():
		return
	_release_remote(piece, vel, basis, ang)


func _release_remote(piece: HayPiece, vel: Vector3, basis := Basis(), ang := Vector3.ZERO) -> void:
	piece.set_meta("holder", 0)
	piece.set_held(null)
	if vel.length() > 0.01:
		piece.global_basis = basis
		piece.linear_velocity = vel
		piece.angular_velocity = ang
	rpc_held.rpc(piece.net_id, 0)


@rpc("any_peer", "reliable")
func req_fork(point: Vector3, normal: Vector3, toward: Vector3) -> void:
	var n := int(main.stat("fork_count"))
	PitchforkTool.fork_out(stack(), point, normal, toward, n)


func tank(peer: int, tool: String) -> CarryTank:
	var key := "%d:%s" % [peer, tool]
	if not tanks.has(key):
		tanks[key] = CarryTank.new()
	return tanks[key]


@rpc("any_peer", "reliable")
func req_absorb(piece_id: int, tool: String) -> void:
	var id := _sender()
	if not stack().by_id.has(piece_id):
		return
	var cap := int(main.stat("basket_capacity" if tool == "basket" else "vacuum_capacity"))
	var t := tank(id, tool)
	if t.count() >= cap:
		return
	t.absorb(stack().by_id[piece_id])
	rpc_tank.rpc_id(id, tool, t.count())


@rpc("any_peer", "reliable")
func req_emit(tool: String, pos: Vector3, vel: Vector3) -> void:
	var id := _sender()
	var t := tank(id, tool)
	t.emit_one(stack(), pos, vel)
	rpc_tank.rpc_id(id, tool, t.count())


@rpc("authority", "reliable")
func rpc_tank(tool: String, count: int) -> void:
	var tl: HandTool = main.belt.tools.get(tool)
	if tl and "remote_count" in tl:
		tl.set("remote_count", count)


@rpc("any_peer", "unreliable")
func req_blow(origin: Vector3, fwd: Vector3, dt: float) -> void:
	BlowerTool.blow(stack(), origin, fwd, float(main.stat("blower_force")), dt)


@rpc("any_peer", "unreliable")
func req_suck(nozzle: Vector3, origin: Vector3, fwd: Vector3, dt: float) -> void:
	var id := _sender()
	var t := tank(id, "vacuum")
	var before := t.count()
	VacuumTool.suck(stack(), t, int(main.stat("vacuum_capacity")), nozzle, origin, fwd, dt)
	if t.count() != before:
		rpc_tank.rpc_id(id, "vacuum", t.count())


@rpc("any_peer", "reliable")
func req_build_line(points: PackedVector3Array, grounds: PackedFloat32Array, meters: int, start_snap: Dictionary, end_snap: Dictionary, flow_forward: bool) -> void:
	var id := _sender()
	if meters > main.conveyor_meters:
		toast_to(id, "Not enough belt", "error")
		return
	main.tool.build_line_from(points, grounds, meters, _snap_from_net(start_snap), _snap_from_net(end_snap), flow_forward)


func snap_to_net(s: Dictionary) -> Dictionary:
	if s.is_empty():
		return {}
	var out := s.duplicate()
	var owner: Variant = s.get("owner")
	out.erase("owner")
	if owner is ConveyorLine:
		out["owner_line"] = (owner as ConveyorLine).net_id
	return out


func _snap_from_net(s: Dictionary) -> Dictionary:
	if s.is_empty():
		return {}
	var out := s.duplicate()
	if s.has("owner_line"):
		out["owner"] = find_line(s.owner_line)
	return out


@rpc("any_peer", "reliable")
func req_dismantle_line(line_id: int) -> void:
	var l := find_line(line_id)
	if l:
		main.tool.dismantle(l)


@rpc("any_peer", "reliable")
func req_reverse_line(line_id: int) -> void:
	var l := find_line(line_id)
	if l:
		main.tool.reverse_line(l)


@rpc("any_peer", "reliable")
func req_place_machine(type: String, xf: Transform3D) -> void:
	var id := _sender()
	if int(main.machines.get(type, 0)) <= 0:
		toast_to(id, "No %s left to place" % Catalog.item(type).name.to_lower(), "error")
		return
	MachineTool.place_machine(main, type, xf)


@rpc("any_peer", "reliable")
func req_pickup_machine(machine_id: int) -> void:
	var m := find_machine(machine_id)
	if m:
		MachineTool.pickup_machine(main, m)


@rpc("any_peer", "reliable")
func req_buy(item_id: String, amount: int) -> void:
	var id := _sender()
	main.buy(item_id, amount, id)
