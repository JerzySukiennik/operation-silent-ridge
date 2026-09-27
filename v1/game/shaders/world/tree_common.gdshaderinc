// Shared GPU tree placement: a jittered world grid, thinned by the baked forest-density mask, identical for mesh trees and billboards.
#include "res://shaders/world/terrain_common.gdshaderinc"

uniform sampler2D forest_mask : hint_default_black, filter_linear, repeat_disable;  // masks_b (R = density)
uniform sampler2D sun_mask : hint_default_white, filter_linear, repeat_disable;     // masks_a (G = sun visibility)
uniform float cell_size = 7.0;
uniform int tile_cells = 32;
uniform float treeline = 1550.0;
uniform float snowline = 950.0;
uniform vec3 focus = vec3(0.0);
uniform float tree_height = 26.0;   // height of the source model (m)

uint thash(uint x) {
	x ^= x >> 16u; x *= 0x7feb352du; x ^= x >> 15u; x *= 0x846ca68bu; x ^= x >> 16u;
	return x;
}

uint cell_hash(ivec2 c) {
	return thash(uint(c.x) * 0x8da6b343u ^ thash(uint(c.y) * 0xd8163841u + 0x9e3779b9u));
}

float h01(uint h) {
	return float(h & 0xffffffu) / 16777215.0;
}

// xyz = base position, w = scale (0 = no tree); rnd = random numbers for rotation/variant
vec4 tree_at(ivec2 c, out vec4 rnd) {
	uint h = cell_hash(c);
	uint h2 = thash(h + 1u);
	uint h3 = thash(h + 2u);
	rnd = vec4(h01(h2), h01(h3), h01(thash(h + 3u)), h01(thash(h + 4u)));
	vec2 jit = vec2(h01(h), h01(thash(h + 5u))) - 0.5;
	vec2 p = (vec2(c) + 0.5 + jit * 0.85) * cell_size;
	float dens = texture(forest_mask, world_uv(p)).r;
	float y = h_at(p, 0);
	dens *= smoothstep(treeline + 60.0, treeline - 140.0, y) * step(3.0, y);
	float keep = step(rnd.w, dens * 1.15);
	// trees get shorter towards the treeline
	float sc = (0.55 + 0.55 * rnd.z) * mix(0.55, 1.0, smoothstep(treeline + 50.0, treeline - 500.0, y));
	return vec4(p.x, y - 0.4, p.y, sc * keep);
}

ivec2 instance_cell(int id, vec3 tile_origin) {
	ivec2 base = ivec2(round(tile_origin.xz / cell_size));
	return base + ivec2(id % tile_cells, id / tile_cells);
}

float dither(vec2 frag) {
	return fract(52.9829189 * fract(dot(frag, vec2(0.06711056, 0.00583715))));
}
