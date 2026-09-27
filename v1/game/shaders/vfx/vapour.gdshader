// Condensation vapour (wing sheets, LEX vortices, transonic cone): noise-scrolled soft white sheet; UV.y runs with the airflow.
shader_type spatial;
render_mode unshaded, cull_disabled, depth_draw_never, shadows_disabled;

uniform sampler2D noise_tex : repeat_enable, filter_linear_mipmap;
instance uniform float intensity = 0.0;
instance uniform float seed = 0.0;
uniform float scroll = 2.2;
uniform vec2 tiling = vec2(2.0, 3.0);
uniform float edge_soft = 0.35;
uniform float front_bias = 1.0;
uniform float fresnel_mode = 0.0;
uniform float coverage = 0.5;
uniform float opacity = 1.0;
uniform vec3 tint : source_color = vec3(0.93, 0.95, 0.98);

void fragment() {
	vec2 uv = UV * tiling + vec2(seed * 0.37, -TIME * scroll + seed);
	float n = texture(noise_tex, uv).r;
	float n2 = texture(noise_tex, uv * 2.3 + vec2(0.5, -TIME * scroll * 0.7)).r;
	float cloud = smoothstep(0.42 - coverage * 0.42, 0.85, n * 0.65 + n2 * 0.45);
	float across = smoothstep(0.0, edge_soft, UV.x) * smoothstep(1.0, 1.0 - edge_soft, UV.x);
	float along = pow(1.0 - UV.y, front_bias) * smoothstep(0.0, 0.06, UV.y);
	float facing = abs(dot(normalize(NORMAL), normalize(VIEW)));
	float rim = mix(1.0, pow(facing, 1.5), fresnel_mode);
	float a = intensity * opacity * cloud * across * along * rim * smoothstep(3.0, 12.0, length(VERTEX));
	ALBEDO = tint * (0.85 + 0.25 * n2);
	ALPHA = clamp(a, 0.0, 1.0);
}
