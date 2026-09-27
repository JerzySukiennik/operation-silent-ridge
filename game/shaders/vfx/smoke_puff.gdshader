// Billboard particle for explosion fire and smoke: soft noisy disc, COLOR from the particle ramp, CUSTOM.y = life fraction.
shader_type spatial;
render_mode cull_disabled, depth_draw_never, shadows_disabled, diffuse_lambert, specular_disabled;

uniform sampler2D noise_tex : repeat_enable, filter_linear_mipmap;
uniform float emissive = 0.0;
uniform float softness = 0.55;

varying float v_seed;
varying float v_life;

void vertex() {
	MODELVIEW_MATRIX = VIEW_MATRIX * mat4(INV_VIEW_MATRIX[0], INV_VIEW_MATRIX[1], INV_VIEW_MATRIX[2], MODEL_MATRIX[3]);
	float sc = length(MODEL_MATRIX[0].xyz);
	MODELVIEW_MATRIX = MODELVIEW_MATRIX * mat4(vec4(sc, 0.0, 0.0, 0.0), vec4(0.0, sc, 0.0, 0.0), vec4(0.0, 0.0, sc, 0.0), vec4(0.0, 0.0, 0.0, 1.0));
	MODELVIEW_NORMAL_MATRIX = mat3(MODELVIEW_MATRIX);
	v_seed = INSTANCE_CUSTOM.x;
	v_life = INSTANCE_CUSTOM.y;
}

void fragment() {
	vec2 c = UV * 2.0 - 1.0;
	float r = length(c);
	vec2 nuv = UV * 0.7 + vec2(v_seed * 3.1, v_seed * 1.7 + v_life * 0.25);
	float n = texture(noise_tex, nuv).r;
	float shape = smoothstep(1.0, 1.0 - softness, r + (n - 0.5) * 0.7);
	ALBEDO = COLOR.rgb * (0.75 + 0.5 * n);
	EMISSION = COLOR.rgb * emissive * (0.6 + 0.8 * n) * (1.0 - v_life);
	ALPHA = clamp(shape * COLOR.a, 0.0, 1.0);
	NORMAL = normalize(vec3(c * 0.6, 1.0));
}
