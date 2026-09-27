// Nozzle glow disc: hot orange ring at MIL, white-yellow core in afterburner; additive.
shader_type spatial;
render_mode blend_add, depth_draw_never, cull_disabled, unshaded, shadows_disabled, fog_disabled;

instance uniform float ab = 0.0;
instance uniform float power = 0.0;

void fragment() {
	vec2 c = UV * 2.0 - 1.0;
	float r = length(c);
	float disc = smoothstep(1.0, 0.75, r);
	float ring = smoothstep(0.55, 0.85, r) * disc;
	vec3 mil = vec3(1.0, 0.3, 0.07) * (0.04 + 0.6 * power * power * power) * (0.25 + 0.75 * ring);
	vec3 aug = vec3(1.0, 0.8, 0.55) * ab * 6.0 * disc;
	ALBEDO = mil * disc + aug;
	ALPHA = 1.0;
}
