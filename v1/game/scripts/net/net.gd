# Net autoload: public ntfy room lobby, WebRTC host-star transport, roster, clock sync and state/event relay used by NetSync.
extends Node

signal rooms_updated(rooms: Array)
signal state_changed(state: String, detail: String)
signal peer_joined(id: int)
signal peer_left(id: int)
signal lobby_status_changed(online: bool)
signal state_packet(owner: int, pkt: PackedByteArray)
signal event_packet(event_name: String, data: Variant, from_id: int)

const PROTOCOL := 1
const DEFAULT_VERSION := "0.1.0"
const NTFY_DEFAULT := "https://ntfy.sh"
const TOPIC_ROOT := "osr-silentridge-p1"
const MAX_PLAYERS := 4
const DEFAULT_MISSION := "FREE FLIGHT"
const ALL := 0
const HOST_ID := 1
# ntfy.sh gives an anonymous IP 250 published messages per day, so the heartbeat is sparse.
const HEARTBEAT := 90.0
const ANNOUNCE_MIN_GAP := 6.0
const ROOM_EXPIRE := 200.0
const LOBBY_SINCE := "210s"
const LOBBY_DOWN_AFTER := 8.0
const ICE := {"iceServers": [{"urls": ["stun:stun.l.google.com:19302", "stun:stun.cloudflare.com:3478"]}]}
const GATHER_MAX := 1.2
const ICE_BATCH := 0.8
const JOIN_RESEND := 8.0
const JOIN_NO_ANSWER := 17.0
const CONNECT_TIMEOUT := 30.0
const PENDING_TIMEOUT := 32.0
const HOST_SILENT_TIMEOUT := 20.0
const CLIENT_SILENT_TIMEOUT := 25.0
const ID_CHARS := "abcdefghijkmnpqrstuvwxyz23456789"

var version := DEFAULT_VERSION
var ntfy_base := NTFY_DEFAULT
var lobby_suffix := ""
var mode := "offline"
var state := "offline"
var last_error := ""
var my_name := "Pilot"
var mission := DEFAULT_MISSION
var room_id := ""
var names := {}
var latest_state := {}
var peer: WebRTCMultiplayerPeer
var clock_offset := 0.0
var rtt := 0.0
var rtt_min := 0.0
var clock_samples := 0
var lobby_online := false
var lobby_known := false
var browsing := false

var _conns := {}
var _remote_set := {}
var _pending_ice := {}
var _outbox := {}
var _pending := {}
var _host_ids := {}
var _host_offer_cache := {}
var _cid := ""
var _early: Array = []
var _sig: NtfyStream
var _lobby: NtfyStream
var _pub: NtfyPublisher
var _rooms := {}
var _dead_rooms := {}
var _lobby_down_clock := 0.0
var _rooms_clock := 0.0
var _announce_clock := 0.0
var _announce_gap := 0.0
var _announce_dirty := false
var _created_unix := 0
var _join_clock := 0.0
var _join_resent := false
var _ping_clock := 0.0
var _silence := {}
var _samples: Array = []
var _peer_rtt := {}
var _event_buffer: Array = []
var _sync_attached := false
var _closing: Array = []


func _ready() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS
	var v := str(ProjectSettings.get_setting("application/config/version", ""))
	if v != "":
		version = v
	for a in OS.get_cmdline_user_args():
		var s := a.trim_prefix("--")
		if s.begins_with("ntfy="):
			ntfy_base = s.substr(5)
		elif s.begins_with("lobby="):
			lobby_suffix = s.substr(6)
		elif s.begins_with("net_version="):
			version = s.substr(12)
	_pub = NtfyPublisher.new()
	_pub.base = ntfy_base
	add_child(_pub)
	_pub.quota_exceeded.connect(func() -> void:
		var msg := "The lobby server refused a message (ntfy daily limit for this network reached). Offline solo still works."
		push_warning(msg)
		if mode == "client" and (state == "joining" or state == "connecting"):
			_fail(msg)
		else:
			last_error = msg)
	_pub.published.connect(func(topic: String, ok: bool, _code: int) -> void:
		if not ok and mode == "host" and topic == lobby_topic():
			_announce_clock = minf(_announce_clock, 10.0))
	multiplayer.peer_connected.connect(_on_peer_connected)
	multiplayer.peer_disconnected.connect(_on_peer_disconnected)
	multiplayer.multiplayer_peer = OfflineMultiplayerPeer.new()
	get_tree().auto_accept_quit = false


