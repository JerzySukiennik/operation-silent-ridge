// Ocean v2 vertex displacement: 5 Gerstner waves (swell from the WSW), faded in shallow water and far from the camera.
// Inputs: P (abs world pos cm, without offsets), Cam (cm), T (s), Ocean (R depth sqrt-encoded 0..255 m, G shore distance). Returns WPO (cm).
float3 wp = float3(P.x, P.z, P.y) * 0.01; float3 cam = float3(Cam.x, Cam.z, Cam.y) * 0.01;
float2 uv = (wp.xz + 40000.0) / 80000.0;
float4 oc = Texture2DSampleLevel(Ocean, OceanSampler, uv, 0);
float depth = oc.r * oc.r * 255.0;
float expo = 0.12 + 0.88 * oc.b;                                   // swell exposure (fjords and bays sheltered)
float amp = smoothstep(0.5, 12.0, depth) * expo * (1.0 - smoothstep(4000.0, 9000.0, length(wp.xz - cam.xz)));
float3 d = 0;
const float4 W[5] = { float4(0.99, 0.14, 120.0, 0.85), float4(0.95, -0.31, 74.0, 0.5), float4(0.83, 0.55, 43.0, 0.28), float4(0.99, -0.12, 23.0, 0.14), float4(0.72, -0.69, 12.5, 0.06) };
[unroll] for (int i = 0; i < 5; i++) {
	float2 dir = normalize(W[i].xy); float k = 6.2831853 / W[i].z; float w = sqrt(9.81 * k);
	float ph = k * dot(dir, wp.xz) - w * T;
	float Q = 0.55 / (k * W[i].w * 5.0);
	d.xz += Q * W[i].w * dir * cos(ph);
	d.y += W[i].w * sin(ph);
}
d *= amp;
return float3(d.x, d.z, d.y) * 100.0;
