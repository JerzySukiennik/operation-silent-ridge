# Net autoload: room codes, WebRTC multiplayer peer and ntfy.sh signaling (streamed subscribe, batched ICE) for host and clients.
extends Node

signal state_changed(state: String, detail: String)
signal peer_joined(id: int)
signal peer_left(id: int)

const NTFY_HOST := "ntfy.sh"
const PREFIX := "haystack-inc-v2-"
const ICE := {"iceServers": [{"urls": ["stun:stun.l.google.com:19302", "stun:stun1.l.google.com:19302", "stun:stun.cloudflare.com:3478"]}]}
const JOIN_TIMEOUT := 25.0
const CODE_CHARS := "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"

var mode := "offline"
var state := "offline"
var code := ""
var my_name := "Farmer"
var names := {}
var peer: WebRTCMultiplayerPeer
var _conns := {}
var _remote_set := {}
var _pending_ice := {}
var _outbox := {}
var _ice_timer := {}
var _cid := ""
var _host_ids := {}
var _next_id := 2
var _stream: NtfyStream
var _publish_queue: Array = []
var _publish_http: HTTPRequest
var _publishing := false
var _join_clock := 0.0
var _join_attempts := 0


func _ready() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS
	_publish_http = HTTPRequest.new()
	_publish_http.timeout = 10.0
	add_child(_publish_http)
	_publish_http.request_completed.connect(func(_r: int, c: int, _h: PackedStringArray, _b: PackedByteArray) -> void:
		_publishing = false
		if c >= 300:
			push_warning("ntfy publish failed %d" % c))
	multiplayer.peer_connected.connect(func(id: int) -> void:
		if mode == "client" and id == 1:
			_set_state("connected", "")
			_stop_signaling()
		peer_joined.emit(id))
	multiplayer.peer_disconnected.connect(func(id: int) -> void:
		names.erase(id)
		peer_left.emit(id)
		if mode == "client" and id == 1:
			leave()
			_set_state("failed", "The host left the game"))


func is_online() -> bool:
	return mode != "offline"


func is_host() -> bool:
	return mode != "client"


func is_client() -> bool:
	return mode == "client"


func my_id() -> int:
	return multiplayer.get_unique_id() if is_online() else 1


func _set_state(s: String, detail: String) -> void:
	state = s
	state_changed.emit(s, detail)


func _rand(n: int, chars := CODE_CHARS) -> String:
	var out := ""
	for i in n:
		out += chars[randi() % chars.length()]
	return out


func host() -> String:
	leave()
	randomize()
	code = _rand(5)
	peer = WebRTCMultiplayerPeer.new()
	var err := peer.create_server()
	if err != OK:
		_set_state("failed", "Could not start WebRTC (%d)" % err)
		return ""
	multiplayer.multiplayer_peer = peer
	mode = "host"
	names[1] = my_name
	_listen(PREFIX + code)
	_set_state("hosting", code)
	return code


func join(room: String) -> void:
	leave()
	randomize()
	code = room.strip_edges().to_upper()
	_early.clear()
	_cid = _rand(12, "abcdefghijklmnopqrstuvwxyz0123456789")
	mode = "client"
	_join_clock = 0.0
	_listen(PREFIX + code + "-" + _cid)
	await get_tree().create_timer(1.0).timeout
	if mode != "client":
		return
	_publish(PREFIX + code, {"t": "join", "cid": _cid, "name": my_name})
	_set_state("joining", code)


func _retry_join() -> void:
	var room := code
	var attempts := _join_attempts
	var clock := _join_clock
	leave()
	_join_attempts = attempts
	join(room)
	_join_clock = clock


func leave() -> void:
	_stop_signaling()
	for c in _conns.values():
		(c as WebRTCPeerConnection).close()
	_conns.clear()
	_remote_set.clear()
	_pending_ice.clear()
	_outbox.clear()
	_host_ids.clear()
	names.clear()
	_next_id = 2
	if peer:
		peer.close()
	peer = null
	multiplayer.multiplayer_peer = OfflineMultiplayerPeer.new()
	mode = "offline"
	code = ""
	if state != "offline":
		_set_state("offline", "")


func _listen(topic: String) -> void:
	_stop_signaling()
	_stream = NtfyStream.new()
	add_child(_stream)
	_stream.message.connect(_on_signal)
	_stream.start(NTFY_HOST, topic)


func _stop_signaling() -> void:
	if is_instance_valid(_stream):
		_stream.stop()
		_stream.queue_free()
	_stream = null


