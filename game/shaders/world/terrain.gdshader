// Terrain clipmap: R16 heightfield displacement with CDLOD-style geomorphing, mask/slope/altitude-driven PBR layers, baked sun visibility + AO.
shader_type spatial;
render_mode cull_back, diffuse_lambert, specular_schlick_ggx;

#include "res://shaders/world/terrain_common.gdshaderinc"

uniform sampler2D masks_a : hint_default_white, filter_linear_mipmap, repeat_disable;   // AO, sun, flow, scree
uniform sampler2D masks_b : hint_default_black, filter_linear_mipmap, repeat_disable;   // forest, curvature, water, hardness
uniform sampler2D noise_tex : hint_default_black, filter_linear_mipmap, repeat_enable;

uniform sampler2D rock_a : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D rock_n : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D rockm_a : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D rockm_n : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D scree_a : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D scree_n : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D screem_a : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D snow_a : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D snow_n : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D snowm_a : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D ground_a : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D ground_n : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D floor_a : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D shore_a : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D shore_n : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D water_n : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;

uniform float morph_start = 104.0;   // grid cells from level centre where geomorphing starts
uniform float morph_len = 16.0;
uniform float shadow_drop = 0.0;     // shadow-caster copy is lowered so it never self-shadows the visible surface
uniform float snowline = 520.0;
uniform float treeline = 1550.0;
uniform float tree_far = 1400.0;      // beyond this the forest is drawn as canopy colour only
uniform vec3 rock_tint = vec3(0.5, 0.51, 0.54);
uniform vec3 canopy_color = vec3(0.028, 0.042, 0.034);
uniform float bounce = 0.28;
uniform vec3 water_color = vec3(0.035, 0.075, 0.07);
uniform bool debug_flat = false;

varying vec3 v_world;
varying float v_sun;
varying float v_rt;

// OpenGL normal map sampled with uv = world axes: image "up" is the negative world axis, so green is flipped.
vec3 unpack_n(vec4 t) {
	vec2 xy = t.xy * 2.0 - 1.0;
	return vec3(xy.x, -xy.y, sqrt(max(1.0 - dot(xy, xy), 0.0)));
}

// Reoriented-free "whiteout" blend of a tangent-space detail normal onto a world normal for a top-down projection.
vec3 blend_top(vec3 n, vec3 tn, float k) {
	return normalize(vec3(n.x + tn.x * k, n.y, n.z + tn.y * k));
}

void vertex() {
	vec2 g = VERTEX.xz;
	float s = MODEL_MATRIX[0][0];
	vec2 origin = MODEL_MATRIX[3].xz;
	float d = max(abs(g.x), abs(g.y));
	float morph = clamp((d - morph_start) / morph_len, 0.0, 1.0);
	vec2 gm = g - mod(g, 2.0) * morph;
	vec2 wxz = origin + gm * s;
	float lvl = round(log2(s / hm_cell)) + 1.0;
	int m0 = int(max(lvl - 2.0, 0.0));
	int m1 = int(max(lvl - 1.0, 0.0));
	float h = h_at(wxz, m0);
	if (morph > 0.0 && m1 != m0) {
		h = mix(h, h_at(wxz, m1), morph);
	}
	h -= shadow_drop;
	VERTEX = vec3(gm.x, h, gm.y);
	v_world = vec3(wxz.x, h, wxz.y);
}

vec4 tri_sample(sampler2D t, vec3 p, vec3 w, float sc) {
	vec4 c = vec4(0.0);
	if (w.x > 0.02) c += texture(t, p.zy * sc) * w.x;
	if (w.y > 0.02) c += texture(t, p.xz * sc) * w.y;
	if (w.z > 0.02) c += texture(t, p.xy * sc) * w.z;
	return c;
}

vec3 tri_normal(sampler2D t, vec3 p, vec3 n, vec3 w, float sc) {
	// whiteout-blended triplanar normal mapping (Golus 2017)
	vec3 r = vec3(0.0);
	if (w.x > 0.02) { vec3 tn = unpack_n(texture(t, p.zy * sc)); tn = vec3(tn.xy + n.zy, abs(tn.z) * n.x); r += tn.zyx * w.x; }
	if (w.y > 0.02) { vec3 tn = unpack_n(texture(t, p.xz * sc)); tn = vec3(tn.xy + n.xz, abs(tn.z) * n.y); r += tn.xzy * w.y; }
	if (w.z > 0.02) { vec3 tn = unpack_n(texture(t, p.xy * sc)); tn = vec3(tn.xy + n.xy, abs(tn.z) * n.z); r += tn * w.z; }
	return normalize(r);
}

float hblend(float w, float h) {
	return w * (0.35 + h);
}