func _notification(what: int) -> void:
	if what != NOTIFICATION_WM_CLOSE_REQUEST:
		return
	if mode == "host":
		leave()
		var until := Time.get_ticks_msec() + 1500
		while _pub.pending() > 0 and Time.get_ticks_msec() < until:
			await get_tree().process_frame
	get_tree().quit()


func is_online() -> bool:
	return mode == "host" or mode == "client"


func is_host() -> bool:
	return mode != "client"


func is_client() -> bool:
	return mode == "client"


func is_in_session() -> bool:
	return state == "hosting" or state == "connected" or state == "solo"


func my_id() -> int:
	return multiplayer.get_unique_id() if is_online() and peer != null else 1


func player_count() -> int:
	return maxi(names.size(), 1)


func ping_ms(id := 0) -> float:
	if id == 0 or id == my_id():
		return rtt * 1000.0 if is_client() else 0.0
	return float(_peer_rtt.get(id, 0.0))


func local_time() -> float:
	return Time.get_ticks_usec() / 1000000.0


func net_time() -> float:
	return local_time() + clock_offset


func to_local_time(t_net: float) -> float:
	return t_net - clock_offset


func lobby_topic() -> String:
	return TOPIC_ROOT + "-lobby" + ("-" + lobby_suffix if lobby_suffix != "" else "")


func room_topic(id: String) -> String:
	return TOPIC_ROOT + "-room-" + id


func rooms() -> Array:
	var now := int(Time.get_unix_time_from_system())
	var out: Array = []
	for r: Dictionary in _rooms.values():
		var age := maxi(now - int(r.ts), 0)
		var full := int(r.players) >= int(r.max)
		var ver_ok := str(r.version) == version and int(r.proto) == PROTOCOL
		out.append({"id": r.id, "host": r.host, "players": r.players, "max": r.max, "mission": r.mission,
			"version": r.version, "age_s": age, "uptime_s": maxi(now - int(r.created), 0),
			"full": full, "version_ok": ver_ok, "joinable": ver_ok and not full})
	out.sort_custom(func(a: Dictionary, b: Dictionary) -> bool:
		if a.joinable != b.joinable:
			return a.joinable
		return int(a.uptime_s) < int(b.uptime_s))
	return out


func start_browsing() -> void:
	browsing = true
	if is_instance_valid(_lobby):
		rooms_updated.emit(rooms())
		return
	_lobby = NtfyStream.new()
	add_child(_lobby)
	_lobby.message.connect(_on_lobby_msg)
	_lobby.opened.connect(func() -> void: _set_lobby_online(true))
	_lobby.closed.connect(func() -> void: _lobby_down_clock = 0.0)
	_lobby_down_clock = 0.0
	_lobby.start(ntfy_base, lobby_topic(), LOBBY_SINCE)


func stop_browsing() -> void:
	browsing = false
	if is_instance_valid(_lobby):
		_lobby.stop()
		_lobby.queue_free()
	_lobby = null


func host(pilot_name := "", p_mission := DEFAULT_MISSION) -> bool:
	leave()
	randomize()
	if pilot_name != "":
		my_name = pilot_name.substr(0, 16)
	mission = p_mission
	last_error = ""
	peer = WebRTCMultiplayerPeer.new()
	var err := peer.create_server()
	if err != OK:
		peer = null
		play_offline(pilot_name, "WebRTC is unavailable (%d), flying offline." % err)
		return false
	multiplayer.multiplayer_peer = peer
	_no_relay()
	mode = "host"
	room_id = _rand(8)
	names = {HOST_ID: my_name}
	clock_offset = 0.0
	_created_unix = int(Time.get_unix_time_from_system())
	_listen(room_topic(room_id), str(_created_unix - 5))
	_announce()
	_set_state("hosting", room_id)
	return true


