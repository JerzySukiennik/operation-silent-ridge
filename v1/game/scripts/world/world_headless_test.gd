# Headless World check (Mac): loads the heightfield, samples height_at along the canyon and at key points, prints REPORT lines, quits.
extends Node


func _ready() -> void:
	var t0 := Time.get_ticks_msec()
	var w: World = preload("res://scenes/world/world.tscn").instantiate()
	add_child(w)
	print("REPORT world_headless load_ms=%d visual=%s size=%d" % [Time.get_ticks_msec() - t0, w.visual, w.heights.size])
	var path := w.canyon_path()
	var min_floor := 1e9
	var max_floor := -1e9
	for p in path:
		var h := w.height_at(p.x, p.z)
		min_floor = minf(min_floor, h)
		max_floor = maxf(max_floor, h)
	print("REPORT canyon points=%d floor_min=%.1f floor_max=%.1f" % [path.size(), min_floor, max_floor])
	var t := w.target_position()
	print("REPORT target=%s h=%.1f carrier=%s surface=%.1f" % [t, w.height_at(t.x, t.z), w.carrier_transform().origin, w.surface_at(w.carrier_transform().origin.x, w.carrier_transform().origin.z)])
	for s in w.spawn_points():
		print("REPORT spawn %s agl=%.0f" % [s.origin, s.origin.y - w.surface_at(s.origin.x, s.origin.z)])
	var t1 := Time.get_ticks_usec()
	var acc := 0.0
	for i in 10000:
		acc += w.height_at(randf_range(-40000, 40000), randf_range(-40000, 40000))
	print("REPORT height_at_us=%.2f" % [(Time.get_ticks_usec() - t1) / 10000.0])
	get_tree().quit()
