// Mid-distance conifers: camera-facing impostors (4 side views + top view atlas rendered from the mesh tree), same placement as tree_mesh.
shader_type spatial;
render_mode cull_disabled, diffuse_lambert_wrap, specular_disabled;

#include "res://shaders/world/tree_common.gdshaderinc"

uniform sampler2D atlas_albedo : source_color, filter_linear_mipmap, repeat_disable;
uniform sampler2D atlas_normal : hint_normal, filter_linear_mipmap, repeat_disable;
uniform float fade_in_start = 400.0;
uniform float fade_in_end = 460.0;
uniform float far_start = 1300.0;
uniform float far_end = 1550.0;
uniform vec3 foliage_tint : source_color = vec3(0.5, 0.6, 0.53);
// source render framing (see make_tree_blender.py)
const float SIDE_H = 27.6;
const float SIDE_BOTTOM = -0.8;
const float SIDE_W = 10.35;
const float TOP_W = 9.2;

varying float v_fade;
varying vec2 v_uv;
varying float v_top;
varying vec3 v_right;
varying vec3 v_up;
varying vec3 v_fwd;
varying float v_snow;
varying float v_sun;

void vertex() {
	vec4 rnd;
	vec4 t = tree_at(instance_cell(INSTANCE_ID, MODEL_MATRIX[3].xyz), rnd);
	float d = length(t.xz - focus.xz);
	v_fade = smoothstep(fade_in_start, fade_in_end, d) * (1.0 - smoothstep(far_start, far_end, d));
	vec3 cam = CAMERA_POSITION_WORLD;
	vec3 to_cam = cam - (t.xyz + vec3(0.0, 12.0 * t.w, 0.0));
	float horiz = length(to_cam.xz);
	float elev = atan(to_cam.y, horiz);
	v_top = step(0.95, elev);
	if (t.w <= 0.0 || v_fade <= 0.0) {
		VERTEX = vec3(0.0);
	} else {
		vec3 fwd = normalize(vec3(to_cam.x, 0.0, to_cam.z));
		vec3 right = normalize(cross(vec3(0.0, 1.0, 0.0), fwd));
		vec3 up = vec3(0.0, 1.0, 0.0);
		vec3 p;
		// pick one of four side renders by the viewing azimuth relative to the tree's random rotation
		float az = atan(fwd.z, fwd.x) / 6.2831853 + rnd.x;
		float view = floor(fract(az) * 4.0);
		if (v_top > 0.5) {
			// looking down steeply: horizontal quad at crown height with the top render
			vec3 r2 = vec3(1.0, 0.0, 0.0);
			vec3 f2 = vec3(0.0, 0.0, -1.0);  // image top = -Z (Blender +Y)
			p = t.xyz + vec3(0.0, 14.0 * t.w, 0.0) + (r2 * VERTEX.x + f2 * (VERTEX.y - 0.5)) * TOP_W * t.w;
			v_uv = vec2(0.75, 0.0) + vec2(UV.x * 0.25, UV.y * 0.5);
			right = r2; up = f2; fwd = vec3(0.0, 1.0, 0.0);
		} else {
			p = t.xyz + right * VERTEX.x * SIDE_W * t.w + up * (SIDE_BOTTOM + VERTEX.y * SIDE_H) * t.w;
			v_uv = vec2((view + UV.x) * 0.1875, UV.y);
		}
		v_right = right; v_up = up; v_fwd = fwd;
		VERTEX = p - MODEL_MATRIX[3].xyz;
		NORMAL = fwd;
		v_snow = smoothstep(snowline - 250.0, snowline + 250.0, t.y);
		v_sun = texture(sun_mask, world_uv(t.xz)).g;
	}
}

void fragment() {
	vec4 c = texture(atlas_albedo, v_uv);
	if (c.a < 0.5 || dither(FRAGCOORD.xy) > v_fade) {
		discard;
	}
	vec2 nxy = texture(atlas_normal, v_uv).rg * 2.0 - 1.0;
	float nz = sqrt(max(1.0 - dot(nxy, nxy), 0.0));
	vec3 wn = normalize(v_right * nxy.x + v_up * nxy.y + v_fwd * nz);
	vec3 col = c.rgb * foliage_tint * vec3(0.55, 0.7, 0.58);
	col = mix(col, vec3(0.78, 0.8, 0.84), v_snow * smoothstep(0.6, 0.95, wn.y) * 0.15);
	ALBEDO = col;
	NORMAL = normalize((VIEW_MATRIX * vec4(wn, 0.0)).xyz);
	ROUGHNESS = 0.9;
	BACKLIGHT = col * 0.2;
	AO = mix(0.6, 1.0, v_sun);
	AO_LIGHT_AFFECT = 0.0;
}
