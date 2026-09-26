# Long-lived ntfy.sh JSON subscription over HTTPClient; emits each message payload as a Dictionary and reconnects on drop.
class_name NtfyStream
extends Node

signal message(data: Dictionary)

var _client := HTTPClient.new()
var _host := ""
var _topic := ""
var _buffer := ""
var _active := false
var _requested := false
var _since := ""
var _retry := 0.0


func start(host: String, topic: String) -> void:
	_host = host
	_topic = topic
	_since = str(int(Time.get_unix_time_from_system()) - 5)
	_active = true
	_connect()


func stop() -> void:
	_active = false
	_client.close()


func _connect() -> void:
	_client.close()
	_buffer = ""
	_requested = false
	var err := _client.connect_to_host(_host, 443, TLSOptions.client())
	if err != OK:
		_retry = 2.0


func _process(delta: float) -> void:
	if not _active:
		return
	if _retry > 0.0:
		_retry -= delta
		if _retry <= 0.0:
			_connect()
		return
	_client.poll()
	match _client.get_status():
		HTTPClient.STATUS_CONNECTED:
			if not _requested:
				_requested = true
				_client.request(HTTPClient.METHOD_GET, "/%s/json?since=%s" % [_topic, _since], ["Accept: application/x-ndjson"])
		HTTPClient.STATUS_BODY:
			var chunk := _client.read_response_body_chunk()
			if chunk.size() > 0:
				_buffer += chunk.get_string_from_utf8()
				_drain()
		HTTPClient.STATUS_DISCONNECTED, HTTPClient.STATUS_CONNECTION_ERROR, HTTPClient.STATUS_TLS_HANDSHAKE_ERROR, HTTPClient.STATUS_CANT_CONNECT, HTTPClient.STATUS_CANT_RESOLVE:
			_retry = 1.5
		_:
			if _requested and _client.get_status() == HTTPClient.STATUS_REQUESTING:
				pass


func _drain() -> void:
	while true:
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
		if o.get("event", "") != "message":
			continue
		if o.has("time"):
			_since = str(int(o.time))
		if o.has("id"):
			_since = str(o.id)
		var inner: Variant = JSON.parse_string(str(o.get("message", "")))
		if typeof(inner) == TYPE_DICTIONARY:
			message.emit(inner)
