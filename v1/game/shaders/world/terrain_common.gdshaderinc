// Shared heightfield sampling for the terrain, ocean and vegetation shaders (must match HeightField.height_at on the CPU).
uniform sampler2D height_map : filter_linear_mipmap, repeat_disable;
uniform int hm_size = 8192;
uniform float hm_cell = 9.765625;
uniform float hm_min = -1024.0;
uniform float hm_range = 5120.0;
uniform float map_half = 40000.0;

int mirror_i(int i, int n) {
	int p = 2 * (n - 1);
	int m = i - p * int(floor(float(i) / float(p)));
	return m <= n - 1 ? m : p - m;
}

float texel_h(ivec2 t, int mip, int n) {
	return texelFetch(height_map, ivec2(mirror_i(t.x, n), mirror_i(t.y, n)), mip).r;
}

// bilinear height at a world position from mip level `mip` (mip 0 == CPU height_at)
float h_at(vec2 wxz, int mip) {
	float sc = float(1 << mip);
	vec2 u = (wxz + map_half) / hm_cell;
	u = (u + 0.5) / sc - 0.5;
	vec2 f = floor(u);
	ivec2 i = ivec2(f);
	vec2 t = u - f;
	int n = hm_size >> mip;
	float a = texel_h(i, mip, n);
	float b = texel_h(i + ivec2(1, 0), mip, n);
	float c = texel_h(i + ivec2(0, 1), mip, n);
	float d = texel_h(i + ivec2(1, 1), mip, n);
	return hm_min + hm_range * mix(mix(a, b, t.x), mix(c, d, t.x), t.y);
}

vec2 world_uv(vec2 wxz) {
	return ((wxz + map_half) / hm_cell + 0.5) / float(hm_size);
}

float h_filtered(vec2 wxz, float lod) {
	return hm_min + hm_range * textureLod(height_map, world_uv(wxz), lod).r;
}

vec3 hm_normal(vec2 wxz, float e, float lod) {
	float l = h_filtered(wxz - vec2(e, 0.0), lod);
	float r = h_filtered(wxz + vec2(e, 0.0), lod);
	float d = h_filtered(wxz - vec2(0.0, e), lod);
	float u = h_filtered(wxz + vec2(0.0, e), lod);
	return normalize(vec3(l - r, 2.0 * e, d - u));
}
