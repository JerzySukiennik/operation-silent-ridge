# Headless tool: packs the raw uint16 heightfield from gen_terrain.py into a Godot Image resource (FORMAT_R16) loadable in exports.
extends SceneTree


func _init() -> void:
	var a := OS.get_cmdline_user_args()
	if a.size() < 3:
		push_error("usage: -- <height.r16> <size> <out.res>")
		quit(1)
		return
	var n := int(a[1])
	var data := FileAccess.get_file_as_bytes(a[0])
	if data.size() != n * n * 2:
		push_error("height.r16 size mismatch: %d vs %d" % [data.size(), n * n * 2])
		quit(1)
		return
	var img := Image.create_from_data(n, n, false, Image.FORMAT_R16, data)
	var err := ResourceSaver.save(img, a[2], ResourceSaver.FLAG_COMPRESS)
	print("pack_height: %dx%d -> %s (err %d)" % [n, n, a[2], err])
	quit(err)
