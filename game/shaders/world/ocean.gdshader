// Ocean: clipmap grid with Gerstner swell (geomorphed ring edges), FFT detail normals, depth colour and shoreline foam from the heightfield.
shader_type spatial;
render_mode cull_back, specular_schlick_ggx;

#include "res://shaders/world/terrain_common.gdshaderinc"

uniform sampler2D wave_a : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D wave_b : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D noise_tex : hint_default_black, filter_linear_mipmap, repeat_enable;
uniform float morph_start = 50.0;
uniform float morph_len = 8.0;
uniform vec3 deep_color : source_color = vec3(0.006, 0.026, 0.05);
uniform vec3 shallow_color : source_color = vec3(0.03, 0.16, 0.17);
uniform vec3 sss_color : source_color = vec3(0.02, 0.2, 0.18);
uniform vec3 to_sun = vec3(-0.25, 0.64, 0.7);
uniform float wave_scale = 1.0;
// xy = direction, z = wavelength (m), w = steepness (0..1)
const vec4 WAVES[6] = vec4[](
	vec4(0.94, 0.34, 140.0, 0.10),
	vec4(0.80, 0.60, 83.0, 0.12),
	vec4(0.99, -0.12, 47.0, 0.14),
	vec4(0.60, 0.80, 29.0, 0.13),
	vec4(0.97, 0.24, 17.0, 0.12),
	vec4(0.30, 0.95, 11.0, 0.10)
);

varying vec3 v_world;
varying vec2 v_base;
varying float v_crest;

vec3 gerstner(vec2 p, float t, float fade, out vec3 nrm, out float crest) {
	vec3 off = vec3(0.0);
	vec3 n = vec3(0.0, 1.0, 0.0);
	crest = 0.0;
	for (int i = 0; i < 6; i++) {
		vec2 d = normalize(WAVES[i].xy);
		float k = 6.2831853 / WAVES[i].z;
		float c = sqrt(9.81 / k);
		float a = WAVES[i].w / k * wave_scale * fade;
		float f = k * (dot(d, p) - c * t);
		float cf = cos(f), sf = sin(f);
		off += vec3(d.x * a * cf, a * sf, d.y * a * cf);
		float wa = k * a;
		n.x -= d.x * wa * cf;
		n.z -= d.y * wa * cf;
		n.y -= WAVES[i].w * wa * sf * fade;
		crest += max(sf, 0.0) * WAVES[i].w * fade;
	}
	nrm = normalize(n);
	return off;
}

void vertex() {
	vec2 g = VERTEX.xz;
	float s = MODEL_MATRIX[0][0];
	vec2 origin = MODEL_MATRIX[3].xz;
	float d = max(abs(g.x), abs(g.y));
	float morph = clamp((d - morph_start) / morph_len, 0.0, 1.0);
	vec2 gm = g - mod(g, 2.0) * morph;
	vec2 wxz = origin + gm * s;
	float dist = length(vec3(wxz.x, 0.0, wxz.y) - CAMERA_POSITION_WORLD);
	// waves die out in the shallows so the swell never climbs onto the beach or up a river mouth
	float shoal = smoothstep(-0.3, -10.0, h_at(wxz, 0));
	float fade = (1.0 - smoothstep(2500.0, 7000.0, dist)) * shoal;
	vec3 n;
	float crest;
	vec3 off = gerstner(wxz, TIME, fade, n, crest);
	VERTEX = vec3(gm.x + off.x / s, off.y, gm.y + off.z / s);
	v_base = wxz;
	v_world = vec3(wxz.x + off.x, off.y, wxz.y + off.z);
	v_crest = crest;
}

vec3 unpack_n(vec4 t) {
	vec2 xy = t.xy * 2.0 - 1.0;
	return vec3(xy.x, sqrt(max(1.0 - dot(xy, xy), 0.0)), -xy.y);
}

void fragment() {
	vec3 cam = CAMERA_POSITION_WORLD;
	vec3 wp = v_world;
	float dist = length(wp - cam);
	float ground = h_filtered(wp.xz, 0.0);
	float fade = (1.0 - smoothstep(2500.0, 7000.0, dist)) * smoothstep(-0.3, -10.0, ground);
	vec3 n;
	float crest;
	gerstner(v_base, TIME, fade, n, crest);
	// detail normals: two FFT tiles scrolling along the wind, faded out with distance (roughness takes over)
	float dfade = 1.0 - smoothstep(300.0, 6000.0, dist);
	vec3 da = unpack_n(texture(wave_a, wp.xz / 64.0 + vec2(TIME * 0.018, TIME * 0.006)));
	vec3 db = unpack_n(texture(wave_b, wp.xz / 12.0 + vec2(-TIME * 0.011, TIME * 0.024)));
	vec3 dn = normalize(vec3(da.x + db.x * 0.6, 1.0, da.z + db.z * 0.6));
	n = normalize(vec3(n.x + dn.x * dfade, n.y, n.z + dn.z * dfade));

	// water depth from the terrain heightfield
	float depth = max(-ground, 0.0);
	vec3 col = mix(shallow_color, deep_color, 1.0 - exp(-depth / 14.0));
	// scattering through wave crests facing away from the sun
	vec3 v = normalize(cam - wp);
	float sss = pow(clamp(dot(v, -normalize(vec3(to_sun.x, 0.0, to_sun.z))), 0.0, 1.0), 3.0) * clamp(v_crest * 3.0, 0.0, 1.0);
	col += sss_color * sss * 0.35 * fade;
	// shoreline foam: surf band that pulses with the swell, broken up by noise
	vec4 nz = texture(noise_tex, wp.xz / 45.0 + vec2(TIME * 0.01, 0.0));
	vec4 nz2 = texture(noise_tex, wp.xz / 9.0 - vec2(0.0, TIME * 0.02));
	float surf = smoothstep(3.5, 0.2, depth + (nz.r - 0.5) * 3.0);
	float pulse = 0.5 + 0.5 * sin(TIME * 1.1 - depth * 1.6 + nz.g * 6.0);
	float lace = smoothstep(0.5, 0.72, nz2.b * 0.7 + texture(noise_tex, wp.xz / 3.1 + vec2(TIME * 0.03)).r * 0.3 + surf * 0.15);
	float foam = surf * mix(0.35, 1.0, pulse) * lace;
	// wind-driven whitecaps on the steepest crests
	foam += smoothstep(0.55, 0.9, v_crest + nz2.r * 0.25) * 0.35 * fade;
	foam = clamp(foam, 0.0, 1.0);
	col = mix(col, vec3(0.82, 0.86, 0.88), foam);

	ALBEDO = col;
	ROUGHNESS = mix(mix(0.035, 0.12, smoothstep(200.0, 12000.0, dist)), 0.7, foam);
	METALLIC = 0.0;
	SPECULAR = 0.5;
	NORMAL = normalize((VIEW_MATRIX * vec4(n, 0.0)).xyz);
}