func _publish(topic: String, data: Dictionary) -> void:
	_publish_queue.append([topic, JSON.stringify(data)])


func _process(delta: float) -> void:
	if not _publishing and not _publish_queue.is_empty():
		var item: Array = _publish_queue.pop_front()
		_publishing = true
		var err := _publish_http.request("https://%s/%s" % [NTFY_HOST, item[0]], ["Content-Type: text/plain"], HTTPClient.METHOD_POST, item[1])
		if err != OK:
			_publishing = false
	for id in _ice_timer.keys():
		_ice_timer[id] -= delta
		if _ice_timer[id] <= 0.0:
			_flush_ice(id)
	if state == "joining" or state == "connecting":
		_join_clock += delta
		if _join_clock > 12.0 and _join_attempts < 2 and state != "connected":
			_join_attempts += 1
			_retry_join()
		elif _join_clock > JOIN_TIMEOUT * 1.6:
			leave()
			_set_state("failed", "No answer from room %s. Check the code and that the host is online." % code)


func _make_conn(id: int, topic: String, tag: Dictionary) -> WebRTCPeerConnection:
	var conn := WebRTCPeerConnection.new()
	conn.initialize(ICE)
	conn.session_description_created.connect(func(type: String, sdp: String) -> void:
		conn.set_local_description(type, sdp)
		var msg := tag.duplicate()
		msg.merge({"t": "sdp", "type": type, "sdp": sdp})
		_publish(topic, msg))
	conn.ice_candidate_created.connect(func(media: String, index: int, cand: String) -> void:
		if not _outbox.has(id):
			_outbox[id] = {"topic": topic, "tag": tag, "list": []}
		(_outbox[id].list as Array).append([media, index, cand])
		if not _ice_timer.has(id):
			_ice_timer[id] = 0.6)
	_conns[id] = conn
	_remote_set[id] = false
	_pending_ice[id] = []
	return conn


func _flush_ice(id: int) -> void:
	_ice_timer.erase(id)
	if not _outbox.has(id):
		return
	var box: Dictionary = _outbox[id]
	_outbox.erase(id)
	var msg: Dictionary = (box.tag as Dictionary).duplicate()
	msg.merge({"t": "ice", "list": box.list})
	_publish(box.topic, msg)


func _add_ice(id: int, list: Array) -> void:
	if not _conns.has(id):
		return
	if not _remote_set.get(id, false):
		(_pending_ice[id] as Array).append_array(list)
		return
	for c in list:
		(_conns[id] as WebRTCPeerConnection).add_ice_candidate(c[0], int(c[1]), c[2])


func _set_remote(id: int, type: String, sdp: String) -> void:
	var conn: WebRTCPeerConnection = _conns[id]
	conn.set_remote_description(type, sdp)
	_remote_set[id] = true
	var pending: Array = _pending_ice[id]
	_pending_ice[id] = []
	_add_ice(id, pending)


func _on_signal(data: Dictionary) -> void:
	var t: String = data.get("t", "")
	if mode == "host":
		var cid: String = data.get("cid", "")
		if cid == "":
			return
		match t:
			"join":
				if _host_ids.has(cid):
					return
				var id := _next_id
				_next_id += 1
				_host_ids[cid] = id
				names[id] = str(data.get("name", "Farmer")).substr(0, 16)
				var topic := PREFIX + code + "-" + cid
				var conn := _make_conn(id, topic, {})
				peer.add_peer(conn, id)
				_publish(topic, {"t": "welcome", "id": id, "host": my_name})
				conn.create_offer()
			"sdp":
				if _host_ids.has(cid):
					_set_remote(_host_ids[cid], data.type, data.sdp)
			"ice":
				if _host_ids.has(cid):
					_add_ice(_host_ids[cid], data.get("list", []))
	elif mode == "client":
		match t:
			"welcome":
				if peer != null:
					return
				var id := int(data.id)
				peer = WebRTCMultiplayerPeer.new()
				peer.create_client(id)
				multiplayer.multiplayer_peer = peer
				names[1] = str(data.get("host", "Host"))
				var conn := _make_conn(1, PREFIX + code, {"cid": _cid})
				peer.add_peer(conn, 1)
				_set_state("connecting", code)
				for queued in _early:
					_on_signal(queued)
				_early.clear()
			"sdp", "ice":
				if not _conns.has(1):
					_early.append(data)
					return
				if t == "sdp":
					_set_remote(1, data.type, data.sdp)
				else:
					_add_ice(1, data.get("list", []))


var _early: Array = []
