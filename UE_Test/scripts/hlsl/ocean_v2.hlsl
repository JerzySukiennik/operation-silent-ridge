// Ocean v2 shading (Godot space inside): analytic Gerstner normal + two scrolling detail normal maps, depth colour from the
// baked bathymetry (fake transparency over shallows), breaking shore waves moving towards the coast + surf foam band.
// Inputs: P (abs world pos cm), Cam (cm), T (s), WaveA, WaveB (normal maps), Noise, Ocean (R depth sqrt 0..255 m, G shore distance sqrt 0..1500 m).
// Outputs: return base colour; NormalWS, Rough, Spec.
#define S2(T_, uv) Texture2DSample(T_, T_##Sampler, (uv))
#define UNPN(t) float3((t).x * 2.0 - 1.0, -((t).y * 2.0 - 1.0), sqrt(saturate(1.0 - dot((t).xy * 2.0 - 1.0, (t).xy * 2.0 - 1.0))))
float3 wp = float3(P.x, P.z, P.y) * 0.01; float3 cam = float3(Cam.x, Cam.z, Cam.y) * 0.01;
float dist = length(wp - cam);
float2 uv = (wp.xz + 40000.0) / 80000.0;
float4 oc = S2(Ocean, uv);
float depth = oc.r * oc.r * 255.0;
float shore = oc.g * oc.g * 1500.0;
float expo = 0.12 + 0.88 * oc.b;                                   // swell exposure (fjords and bays sheltered)
float amp = smoothstep(0.5, 12.0, depth) * expo * (1.0 - smoothstep(4000.0, 9000.0, length(wp.xz - cam.xz)));
float3 gn = float3(0, 1, 0);
const float4 W[5] = { float4(0.99, 0.14, 120.0, 0.85), float4(0.95, -0.31, 74.0, 0.5), float4(0.83, 0.55, 43.0, 0.28), float4(0.99, -0.12, 23.0, 0.14), float4(0.72, -0.69, 12.5, 0.06) };
float crest = 0;
[unroll] for (int i = 0; i < 5; i++) {
	float2 dir = normalize(W[i].xy); float k = 6.2831853 / W[i].z; float w = sqrt(9.81 * k);
	float ph = k * dot(dir, wp.xz) - w * T;
	float Q = 0.55 / (k * W[i].w * 5.0);
	float wa = k * W[i].w * amp;
	gn.xz -= dir * wa * cos(ph);
	gn.y -= Q * wa * sin(ph);
	crest += saturate(sin(ph)) * W[i].w;
}
gn = normalize(gn);
float fade = 1.0 - smoothstep(300.0, 6000.0, dist);
float3 a = UNPN(S2(WaveA, wp.xz / 9.0 + float2(0.021, 0.013) * T));
float3 b = UNPN(S2(WaveB, wp.xz / 33.0 - float2(0.009, 0.017) * T));
float3 c = UNPN(S2(WaveB, wp.xz / 140.0 + float2(0.004, -0.002) * T));
float2 det = (a.xy * 0.45 * fade + b.xy * 0.5 * (0.4 + 0.6 * fade) + c.xy * 0.35);
float3 nn = normalize(float3(gn.x + det.x * 0.35, gn.y, gn.z + det.y * 0.35));
// breaking waves: lines parallel to the shore travelling in, strongest in the surf zone
float4 nzs = S2(Noise, wp.xz / 60.0 + T * 0.002);
float sp = shore / 32.0 + T * 0.19 + nzs.r * 0.6;
float brk = smoothstep(0.8, 0.97, frac(sp)) * smoothstep(90.0, 15.0, shore) * smoothstep(0.0, 6.0, shore) * smoothstep(0.15, 0.6, oc.b);
float surf = smoothstep(lerp(4.0, 12.0, oc.b), 1.5, shore + (nzs.g - 0.5) * 8.0) * (0.35 + 0.65 * oc.b);   // thin wash line in fjords
float foam = saturate(max(brk * (0.5 + nzs.b), surf) * (0.55 + 0.45 * S2(Noise, wp.xz / 7.0).g));
foam = max(foam, smoothstep(1.1, 1.6, crest) * 0.25 * amp * smoothstep(0.55, 0.8, nzs.a));      // occasional whitecaps
// colour by depth: dark blue ocean, teal over the shelf, green-sand over shallows (fake transparency)
float3 deep = float3(0.0035, 0.017, 0.032);
float3 shelf = float3(0.007, 0.034, 0.042);
float3 shal = float3(0.03, 0.062, 0.058);
float3 col = lerp(shelf, deep, smoothstep(15.0, 120.0, depth));
col = lerp(shal, col, smoothstep(0.5, 8.0, depth));
col += float3(0.004, 0.02, 0.018) * saturate(crest - 0.6) * amp;                                // light through wave crests
col = lerp(col, float3(0.8, 0.82, 0.83), foam);
nn = normalize(lerp(nn, float3(0, 1, 0), foam * 0.6));
NormalWS = float3(nn.x, nn.z, nn.y);
Rough = lerp(lerp(0.035, 0.1, smoothstep(1500.0, 15000.0, dist)), 0.75, foam);
Spec = lerp(0.5, 0.3, foam);
return col;
