// Heat haze behind the nozzle: refracts the already-rendered opaque scene with scrolling ripples, strongest along the axis.
shader_type spatial;
render_mode unshaded, cull_disabled, depth_draw_never, shadows_disabled, fog_disabled;

uniform sampler2D screen_tex : hint_screen_texture, filter_linear_mipmap;
instance uniform float strength = 0.0;
varying float v_along;

void vertex() {
	v_along = UV.y;
}

void fragment() {
	float facing = pow(abs(dot(normalize(NORMAL), normalize(VIEW))), 2.0);
	float fade = smoothstep(1.0, 0.2, v_along) * smoothstep(0.0, 0.06, v_along);
	float a = UV.x * 6.2831853;
	vec2 n = vec2(sin(v_along * 38.0 - TIME * 26.0 + a * 3.0), cos(v_along * 29.0 - TIME * 21.0 + a * 5.0));
	vec2 off = n * 0.006 * strength * facing * fade;
	ALBEDO = textureLod(screen_tex, SCREEN_UV + off, 0.0).rgb;
	ALPHA = clamp(strength * fade * facing * 2.5, 0.0, 1.0);
}