func join(p_room_id: String, pilot_name := "") -> void:
	leave()
	randomize()
	if pilot_name != "":
		my_name = pilot_name.substr(0, 16)
	last_error = ""
	room_id = p_room_id
	if _rooms.has(room_id):
		var r: Dictionary = _rooms[room_id]
		if str(r.version) != version or int(r.proto) != PROTOCOL:
			_fail("Version mismatch: this room runs %s, you have %s." % [r.version, version])
			return
		if int(r.players) >= int(r.max):
			_fail("That room is full (%d/%d)." % [r.players, r.max])
			return
	mode = "client"
	_cid = _rand(10)
	_early.clear()
	_join_clock = 0.0
	_join_resent = false
	_listen(room_topic(room_id) + "-" + _cid, str(int(Time.get_unix_time_from_system()) - 5))
	_send_join()
	_set_state("joining", room_id)


func play_offline(pilot_name := "", note := "") -> void:
	leave()
	if pilot_name != "":
		my_name = pilot_name.substr(0, 16)
	names = {HOST_ID: my_name}
	mode = "offline"
	_set_state("solo", note)


func leave() -> void:
	if mode == "host" and room_id != "":
		_pub.publish(lobby_topic(), {"t": "close", "id": room_id, "ts": int(Time.get_unix_time_from_system())})
		if peer != null and names.size() > 1:
			_rx_bye.rpc("host")
	elif mode == "client" and peer != null and state == "connected":
		_rx_bye.rpc_id(HOST_ID, "client")
	_stop_signaling()
	if peer != null:
		_closing.append({"peer": peer, "conns": _conns.values(), "t": 0.6})
	_conns.clear()
	_remote_set.clear()
	_pending_ice.clear()
	_outbox.clear()
	_pending.clear()
	_host_ids.clear()
	_host_offer_cache.clear()
	_silence.clear()
	_peer_rtt.clear()
	_samples.clear()
	names.clear()
	latest_state.clear()
	_event_buffer.clear()
	clock_offset = 0.0
	clock_samples = 0
	rtt = 0.0
	rtt_min = 0.0
	peer = null
	multiplayer.multiplayer_peer = OfflineMultiplayerPeer.new()
	mode = "offline"
	room_id = ""
	if state != "offline" and state != "failed":
		_set_state("offline", "")


func send_state(pkt: PackedByteArray) -> void:
	if not is_online() or peer == null:
		return
	latest_state[my_id()] = pkt
	if mode == "host":
		for id: int in names:
			if id != HOST_ID:
				_rx_state.rpc_id(id, pkt)
	elif state == "connected":
		_rx_state.rpc_id(HOST_ID, pkt)


func send_event(event_name: String, data: Variant = null, to := ALL) -> void:
	if not is_online() or peer == null:
		return
	if mode == "host":
		_route_event(to, event_name, data, HOST_ID)
	elif state == "connected":
		_rx_event.rpc_id(HOST_ID, to, event_name, data, 0)


func attach_sync(on: bool) -> Array:
	_sync_attached = on
	var out := _event_buffer
	_event_buffer = []
	return out


func _on_lobby_msg(d: Dictionary) -> void:
	var id := str(d.get("id", ""))
	if id == "":
		return
	match str(d.get("t", "")):
		"room":
			if _dead_rooms.has(id) and int(d.get("ts", 0)) <= int(_dead_rooms[id]):
				return
			if _dead_rooms.has(id):
				_dead_rooms.erase(id)
			_rooms[id] = {"id": id, "host": str(d.get("host", "Pilot")).substr(0, 16), "players": clampi(int(d.get("players", 1)), 0, 16),
				"max": clampi(int(d.get("max", MAX_PLAYERS)), 1, 16), "mission": str(d.get("mission", "")).substr(0, 32),
				"version": str(d.get("ver", "?")).substr(0, 24), "proto": int(d.get("proto", 0)), "ts": int(d.get("ts", 0)),
				"created": int(d.get("created", d.get("ts", 0)))}
		"close":
			_rooms.erase(id)
			_dead_rooms[id] = int(d.get("ts", Time.get_unix_time_from_system()))
	_rooms_clock = 0.0
	rooms_updated.emit(rooms())


func _set_lobby_online(v: bool) -> void:
	_lobby_down_clock = 0.0
	if v == lobby_online and lobby_known:
		return
	lobby_online = v
	lobby_known = true
	lobby_status_changed.emit(v)


