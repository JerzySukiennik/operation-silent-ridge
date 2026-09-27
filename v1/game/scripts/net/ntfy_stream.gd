# Long-lived ntfy JSON subscription over HTTPClient: emits each message payload, reconnects with backoff, resumes from the last id, detects stale sockets.
class_name NtfyStream
extends Node

signal message(data: Dictionary)
signal opened
signal closed

# ntfy.sh sends a keepalive event every 45 s; silence well beyond that means a half-open socket.
const STALE_AFTER := 75.0
const MAX_BACKOFF := 10.0

var topic := ""
var fail_count := 0
var _client := HTTPClient.new()
var _host := ""
var _port := 443
var _tls := true
var _buffer := ""
var _active := false
var _requested := false
var _open := false
var _since := ""
var _retry := 0.0
var _backoff := 1.0
var _silence := 0.0


func start(base: String, p_topic: String, since := "") -> void:
	_tls = base.begins_with("https://")
	var hostport := base.trim_prefix("https://").trim_prefix("http://").trim_suffix("/")
	_port = 443 if _tls else 80
	var colon := hostport.rfind(":")
	if colon > 0:
		_port = int(hostport.substr(colon + 1))
		hostport = hostport.substr(0, colon)
	_host = hostport
	topic = p_topic
	_since = since if since != "" else str(int(Time.get_unix_time_from_system()) - 5)
	_active = true
	_backoff = 1.0
	_connect()


func stop() -> void:
	_active = false
	_client.close()
	_set_open(false)


func is_open() -> bool:
	return _open


func _connect() -> void:
	_client.close()
	_buffer = ""
	_requested = false
	_silence = 0.0
	var err := _client.connect_to_host(_host, _port, TLSOptions.client() if _tls else null)
	if err != OK:
		_fail()


func _fail() -> void:
	fail_count += 1
	_client.close()
	_set_open(false)
	_retry = _backoff
	_backoff = minf(_backoff * 2.0, MAX_BACKOFF)


func _set_open(v: bool) -> void:
	if v == _open:
		return
	_open = v
	if v:
		opened.emit()
	else:
		closed.emit()


func _process(delta: float) -> void:
	if not _active:
		return
	if _retry > 0.0:
		_retry -= delta
		if _retry <= 0.0:
			_connect()
		return
	_client.poll()
	_silence += delta
	match _client.get_status():
		HTTPClient.STATUS_CONNECTED:
			if not _requested:
				_requested = true
				_client.request(HTTPClient.METHOD_GET, "/%s/json?since=%s" % [topic, _since.uri_encode()], ["Accept: application/x-ndjson"])
			else:
				_fail()
				return
		HTTPClient.STATUS_BODY:
			if _client.get_response_code() != 200:
				_fail()
				return
			var chunk := _client.read_response_body_chunk()
			if chunk.size() > 0:
				_silence = 0.0
				_buffer += chunk.get_string_from_utf8()
				_drain()
		HTTPClient.STATUS_DISCONNECTED, HTTPClient.STATUS_CONNECTION_ERROR, HTTPClient.STATUS_TLS_HANDSHAKE_ERROR, HTTPClient.STATUS_CANT_CONNECT, HTTPClient.STATUS_CANT_RESOLVE:
			_fail()
			return
	if _silence > STALE_AFTER or (not _open and _silence > 12.0):
		_fail()


func _drain() -> void:
	while _active:
		var nl := _buffer.find("\n")
		if nl < 0:
			return
		var line := _buffer.substr(0, nl).strip_edges()
		_buffer = _buffer.substr(nl + 1)
		if line == "":
			continue
		var outer: Variant = JSON.parse_string(line)
		if typeof(outer) != TYPE_DICTIONARY:
			continue
		var o: Dictionary = outer
		var ev := str(o.get("event", ""))
		if ev == "open":
			_backoff = 1.0
			fail_count = 0
			_set_open(true)
			continue
		if ev != "message":
			continue
		if o.has("id"):
			_since = str(o.id)
		var inner: Variant = JSON.parse_string(str(o.get("message", "")))
		if typeof(inner) == TYPE_DICTIONARY:
			message.emit(inner)
