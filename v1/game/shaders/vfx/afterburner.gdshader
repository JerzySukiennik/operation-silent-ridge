// Afterburner plume: additive soft cone with flicker and shock diamonds; UV.y runs 0 at the nozzle to 1 at the tip.
shader_type spatial;
render_mode blend_add, depth_draw_never, cull_disabled, unshaded, shadows_disabled, fog_disabled;

instance uniform float ab = 0.0;
instance uniform float power = 0.0;
instance uniform float seed = 0.0;
uniform float layer = 0.0;
uniform vec3 core_color : source_color = vec3(1.0, 0.7, 0.38);
uniform vec3 outer_color : source_color = vec3(1.0, 0.5, 0.16);
uniform vec3 blue_color : source_color = vec3(0.55, 0.45, 1.0);
uniform vec3 diamond_color : source_color = vec3(1.0, 0.86, 0.62);
uniform float diamonds = 4.5;
uniform float brightness = 3.0;

varying float v_along;

void vertex() {
	v_along = UV.y;
}

void fragment() {
	float t = clamp(v_along, 0.0, 1.0);
	float facing = abs(dot(normalize(NORMAL), normalize(VIEW)));
	float soft = pow(facing, mix(0.9, 1.8, layer));
	float tm = TIME + seed * 17.0;
	float flick = 0.82 + 0.1 * sin(tm * 57.0 + t * 23.0) + 0.08 * sin(tm * 91.0 - t * 41.0);
	float fade = pow(1.0 - t, mix(1.4, 2.2, layer)) * smoothstep(0.0, 0.04, t);
	float d = pow(0.5 + 0.5 * cos((t * diamonds - 0.2) * 6.2831853), 6.0) * smoothstep(0.85, 0.1, t) * (1.0 - layer);
	vec3 base = mix(outer_color, core_color, smoothstep(0.55, 0.0, t) * (1.0 - layer * 0.6));
	base = mix(base, blue_color, smoothstep(0.12, 0.0, t) * 0.6);
	vec3 col = base * fade + diamond_color * d * 2.6;
	float k = ab * flick * soft * brightness;
	ALBEDO = col * k;
	ALPHA = 1.0;
}