func _announce() -> void:
	if mode != "host" or room_id == "":
		return
	_announce_clock = HEARTBEAT
	_announce_gap = ANNOUNCE_MIN_GAP
	_announce_dirty = false
	_pub.publish(lobby_topic(), {"t": "room", "id": room_id, "host": my_name, "players": names.size(), "max": MAX_PLAYERS,
		"mission": mission, "ver": version, "proto": PROTOCOL, "ts": int(Time.get_unix_time_from_system()), "created": _created_unix})


func _listen(topic: String, since: String) -> void:
	_stop_signaling()
	_sig = NtfyStream.new()
	add_child(_sig)
	_sig.message.connect(_on_signal)
	_sig.start(ntfy_base, topic, since)


func _stop_signaling() -> void:
	if is_instance_valid(_sig):
		_sig.stop()
		_sig.queue_free()
	_sig = null


func _send_join() -> void:
	_pub.publish(room_topic(room_id), {"t": "join", "cid": _cid, "name": my_name, "ver": version, "proto": PROTOCOL})


func _make_conn(id: int, topic: String, tag: Dictionary) -> WebRTCPeerConnection:
	var conn := WebRTCPeerConnection.new()
	conn.initialize(ICE)
	conn.session_description_created.connect(func(type: String, sdp: String) -> void:
		conn.set_local_description(type, sdp)
		_outbox[id] = {"topic": topic, "tag": tag, "type": type, "sdp": sdp, "list": _outbox.get(id, {}).get("list", []), "t": 0.0, "sent": false})
	conn.ice_candidate_created.connect(func(media: String, index: int, cand: String) -> void:
		if not _outbox.has(id):
			_outbox[id] = {"topic": topic, "tag": tag, "list": [], "t": 0.0, "sent": true, "pre": true}
		var box: Dictionary = _outbox[id]
		(box.list as Array).append([media, index, cand])
		if box.sent and float(box.t) <= 0.0:
			box.t = ICE_BATCH)
	_conns[id] = conn
	_remote_set[id] = false
	_pending_ice[id] = []
	return conn


func _pump_outbox(delta: float) -> void:
	for id: int in _outbox.keys():
		var box: Dictionary = _outbox[id]
		if not _conns.has(id):
			_outbox.erase(id)
			continue
		var conn: WebRTCPeerConnection = _conns[id]
		if not box.sent:
			box.t = float(box.t) + delta
			if conn.get_gathering_state() == WebRTCPeerConnection.GATHERING_STATE_COMPLETE or float(box.t) >= GATHER_MAX:
				var msg: Dictionary = (box.tag as Dictionary).duplicate()
				msg.merge({"t": "sdp", "type": box.type, "sdp": box.sdp, "ice": box.list})
				_pub.publish(box.topic, msg)
				if mode == "host":
					_host_offer_cache[id] = [box.topic, msg]
				box.sent = true
				box.list = []
				box.t = 0.0
		elif not (box.list as Array).is_empty() and not box.has("pre"):
			box.t = float(box.t) - delta
			if float(box.t) <= 0.0:
				var msg: Dictionary = (box.tag as Dictionary).duplicate()
				msg.merge({"t": "ice", "list": box.list})
				_pub.publish(box.topic, msg)
				box.list = []
				box.t = 0.0


func _add_ice(id: int, list: Array) -> void:
	if not _conns.has(id):
		return
	if not _remote_set.get(id, false):
		(_pending_ice[id] as Array).append_array(list)
		return
	for c: Variant in list:
		if c is Array and (c as Array).size() == 3:
			(_conns[id] as WebRTCPeerConnection).add_ice_candidate(str(c[0]), int(c[1]), str(c[2]))


func _set_remote(id: int, type: String, sdp: String) -> void:
	if not _conns.has(id) or _remote_set.get(id, false):
		return
	(_conns[id] as WebRTCPeerConnection).set_remote_description(type, sdp)
	_remote_set[id] = true
	var pending: Array = _pending_ice[id]
	_pending_ice[id] = []
	_add_ice(id, pending)


func _free_id() -> int:
	for i in range(2, 250):
		if not names.has(i) and not _pending.has(i):
			return i
	return -1