void fragment() {
	vec3 wp = v_world;
	vec3 cam = CAMERA_POSITION_WORLD;
	float dist = length(wp - cam);
	vec2 uv = world_uv(wp.xz);

	// per-pixel normal from the heightfield; the sample spacing grows with distance to avoid shimmering
	float e = hm_cell * max(1.0, dist / 900.0);
	float lod = log2(e / hm_cell);
	vec3 n = hm_normal(wp.xz, e, lod);
	vec3 nm = hm_normal(wp.xz, max(e, 40.0), log2(max(e, 40.0) / hm_cell));  // macro normal for material decisions

	vec4 ma = texture(masks_a, uv);
	vec4 mb = texture(masks_b, uv);
	vec4 nz = texture(noise_tex, wp.xz / 1900.0);
	vec4 nz2 = texture(noise_tex, wp.xz / 230.0);
	vec4 nz3 = texture(noise_tex, wp.xz / 37.0);

	float slope = 1.0 - nm.y;
	float slope_d = 1.0 - n.y;
	float alt = wp.y;
	float curv = mb.g - 0.5;
	float near = 1.0 - smoothstep(120.0, 700.0, dist);
	float mid = 1.0 - smoothstep(2500.0, 9000.0, dist);

	// ---------------- layer weights
	float wet = ma.b;
	float gully = smoothstep(0.03, 0.14, curv) * smoothstep(0.35, 0.75, wet + curv);
	float rock_w = smoothstep(0.28, 0.48, mix(slope, slope_d, 0.5) + (nz2.r - 0.5) * 0.16 - curv * 0.3 + mb.a * 0.1);
	float sline = snowline + (nz.r - 0.5) * 420.0 + (nz2.g - 0.5) * 110.0 - curv * 400.0 - nm.z * 90.0;
	float snow_alt = smoothstep(sline - 100.0, sline + 220.0, alt);
	// snow sticks up to ~50 deg, in gullies and couloirs up to ~70 deg; wind strips convex ridges
	float hold = 0.34 + gully * 0.3 - max(-curv, 0.0) * 0.5 + (nz3.a - 0.5) * 0.12 + (nz2.b - 0.5) * 0.08;
	float snow_w = snow_alt * smoothstep(hold + 0.08, hold - 0.1, slope_d);
	float scree_w = ma.a * smoothstep(0.08, 0.35, slope) * (1.0 - rock_w) * smoothstep(treeline - 700.0, treeline - 200.0, alt + nz.g * 300.0);
	scree_w = max(scree_w, smoothstep(treeline + 100.0, treeline + 500.0, alt + nz.b * 400.0) * (1.0 - rock_w) * 0.5);
	float shore_w = smoothstep(7.0, 1.5, alt + nz2.b * 5.0) * (1.0 - rock_w * 0.6);
	float forest = mb.r * smoothstep(treeline + 80.0, treeline - 200.0, alt);
	float water = mb.b;

	// ---------------- albedo
	vec3 aw = pow(abs(n), vec3(6.0));
	aw /= (aw.x + aw.y + aw.z);
	// rock: two triplanar scales of the cliff texture (grey granite tint), aerial rock on the macro scale
	// jointed granite slabs (aerial rock scan, triplanar at two scales) with a close-range cliff detail texture
	vec4 rm = tri_sample(rockm_a, wp, aw, 1.0 / 46.0);
	vec4 rm2 = tri_sample(rockm_a, wp + vec3(311.0, 0.0, 127.0), aw, 1.0 / 173.0);
	vec4 rc = tri_sample(rock_a, wp, aw, 1.0 / 9.0);
	vec3 slab = mix(rm.rgb, rm2.rgb, 0.4 + 0.3 * (1.0 - mid));
	float sl = dot(slab, vec3(0.33));
	vec3 rock_col = mix(vec3(sl), slab, 0.3) * vec3(1.0, 1.0, 1.04);   // winter: moss is dull, rock reads grey
	rock_col = mix(rock_col, rock_col * rc.rgb * 2.2, near * 0.5);
	rock_col *= rock_tint * 1.9 * (0.8 + 0.4 * nz2.a) * (0.88 + 0.24 * nz.a);
	vec4 rc2 = rm2;
	// big walls: water/lichen streaks running down the fall line and broad tonal bands (strata)
	float streak = texture(noise_tex, vec2(dot(wp.xz, vec2(0.7071)) / 55.0, wp.y / 900.0)).g;
	float streak2 = texture(noise_tex, vec2(dot(wp.xz, vec2(-0.7071, 0.7071)) / 38.0, wp.y / 700.0)).a;
	float band = texture(noise_tex, vec2(wp.y / 260.0, dot(wp.xz, vec2(0.6, 0.8)) / 4000.0)).b;
	float wall = 1.0 - aw.y;
	rock_col *= mix(1.0, (0.65 + 0.7 * streak * streak2) * (0.8 + 0.4 * band), wall);
	rock_col = mix(rock_col, rock_col * vec3(0.85, 0.83, 0.78), smoothstep(0.55, 0.8, band) * wall);
	// jointed cliff structure: irregular horizontal ledges + vertical joints (normal/shade only, the heightfield is 10 m)
	float cliff = smoothstep(0.3, 0.55, slope_d) * rock_w * smoothstep(0.35, 0.6, texture(noise_tex, wp.xz / 310.0 + 0.37).g);
	float yy = wp.y + (texture(noise_tex, wp.xz / 400.0).r - 0.5) * 30.0 + (nz3.g - 0.5) * 5.0;
	float per = 19.0 + 14.0 * nz.b;
	float lf = fract(yy / per);
	float ledge_top = smoothstep(0.80, 0.9, lf) * (1.0 - smoothstep(0.96, 1.0, lf));
	float under = (1.0 - smoothstep(0.0, 0.18, lf));
	vec2 along = normalize(vec2(-n.z, n.x) + 1e-5);
	float jf = fract(dot(wp.xz, along) / (13.0 + 9.0 * nz2.g) + nz3.r * 0.6);
	float joint = smoothstep(0.0, 0.06, jf) * (1.0 - smoothstep(0.9, 1.0, jf));
	rock_col *= mix(1.0, (1.0 - 0.25 * under) * mix(0.82, 1.0, joint), cliff);
	float rock_h = rm.a * 0.6 + rc2.a * 0.4;
	// scree / talus
	vec4 sc = texture(scree_a, wp.xz / 3.0);
	vec4 scm = texture(screem_a, wp.xz / 85.0 + nz.rg * 0.2);
	vec3 scree_col = mix(vec3(dot(scm.rgb, vec3(0.33))) * vec3(0.9, 0.9, 0.93), sc.rgb, near * 0.5) * (0.8 + 0.35 * nz3.r);
	// snow: mostly flat bright white with soft macro variation (wind crust, sastrugi)
	vec4 sn = texture(snow_a, wp.xz / 5.0);
	vec4 snm = texture(snowm_a, wp.xz / 160.0 + nz.ba * 0.25);
	vec3 snow_col = vec3(0.88, 0.9, 0.93) * (0.92 + 0.12 * dot(snm.rgb, vec3(0.33)) + 0.05 * (nz3.b - 0.5));
	// ground below the snowline: dark mossy forest floor and meadow (anti-tiled by a second rotated sample)
	vec4 gr = texture(ground_a, wp.xz / 26.0);
	vec4 gr2 = texture(ground_a, mat2(vec2(0.8, 0.6), vec2(-0.6, 0.8)) * wp.xz / 61.0);
	vec3 gcol = mix(gr.rgb, gr2.rgb, smoothstep(0.35, 0.65, nz2.r));
	float gl = dot(gcol, vec3(0.33));
	vec3 ground_col = mix(vec3(gl), gcol, 0.4) * vec3(0.62, 0.66, 0.55);
	vec4 fl = texture(floor_a, wp.xz / 3.5);
	ground_col = mix(ground_col, fl.rgb * 0.6, smoothstep(0.1, 0.6, mb.r) * 0.6 * near);
	ground_col *= 0.75 + 0.4 * nz2.b;
	// shore
	vec4 sh = texture(shore_a, wp.xz / 18.0);
	vec3 shore_col = sh.rgb * mix(0.5, 0.95, smoothstep(0.0, 2.5, alt));

	// height-aware blending, back to front
	vec3 col = ground_col;
	float wr = clamp(hblend(rock_w, rock_h) * 1.4, 0.0, 1.0);
	col = mix(col, scree_col, clamp(hblend(scree_w, sc.a) * 1.4, 0.0, 1.0));
	col = mix(col, rock_col, wr);
	// snow fills texture cavities first (low rock height) and thins out on rock highs
	float sw = clamp(snow_w * 1.5 - (1.0 - snow_w) * rock_h * 0.8 * rock_w + (sn.a - 0.5) * 0.25 * near, 0.0, 1.0);

	col = mix(col, snow_col, sw);
	col = mix(col, shore_col, shore_w * (1.0 - sw));
	// distant forest: dark crowns with snow between them replace the ground where trees are no longer instanced
	float canopy = forest * smoothstep(tree_far * 0.75, tree_far, dist);
	float crowns = smoothstep(0.42, 0.58, texture(noise_tex, wp.xz / 8.0).r * 0.7 + nz3.g * 0.3 + (forest - 0.5) * 0.3);
	vec3 can = mix(snow_col * 0.75 * snow_alt + ground_col * (1.0 - snow_alt), canopy_color * (0.8 + 0.4 * nz3.g), clamp(crowns * 0.35 + 0.6, 0.0, 1.0));
	col = mix(col, can, canopy);
	// under near trees: darker ground, snow wells
	col *= 1.0 - forest * (1.0 - canopy) * 0.3;
	// river / lake
	col = mix(col, water_color, water);

	// normals
	vec3 nn = n;
	if (near > 0.0) {
		vec3 rn = normalize(tri_normal(rockm_n, wp, n, aw, 1.0 / 46.0) + tri_normal(rock_n, wp, n, aw, 1.0 / 9.0) * near * 0.6);
		vec3 gn = blend_top(n, unpack_n(texture(ground_n, wp.xz / 26.0)).xyz, 0.7);
		vec3 snn = blend_top(n, unpack_n(texture(snow_n, wp.xz / 5.0)).xyz, 0.25);
		vec3 scn = blend_top(n, unpack_n(texture(scree_n, wp.xz / 3.0)).xyz, 0.9);
		vec3 dn = mix(gn, scn, scree_w);
		dn = mix(dn, rn, wr);
		dn = mix(dn, snn, sw);
		nn = normalize(mix(n, dn, near));
		// ledge tops face up, joints and ledge undersides break the wall normal
		vec3 up = vec3(0.0, 1.0, 0.0);
		vec3 side = normalize(vec3(along.x, 0.0, along.y));
		nn = normalize(nn + cliff * (up * (ledge_top * 0.6 - under * 0.25) + side * (1.0 - joint) * 0.3 * sign(jf - 0.5)));
	} else {
		vec3 rn = tri_normal(rockm_n, wp, n, aw, 1.0 / 46.0);
		nn = normalize(mix(n, rn, wr * mid * 0.8 * (1.0 - sw)));

	}
	// rivers: flat surface with small moving ripples
	vec2 rp = texture(water_n, wp.xz / 9.0 + vec2(TIME * 0.06, TIME * 0.035)).rg * 2.0 - 1.0;
	vec2 rp2 = texture(water_n, wp.xz / 23.0 - vec2(TIME * 0.025, TIME * 0.05)).rg * 2.0 - 1.0;
	vec3 wn = normalize(vec3((rp.x + rp2.x) * 0.5, 1.0, -(rp.y + rp2.y) * 0.5));
	nn = normalize(mix(nn, wn, water));

	float rough = mix(0.93, 0.72, sw);
	rough = mix(rough, 0.1, water);
	if (debug_flat) { col = vec3(0.5); }
	ALBEDO = col;
	ROUGHNESS = rough;
	SPECULAR = mix(0.3, 0.5, max(water, sw));
	NORMAL = normalize((VIEW_MATRIX * vec4(nn, 0.0)).xyz);
	// light bounced off sunlit terrain into shadowed hollows (cheap GI stand-in)
	EMISSION = col * bounce * (1.0 - ma.r * 0.75) * (0.35 + 0.65 * (1.0 - ma.g));
	AO = mix(0.2, 1.0, ma.r);
	AO_LIGHT_AFFECT = 0.0;
	v_sun = ma.g;
	v_rt = 1.0 - smoothstep(500.0, 800.0, dist);
}

void light() {
	float ndl = clamp(dot(NORMAL, LIGHT), 0.0, 1.0);
	// realtime shadows only near the camera (trees, aircraft); terrain self-shadowing comes from the baked sun mask
	float vis = min(mix(1.0, ATTENUATION, v_rt), v_sun);
	DIFFUSE_LIGHT += ndl * vis * LIGHT_COLOR / PI;
	vec3 h = normalize(VIEW + LIGHT);
	float ndh = clamp(dot(NORMAL, h), 0.0, 1.0);
	float a = ROUGHNESS * ROUGHNESS;
	float a2 = a * a;
	float dd = ndh * ndh * (a2 - 1.0) + 1.0;
	float D = a2 / (PI * dd * dd);
	float ndv = clamp(dot(NORMAL, VIEW), 0.001, 1.0);
	float k = a * 0.5;
	float G = ndl / (ndl * (1.0 - k) + k) * ndv / (ndv * (1.0 - k) + k);
	float F = 0.04 + 0.96 * pow(1.0 - clamp(dot(LIGHT, h), 0.0, 1.0), 5.0);
	SPECULAR_LIGHT += D * G * F / (4.0 * ndv + 0.0001) * vis * LIGHT_COLOR / PI * SPECULAR_AMOUNT;
}
