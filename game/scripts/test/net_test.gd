# Headless multiplayer test driver: one process per player (host or client) with a circle-flying dummy; prints REPORT lines on lobby, joins, state rate, latency, clock sync, events and leaves.
extends Node

var args := {}
var role := "client"
var pilot := "Pilot"
var t := 0.0
var sync: NetSync
var dummy: NetDummy
var streams := {}
var spawned: Array[String] = []
var left: Array[String] = []
var events_rx := {}
var room_seen := {}
var joined := false
var connected_at := -1.0
var host_lost_at := -1.0
var room_gone_at := -1.0
var close_seen_room := ""
var done := false
var fail_reason := ""
var ev_clock := 0.0
var ev_sent := 0
var lobby_states: Array = []
var clock_snap := [0.0, 0.0, 0, 0.0]


func _ready() -> void:
	Engine.max_fps = 60
	args = Engine.get_meta("boot_args", {}).duplicate()
	# short presets keep the HP scheduled-task command line under Windows' 261-char /tr limit
	var presets := {
		"xh": {"role": "host", "name": "HP-Host", "quit_at": 45, "expect_peers": 2, "max_lat_ms": 300},
		"xc": {"role": "client", "name": "HP-Client", "host_name": "Mac-Host", "join_at": 0, "leave_at": 40, "run": 60, "expect_peers": 1, "max_lat_ms": 300},
	}
	if presets.has(str(args.get("p", ""))):
		args.merge(presets[str(args.p)])
	role = str(args.get("role", "client"))
	pilot = str(args.get("name", role.capitalize()))
	Net.state_changed.connect(_on_state)
	Net.rooms_updated.connect(_on_rooms)
	Net.lobby_status_changed.connect(func(on: bool) -> void:
		lobby_states.append(on)
		_log("lobby online=%s" % on))
	_log("start role=%s version=%s lobby=%s ntfy=%s" % [role, Net.version, Net.lobby_topic(), Net.ntfy_base])
	if role == "host":
		Net.host(pilot, str(args.get("mission", "FREE FLIGHT")))
		_log("hosting room=%s" % Net.room_id)
		_make_sync()
	else:
		Net.start_browsing()


func _log(s: String) -> void:
	print("[%6.2f] %s: %s" % [t, pilot, s])


func _report(s: String) -> void:
	print("REPORT %s %s" % [pilot, s])


func _num(key: String, def: float) -> float:
	return float(args.get(key, def))


func _on_state(s: String, detail: String) -> void:
	_log("state %s %s" % [s, detail])
	if s == "connected" and role == "client":
		connected_at = t
		_report("connected t=%.2f room=%s id=%d roster=%s" % [t, Net.room_id, Net.my_id(), _roster_names()])
		_make_sync()
		Net.send_event("hi", {"from": pilot})
	elif s == "failed":
		if joined and connected_at >= 0.0 and host_lost_at < 0.0:
			host_lost_at = t
			_report("host_lost t=%.2f reason=\"%s\"" % [t, detail])
		elif connected_at < 0.0:
			_report("join_failed t=%.2f reason=\"%s\"" % [t, detail])


func _roster_names() -> String:
	var n: Array = []
	for id: int in Net.names:
		n.append("%d:%s" % [id, Net.names[id]])
	return ",".join(n)


func _make_sync() -> void:
	sync = NetSync.new()
	sync.name = "NetSync"
	add_child(sync)
	dummy = NetDummy.new()
	dummy.peer_id = Net.my_id()
	dummy.pilot_name = pilot
	add_child(dummy)
	sync.register_local(dummy)
	sync.player_spawned.connect(func(id: int, n: String) -> void:
		spawned.append(n)
		_log("player_spawned %d %s" % [id, n])
		streams[id] = {"name": n, "n": 0, "first": -1.0, "last": 0.0, "lat_sum": 0.0, "lat_max": 0.0, "lat_min": 99.0, "err_max": 0.0, "spawn": t, "gaps": 0.0})
	sync.player_left.connect(func(id: int) -> void:
		var n: String = streams.get(id, {}).get("name", str(id))
		left.append(n)
		_log("player_left %d %s" % [id, n])
		if streams.has(id):
			streams[id]["left"] = t)
	sync.remote_state.connect(_on_remote)
	sync.event_received.connect(func(n: String, d: Variant, from_id: int) -> void:
		events_rx[n] = int(events_rx.get(n, 0)) + 1
		if int(events_rx[n]) == 1:
			_log("event %s from %d data=%s" % [n, from_id, d]))


func _on_remote(id: int, st: Dictionary, sent_time: float) -> void:
	if not streams.has(id):
		return
	var s: Dictionary = streams[id]
	var now := sync.now()
	var lat := now - sent_time
	if s.first < 0.0:
		s.first = t
	elif t - s.last > s.gaps:
		s.gaps = t - s.last
	s.last = t
	s.n += 1
	s.lat_sum += lat
	s.lat_max = maxf(s.lat_max, lat)
	if t - float(s.first) > 3.0:
		s.lat_min = minf(s.lat_min, lat)
	if t - s.spawn > 4.0 and t - float(s.first) > 3.0:
		var expect := NetDummy.circle_pos(id, Net.net_time() - lat)
		var err: float = (st.p as Vector3).distance_to(expect)
		s.err_max = maxf(s.err_max, err)