func _on_signal(data: Dictionary) -> void:
	var t := str(data.get("t", ""))
	if mode == "host":
		var cid := str(data.get("cid", ""))
		if cid == "":
			return
		match t:
			"join":
				_host_on_join(cid, data)
			"sdp":
				if _host_ids.has(cid):
					var id: int = _host_ids[cid]
					_set_remote(id, str(data.get("type", "answer")), str(data.get("sdp", "")))
					_add_ice(id, data.get("ice", []))
			"ice":
				if _host_ids.has(cid):
					_add_ice(_host_ids[cid], data.get("list", []))
	elif mode == "client":
		match t:
			"reject":
				var reason := str(data.get("reason", ""))
				if reason == "full":
					_fail("That room is full (%d/%d)." % [MAX_PLAYERS, MAX_PLAYERS])
				elif reason == "version":
					_fail("Version mismatch: the host runs %s, you have %s." % [data.get("ver", "?"), version])
				else:
					_fail("The host turned the join down.")
			"sdp":
				if peer == null:
					_client_on_offer(data)
				elif _conns.has(HOST_ID):
					_set_remote(HOST_ID, str(data.get("type", "offer")), str(data.get("sdp", "")))
			"ice":
				if not _conns.has(HOST_ID):
					_early.append(data)
				else:
					_add_ice(HOST_ID, data.get("list", []))


func _host_on_join(cid: String, data: Dictionary) -> void:
	var inbox := room_topic(room_id) + "-" + cid
	if _host_ids.has(cid):
		var known: int = _host_ids[cid]
		if _pending.has(known) and _host_offer_cache.has(known):
			var cached: Array = _host_offer_cache[known]
			_host_offer_cache.erase(known)
			_pub.publish(cached[0], cached[1])
		return
	if str(data.get("ver", "")) != version or int(data.get("proto", 0)) != PROTOCOL:
		_host_ids[cid] = -1
		_pub.publish(inbox, {"t": "reject", "reason": "version", "ver": version})
		return
	var id := _free_id()
	if names.size() + _pending.size() >= MAX_PLAYERS or id < 0:
		_host_ids[cid] = -1
		_pub.publish(inbox, {"t": "reject", "reason": "full"})
		return
	_host_ids[cid] = id
	_pending[id] = {"cid": cid, "name": _clean_name(str(data.get("name", "Pilot"))), "t": 0.0}
	var conn := _make_conn(id, inbox, {"id": id, "host": my_name})
	peer.add_peer(conn, id)
	conn.create_offer()


func _client_on_offer(data: Dictionary) -> void:
	var id := int(data.get("id", 0))
	if id < 2:
		return
	peer = WebRTCMultiplayerPeer.new()
	peer.create_client(id)
	multiplayer.multiplayer_peer = peer
	_no_relay()
	names = {HOST_ID: _clean_name(str(data.get("host", "Host")))}
	var conn := _make_conn(HOST_ID, room_topic(room_id), {"cid": _cid})
	peer.add_peer(conn, HOST_ID)
	_set_remote(HOST_ID, str(data.get("type", "offer")), str(data.get("sdp", "")))
	_add_ice(HOST_ID, data.get("ice", []))
	for queued: Dictionary in _early:
		_add_ice(HOST_ID, queued.get("list", []))
	_early.clear()
	_join_clock = 0.0
	_set_state("connecting", room_id)


func _on_peer_connected(id: int) -> void:
	if mode == "host":
		if not _pending.has(id):
			return
		names[id] = _pending[id].name
		_pending.erase(id)
		_host_offer_cache.erase(id)
		_silence[id] = 0.0
		_broadcast_roster()
		_announce_dirty = true
		peer_joined.emit(id)
	elif mode == "client" and id == HOST_ID:
		_stop_signaling()
		_silence[HOST_ID] = 0.0
		_ping_clock = 0.0
		_send_ping()


func _on_peer_disconnected(id: int) -> void:
	if mode == "host":
		_drop_client(id)
	elif mode == "client" and id == HOST_ID:
		_host_lost("The host left the mission.")


