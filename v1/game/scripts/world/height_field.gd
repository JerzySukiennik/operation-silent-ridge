# CPU copy of the terrain heightfield: mirrored-edge bilinear sampling = h_at() in terrain_common.gdshaderinc, plus the exact L0 clipmap surface (works headless).
class_name HeightField
extends RefCounted

const PATH := "res://assets/world/terrain/height.res"

var size := 0
var cell := 1.0
var half := 0.0
var hmin := 0.0
var hscale := 0.0
var image: Image
var _data: PackedByteArray


func load_default() -> bool:
	if not ResourceLoader.exists(PATH):
		push_error("HeightField: missing %s (run tools/terrain/gen_terrain.py)" % PATH)
		return false
	var img: Image = load(PATH)
	if img == null or img.get_format() != Image.FORMAT_R16:
		push_error("HeightField: %s is not an R16 image" % PATH)
		return false
	setup(img)
	return true


func setup(img: Image) -> void:
	image = img
	size = img.get_width()
	half = WorldLayout.MAP_SIZE * 0.5
	cell = WorldLayout.MAP_SIZE / float(size)
	hmin = WorldLayout.HEIGHT_MIN
	hscale = (WorldLayout.HEIGHT_MAX - WorldLayout.HEIGHT_MIN) / 65535.0
	_data = img.get_data()


func _mirror(i: int) -> int:
	var p := 2 * (size - 1)
	var m := posmod(i, p)
	return m if m <= size - 1 else p - m


func texel(i: int, j: int) -> float:
	return hmin + float(_data.decode_u16((_mirror(j) * size + _mirror(i)) * 2)) * hscale


## Height of the rendered surface near the focus: the finest clipmap ring is a half-texel triangle grid
## (diagonal (0,0)-(1,1)) whose vertices sample the bilinear field, so this matches it to float precision.
func height(x: float, z: float) -> float:
	if size == 0:
		return 0.0
	var u := (x + half) / cell * 2.0
	var v := (z + half) / cell * 2.0
	var gx := floori(u)
	var gz := floori(v)
	var fu := u - gx
	var fv := v - gz
	var p00 := _half_vertex(gx, gz)
	var p11 := _half_vertex(gx + 1, gz + 1)
	if fu >= fv:
		var p10 := _half_vertex(gx + 1, gz)
		return p00 + (p10 - p00) * fu + (p11 - p10) * fv
	var p01 := _half_vertex(gx, gz + 1)
	return p00 + (p01 - p00) * fv + (p11 - p01) * fu


# bilinear value at a half-texel grid vertex: a texel, an edge midpoint or a cell centre
func _half_vertex(i2: int, j2: int) -> float:
	var i := i2 >> 1
	var j := j2 >> 1
	if i2 & 1 == 0:
		if j2 & 1 == 0:
			return texel(i, j)
		return (texel(i, j) + texel(i, j + 1)) * 0.5
	if j2 & 1 == 0:
		return (texel(i, j) + texel(i + 1, j)) * 0.5
	return (texel(i, j) + texel(i + 1, j) + texel(i, j + 1) + texel(i + 1, j + 1)) * 0.25


## Smooth bilinear heightfield (same as the shader's h_at at mip 0); cheaper, for LOS/radar sweeps.
func height_bilinear(x: float, z: float) -> float:
	if size == 0:
		return 0.0
	var u := (x + half) / cell
	var v := (z + half) / cell
	var i := floori(u)
	var j := floori(v)
	var fu := u - i
	var fv := v - j
	var a := texel(i, j)
	var b := texel(i + 1, j)
	var c := texel(i, j + 1)
	var d := texel(i + 1, j + 1)
	return lerpf(lerpf(a, b, fu), lerpf(c, d, fu), fv)


func normal(x: float, z: float, e := -1.0) -> Vector3:
	if e <= 0.0:
		e = cell
	var l := height_bilinear(x - e, z)
	var r := height_bilinear(x + e, z)
	var dn := height_bilinear(x, z - e)
	var up := height_bilinear(x, z + e)
	return Vector3(l - r, 2.0 * e, dn - up).normalized()