func _on_rooms(rooms: Array) -> void:
	var want := str(args.get("host_name", ""))
	var found := false
	for r: Dictionary in rooms:
		if want != "" and r.host != want:
			continue
		found = true
		if not room_seen.has(r.id):
			room_seen[r.id] = t
			_report("room_seen t=%.2f id=%s host=%s players=%d/%d mission=%s version=%s version_ok=%s joinable=%s age_s=%d" % [t, r.id, r.host, r.players, r.max, r.mission, r.version, r.version_ok, r.joinable, r.age_s])
		if not joined and t >= _num("join_at", 2.0) and t >= float(room_seen[r.id]) + _num("join_delay", 0.0):
			joined = true
			if args.has("force"):
				Net._rooms.erase(r.id)
			_log("joining %s (players %d/%d)" % [r.id, r.players, r.max])
			Net.join(r.id, pilot)
		if close_seen_room == "":
			close_seen_room = r.id
	if not found and close_seen_room != "" and host_lost_at >= 0.0 and room_gone_at < 0.0:
		room_gone_at = t
		_report("room_gone_from_list t=%.2f (%.2f s after host left)" % [t, t - host_lost_at])


func _process(delta: float) -> void:
	t += delta
	if done:
		return
	if Net.is_online():
		clock_snap = [Net.clock_offset, Net.rtt, Net.clock_samples, Net.rtt_min]
	if sync and Net.is_host() and Net.is_online():
		ev_clock += delta
		if ev_clock > 2.0:
			ev_clock = 0.0
			ev_sent += 1
			sync.send_event("hb", {"n": ev_sent, "note": "host heartbeat event"})
	if args.has("leave_at") and t >= _num("leave_at", 0.0) and Net.is_online():
		_log("leaving")
		_finish_report()
		Net.leave()
		_quit_later(2.0)
	if args.has("quit_at") and t >= _num("quit_at", 0.0) and Net.is_online():
		_log("host quitting")
		_finish_report()
		Net.leave()
		_quit_later(2.0)
	if args.has("quit_on_left") and role == "host" and left.has(str(args.quit_on_left)) and not done:
		_log("%s left, quitting" % args.quit_on_left)
		_finish_report()
		Net.leave()
		_quit_later(2.0)
	if args.has("quit_when_alone") and role == "host" and not spawned.is_empty() and Net.names.size() <= 1 and not done:
		_log("everyone left, quitting")
		_finish_report()
		Net.leave()
		_quit_later(_num("quit_when_alone", 5.0))
	var linger := _num("linger", 8.0)
	if host_lost_at >= 0.0 and t > host_lost_at + linger:
		_finish_report()
		_quit_later(0.1)
	if t >= _num("run", 60.0):
		_finish_report()
		Net.leave()
		_quit_later(1.5)


func _quit_later(s: float) -> void:
	if done:
		return
	done = true
	get_tree().create_timer(s).timeout.connect(func() -> void: get_tree().quit())


var _reported := false


func _finish_report() -> void:
	if _reported:
		return
	_reported = true
	var ok := true
	var why: Array[String] = []
	var expect_fail := str(args.get("expect_fail", ""))
	if expect_fail != "":
		var hit := Net.last_error.to_lower().contains(expect_fail)
		_report("expected_failure=%s got=\"%s\" ok=%s" % [expect_fail, Net.last_error, hit])
		if not hit:
			ok = false
			why.append("expected %s failure" % expect_fail)
		_report("RESULT %s %s" % ["PASS" if ok else "FAIL", ", ".join(why)])
		return
	if role == "client" and connected_at < 0.0:
		ok = false
		why.append("never connected")
	var expect_peers := int(args.get("expect_peers", -1))
	if expect_peers >= 0 and spawned.size() < expect_peers:
		ok = false
		why.append("saw %d peers, expected %d" % [spawned.size(), expect_peers])
	_report("spawned=[%s] left=[%s] events=%s ev_sent=%d sent_packets=%d" % [",".join(spawned), ",".join(left), events_rx, ev_sent, sync.sent_packets if sync else 0])
	_report("clock offset_s=%.4f rtt_ms=%.1f rtt_min_ms=%.1f samples=%d fps=%.0f" % [clock_snap[0], clock_snap[1] * 1000.0, clock_snap[3] * 1000.0, clock_snap[2], Engine.get_frames_per_second()])
	for id: int in streams:
		var s: Dictionary = streams[id]
		var span: float = float(s.last) - float(s.first)
		var hz: float = (s.n - 1) / span if span > 0.5 else 0.0
		var lat_ms: float = s.lat_sum / maxf(s.n, 1) * 1000.0
		_report("stream from=%s n=%d hz=%.1f lat_mean_ms=%.1f lat_min_ms=%.1f lat_max_ms=%.1f max_gap_ms=%.0f pos_err_max_m=%.2f" % [s.name, s.n, hz, lat_ms, s.lat_min * 1000.0, s.lat_max * 1000.0, s.gaps * 1000.0, s.err_max])
		if span > 5.0:
			if hz < 25.0 or hz > 35.0:
				ok = false
				why.append("%s rate %.1f Hz" % [s.name, hz])
			if lat_ms > _num("max_lat_ms", 150.0) or s.lat_min < -0.012:
				ok = false
				why.append("%s latency %.1f ms" % [s.name, lat_ms])
			if s.err_max > _num("max_err_m", 5.0):
				ok = false
				why.append("%s pos error %.2f m" % [s.name, s.err_max])
		elif not s.has("left"):
			ok = false
			why.append("%s stream too short (%.1f s)" % [s.name, span])
	if role == "client" and events_rx.get("hb", 0) == 0 and connected_at >= 0.0 and t - connected_at > 5.0:
		ok = false
		why.append("no host events")
	_report("RESULT %s %s" % ["PASS" if ok else "FAIL", ", ".join(why)])