func _drop_client(id: int) -> void:
	_pending.erase(id)
	_conns.erase(id)
	_remote_set.erase(id)
	_pending_ice.erase(id)
	_outbox.erase(id)
	_silence.erase(id)
	_peer_rtt.erase(id)
	latest_state.erase(id)
	if peer != null and peer.has_peer(id):
		peer.remove_peer(id)
	if names.has(id):
		names.erase(id)
		_broadcast_roster()
		_announce_dirty = true
		peer_left.emit(id)


func _host_lost(reason: String) -> void:
	if mode != "client":
		return
	if state != "connected":
		reason = "The connection to the host failed."
	var ids := names.keys()
	var me := my_id()
	state = "failed"
	leave()
	for id: int in ids:
		if id != me:
			peer_left.emit(id)
	_fail(reason)


func _broadcast_roster() -> void:
	for id: int in names:
		if id != HOST_ID:
			_rx_roster.rpc_id(id, names)


func _no_relay() -> void:
	var sm := multiplayer as SceneMultiplayer
	if sm:
		sm.server_relay = false


@rpc("authority", "call_remote", "reliable")
func _rx_roster(r: Dictionary) -> void:
	_silence[HOST_ID] = 0.0
	var prev := names
	names = {}
	for k: Variant in r:
		names[int(k)] = _clean_name(str(r[k]))
	var me := my_id()
	for id: int in names:
		if id != me and not prev.has(id):
			peer_joined.emit(id)
	for id: int in prev:
		if id != me and not names.has(id):
			latest_state.erase(id)
			peer_left.emit(id)
	if state == "connecting":
		_set_state("connected", room_id)


@rpc("any_peer", "call_remote", "reliable")
func _rx_bye(who: String) -> void:
	var sender := multiplayer.get_remote_sender_id()
	if mode == "host" and who == "client":
		_drop_client(sender)
	elif mode == "client" and sender == HOST_ID:
		_host_lost("The host ended the mission.")


@rpc("any_peer", "call_remote", "unreliable_ordered")
func _rx_state(pkt: PackedByteArray) -> void:
	var sender := multiplayer.get_remote_sender_id()
	if pkt.size() < 5:
		return
	var owner := pkt[0]
	_silence[sender] = 0.0
	if mode == "host":
		if owner != sender or not names.has(sender):
			return
		latest_state[owner] = pkt
		state_packet.emit(owner, pkt)
		for id: int in names:
			if id != HOST_ID and id != sender:
				_rx_state.rpc_id(id, pkt)
	elif sender == HOST_ID and owner != my_id():
		latest_state[owner] = pkt
		state_packet.emit(owner, pkt)


@rpc("any_peer", "call_remote", "reliable")
func _rx_event(to: int, event_name: String, data: Variant, from_id: int) -> void:
	var sender := multiplayer.get_remote_sender_id()
	_silence[sender] = 0.0
	if mode == "host":
		if names.has(sender):
			_route_event(to, event_name, data, sender)
	elif sender == HOST_ID:
		_emit_event(event_name, data, from_id)


func _route_event(to: int, event_name: String, data: Variant, from_id: int) -> void:
	if to == ALL or to == HOST_ID:
		if from_id != HOST_ID:
			_emit_event(event_name, data, from_id)
	if to == ALL:
		for id: int in names:
			if id != HOST_ID and id != from_id:
				_rx_event.rpc_id(id, to, event_name, data, from_id)
	elif to != HOST_ID and names.has(to) and to != from_id:
		_rx_event.rpc_id(to, to, event_name, data, from_id)


func _emit_event(event_name: String, data: Variant, from_id: int) -> void:
	if not _sync_attached:
		if _event_buffer.size() < 256:
			_event_buffer.append([event_name, data, from_id])
		return
	event_packet.emit(event_name, data, from_id)


@rpc("any_peer", "call_remote", "unreliable_ordered")
func _rx_ping(t_client: float, client_rtt_ms: float) -> void:
	var sender := multiplayer.get_remote_sender_id()
	if mode != "host" or not names.has(sender):
		return
	_silence[sender] = 0.0
	_peer_rtt[sender] = client_rtt_ms
	_rx_pong.rpc_id(sender, t_client, local_time())


