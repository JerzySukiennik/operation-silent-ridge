// Near conifers: streamed MultiMesh tiles (placement mirrors tree_at() on the CPU); dithered fade into the billboard ring.
shader_type spatial;
render_mode cull_disabled, diffuse_lambert_wrap, specular_disabled;

#include "res://shaders/world/tree_common.gdshaderinc"

uniform sampler2D albedo_tex : source_color, filter_linear_mipmap_anisotropic, repeat_disable;
uniform sampler2D normal_tex : hint_normal, filter_linear_mipmap, repeat_disable;
uniform bool is_bark = false;
uniform float fade_start = 400.0;
uniform float fade_end = 460.0;
uniform vec3 foliage_tint : source_color = vec3(0.5, 0.6, 0.53);
uniform float alpha_cut = 0.45;

varying float v_fade;
varying float v_snow;
varying float v_sun;
varying vec3 v_wn;

void vertex() {
	// instance transforms come from the CPU (forest.gd mirrors tree_at()); MODEL_MATRIX includes them
	vec3 base = MODEL_MATRIX[3].xyz;
	float d = length(base.xz - focus.xz);
	v_fade = 1.0 - smoothstep(fade_start, fade_end, d);
	if (v_fade <= 0.0) {
		VERTEX = vec3(0.0);
	}
	v_wn = normalize((MODEL_MATRIX * vec4(NORMAL, 0.0)).xyz);
	v_snow = smoothstep(snowline - 250.0, snowline + 250.0, base.y);
	v_sun = texture(sun_mask, world_uv(base.xz)).g;
}

void fragment() {
	vec4 c = texture(albedo_tex, UV);
	float fade_ok = step(dither(FRAGCOORD.xy), v_fade);
	vec3 col = c.rgb;
	if (!is_bark) {
		col *= foliage_tint * COLOR.rgb;
		col = mix(vec3(dot(col, vec3(0.33))), col, 0.75) * 0.9;  // match the impostors' tone at the LOD switch
		// snow resting on the upper side of the sprays
		float s = v_snow * smoothstep(0.6, 0.95, v_wn.y) * 0.15;
		col = mix(col, vec3(0.78, 0.8, 0.84), s);
		ALPHA = c.a * fade_ok;
		ALPHA_SCISSOR_THRESHOLD = alpha_cut;
	} else {
		col *= 0.55;
		ALPHA = fade_ok;
		ALPHA_SCISSOR_THRESHOLD = 0.5;
	}
	ALBEDO = col;
	ROUGHNESS = 0.9;
	BACKLIGHT = is_bark ? vec3(0.0) : col * 0.35;
	AO = mix(0.6, 1.0, v_sun);
	AO_LIGHT_AFFECT = 0.0;
}
