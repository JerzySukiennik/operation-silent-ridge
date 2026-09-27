// Wingtip vortex ribbon: camera-facing strip, UV.x across, UV.y age (0 new .. 1 old); COLOR.a carries the emission-time intensity.
shader_type spatial;
render_mode unshaded, cull_disabled, depth_draw_never, shadows_disabled;

void fragment() {
	float across = 1.0 - abs(UV.x * 2.0 - 1.0);
	float core = pow(across, 1.8);
	float age = clamp(UV.y, 0.0, 1.0);
	float near_fade = smoothstep(7.0, 30.0, length(VERTEX));
	float a = COLOR.a * core * (1.0 - age) * (1.0 - age) * smoothstep(0.0, 0.03, age) * near_fade;
	ALBEDO = vec3(0.94, 0.96, 1.0);
	ALPHA = clamp(a * 0.8, 0.0, 1.0);
}