@rpc("authority", "call_remote", "unreliable_ordered")
func _rx_pong(t_client: float, t_host: float) -> void:
	_silence[HOST_ID] = 0.0
	var t3 := local_time()
	var r := t3 - t_client
	if r < 0.0 or r > 5.0:
		return
	_samples.append([r, t_host + r * 0.5 - t3])
	if _samples.size() > 16:
		_samples.pop_front()
	var best: Array = _samples[0]
	for s: Array in _samples:
		if s[0] < best[0]:
			best = s
	rtt_min = best[0]
	if clock_samples < 6:
		clock_offset = best[1]
		rtt = r
	else:
		clock_offset = lerpf(clock_offset, best[1], 0.2)
		rtt = lerpf(rtt, r, 0.15)
	clock_samples += 1


func _send_ping() -> void:
	if mode == "client" and peer != null and _conns.has(HOST_ID):
		_rx_ping.rpc_id(HOST_ID, local_time(), rtt * 1000.0)


func _process(delta: float) -> void:
	var dt := minf(delta, 0.25)
	for c: Dictionary in _closing.duplicate():
		c.t = float(c.t) - delta
		(c.peer as WebRTCMultiplayerPeer).poll()
		if float(c.t) <= 0.0:
			(c.peer as WebRTCMultiplayerPeer).close()
			_closing.erase(c)
	_pump_outbox(delta)
	if browsing:
		if is_instance_valid(_lobby) and not _lobby.is_open():
			_lobby_down_clock += delta
			if (_lobby_down_clock > LOBBY_DOWN_AFTER or _lobby.fail_count >= 2) and (lobby_online or not lobby_known):
				lobby_online = false
				lobby_known = true
				lobby_status_changed.emit(false)
		_rooms_clock += delta
		if _rooms_clock >= 1.0:
			_rooms_clock = 0.0
			var now := int(Time.get_unix_time_from_system())
			for id: String in _rooms.keys():
				if now - int(_rooms[id].ts) > ROOM_EXPIRE:
					_rooms.erase(id)
			rooms_updated.emit(rooms())
	match mode:
		"host":
			_announce_clock -= delta
			_announce_gap -= delta
			if _announce_clock <= 0.0 or (_announce_dirty and _announce_gap <= 0.0):
				_announce()
			for id: int in _pending.keys():
				_pending[id].t = float(_pending[id].t) + delta
				if float(_pending[id].t) > PENDING_TIMEOUT:
					_drop_client(id)
			for id: int in names:
				if id != HOST_ID:
					_silence[id] = float(_silence.get(id, 0.0)) + dt
					if float(_silence[id]) > CLIENT_SILENT_TIMEOUT:
						_drop_client.call_deferred(id)
		"client":
			_join_clock += delta
			if state == "joining":
				if _join_clock > JOIN_RESEND and not _join_resent:
					_join_resent = true
					_send_join()
				elif _join_clock > JOIN_NO_ANSWER:
					_dead_rooms[room_id] = int(Time.get_unix_time_from_system())
					_rooms.erase(room_id)
					var rid := room_id
					leave()
					_fail("The host isn't answering; room %s is probably gone." % rid)
					rooms_updated.emit(rooms())
			elif state == "connecting" and _join_clock > CONNECT_TIMEOUT:
				leave()
				_fail("Couldn't open a connection to the host (network/NAT). Try again or host your own.")
			elif state == "connected" or (state == "connecting" and _silence.has(HOST_ID)):
				_ping_clock -= delta
				if _ping_clock <= 0.0:
					_ping_clock = 0.5 if clock_samples < 10 else 2.0
					_send_ping()
				_silence[HOST_ID] = float(_silence.get(HOST_ID, 0.0)) + dt
				if float(_silence[HOST_ID]) > HOST_SILENT_TIMEOUT:
					_host_lost("Lost the connection to the host.")


func _set_state(s: String, detail: String) -> void:
	state = s
	state_changed.emit(s, detail)


func _fail(reason: String) -> void:
	last_error = reason
	if mode != "offline":
		leave()
	_set_state("failed", reason)


func _clean_name(s: String) -> String:
	var n := s.strip_edges().substr(0, 16)
	return n if n != "" else "Pilot"


func _rand(n: int) -> String:
	var out := ""
	for i in n:
		out += ID_CHARS[randi() % ID_CHARS.length()]
	return out
