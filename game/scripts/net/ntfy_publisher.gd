# ntfy publish queue: two HTTP workers, strict order per topic, one retry on transient failure, reports quota (429) and reachability.
class_name NtfyPublisher
extends Node

signal published(topic: String, ok: bool, code: int)
signal quota_exceeded

const WORKERS := 2
const MAX_BODY := 3900

var base := "https://ntfy.sh"
var sent_count := 0
var _queue: Array = []
var _workers: Array[HTTPRequest] = []
var _busy_topic := {}
var _items := {}


func _ready() -> void:
	for i in WORKERS:
		var w := HTTPRequest.new()
		w.timeout = 10.0
		add_child(w)
		w.request_completed.connect(_on_done.bind(w))
		_workers.append(w)


func publish(topic: String, data: Dictionary) -> void:
	var body := JSON.stringify(data)
	if body.length() > MAX_BODY:
		push_warning("ntfy body %d B over limit for %s" % [body.length(), topic])
	_queue.append({"topic": topic, "body": body, "tries": 0, "at": 0.0})


func clear() -> void:
	_queue.clear()


func pending() -> int:
	return _queue.size() + _busy_topic.size()


func _process(_delta: float) -> void:
	if _queue.is_empty():
		return
	for w in _workers:
		if _items.has(w):
			continue
		var idx := -1
		var now := Time.get_ticks_msec() / 1000.0
		for i in _queue.size():
			if not _busy_topic.has(_queue[i].topic) and float(_queue[i].at) <= now:
				idx = i
				break
		if idx < 0:
			return
		var item: Dictionary = _queue[idx]
		_queue.remove_at(idx)
		_items[w] = item
		_busy_topic[item.topic] = true
		var err := w.request("%s/%s" % [base, item.topic], ["Content-Type: text/plain"], HTTPClient.METHOD_POST, item.body)
		if err != OK:
			_on_done(HTTPRequest.RESULT_CANT_CONNECT, 0, PackedStringArray(), PackedByteArray(), w)


func _on_done(result: int, code: int, _h: PackedStringArray, _b: PackedByteArray, w: HTTPRequest) -> void:
	var item: Dictionary = _items[w]
	_items.erase(w)
	_busy_topic.erase(item.topic)
	var ok := result == HTTPRequest.RESULT_SUCCESS and code >= 200 and code < 300
	if ok:
		sent_count += 1
	elif code == 429:
		quota_exceeded.emit()
	elif int(item.tries) < 1:
		item.tries = int(item.tries) + 1
		item.at = Time.get_ticks_msec() / 1000.0 + 1.0
		_queue.push_front(item)
		return
	published.emit(item.topic, ok, code)
