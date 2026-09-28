// Ocean v2 shading (Godot space inside). Swell normal from the shared 8-wave Gerstner set (ocean_waves.inc, identical to the WPO),
// 4 detail normal layers at non-integer scale ratios (4.3 / 17.9 / 71 / 293 m), each rotated and scrolled differently and faded
// with distance, a kilometre-scale macro field (wind patches / calm slicks) modulating waves, roughness, colour and whitecaps,
// roughness rising with distance (broad sun-glitter path), depth colour from the baked bathymetry, crest scattering, whitecaps
// only on steep crests in windy patches, breaking bands + surf foam on exposed shores.
// Inputs: P (cm), Cam (cm), T (s), WaveA, WaveB (normal maps), Noise, Ocean (R depth, G shore distance, B swell exposure).
// Outputs: return base colour; NormalWS, Rough, Spec.
#define S2(T_, uv) Texture2DSample(T_, T_##Sampler, (uv))
#define UNPN(t) float3((t).x * 2.0 - 1.0, -((t).y * 2.0 - 1.0), sqrt(saturate(1.0 - dot((t).xy * 2.0 - 1.0, (t).xy * 2.0 - 1.0))))
#define ROT(uv, a) float2((uv).x * cos(a) - (uv).y * sin(a), (uv).x * sin(a) + (uv).y * cos(a))
float3 wp = float3(P.x, P.z, P.y) * 0.01; float3 cam = float3(Cam.x, Cam.z, Cam.y) * 0.01;
float dist = length(wp - cam);
float4 oc = S2(Ocean, (wp.xz + 40000.0) / 80000.0);
float depth = oc.r * oc.r * 255.0;
float shore = oc.g * oc.g * 1500.0;
float expo = oc.b;
float macro = S2(Noise, wp.xz / 3710.0 + 0.13).g * 0.65 + S2(Noise, wp.xz / 1270.0 + 0.61).b * 0.35;
macro = smoothstep(0.25, 0.75, macro);
float amp = smoothstep(0.5, 12.0, depth) * (0.12 + 0.88 * expo) * (1.0 - smoothstep(4000.0, 9000.0, length(wp.xz - cam.xz)));
float wind = lerp(0.35, 1.2, macro) * (0.3 + 0.7 * expo);                       // local wind strength
#include "/OSR/ocean_waves.inc"
// detail layers: (scale m, rotation, drift m/s, fade distance m)
float2 d = 0;
float3 t;
t = UNPN(S2(WaveA, ROT(wp.xz, 0.31) / 4.3 + float2(0.83, 0.21) * T / 4.3));    d += ROT(t.xy, -0.31) * 0.55 * (1.0 - smoothstep(150.0, 500.0, dist));
t = UNPN(S2(WaveB, ROT(wp.xz, 1.73) / 17.9 + float2(1.37, -0.52) * T / 17.9)); d += ROT(t.xy, -1.73) * 0.5 * (1.0 - smoothstep(600.0, 2200.0, dist));
t = UNPN(S2(WaveA, ROT(wp.xz, 2.87) / 71.0 + float2(2.61, 0.94) * T / 71.0));  d += ROT(t.xy, -2.87) * 0.42 * (1.0 - smoothstep(2500.0, 9000.0, dist));
t = UNPN(S2(WaveB, ROT(wp.xz, 4.41) / 293.0 + float2(4.9, -2.2) * T / 293.0));  d += ROT(t.xy, -4.41) * 0.3 * (1.0 - smoothstep(9000.0, 30000.0, dist));
d *= wind * lerp(0.6, 1.0, amp);
float3 nn = normalize(float3(gn.x + d.x * 0.3, gn.y, gn.z + d.y * 0.3));
// breaking waves on exposed shores: bands parallel to the shore moving in
float4 nzs = S2(Noise, wp.xz / 61.0 + T * 0.0023);
float sp = shore / 31.0 + T * 0.19 + nzs.r * 0.7;
float brk = smoothstep(0.8, 0.97, frac(sp)) * smoothstep(90.0, 15.0, shore) * smoothstep(0.0, 6.0, shore) * smoothstep(0.15, 0.6, expo);
float surf = smoothstep(lerp(4.0, 12.0, expo), 1.5, shore + (nzs.g - 0.5) * 8.0) * (0.35 + 0.65 * expo);
float foam = saturate(max(brk * (0.5 + nzs.b), surf) * (0.55 + 0.45 * S2(Noise, wp.xz / 7.3).g));
// whitecaps: only on steep crests, only where the wind patch is strong, broken up by noise
float wc = smoothstep(0.42, 0.7, crest) * smoothstep(0.55, 0.95, macro) * smoothstep(0.5, 0.75, S2(Noise, ROT(wp.xz, 0.7) / 23.0 + T * 0.004).a) * amp * expo;
foam = max(foam, wc * 0.8);
// colour: deep blue offshore, teal over the shelf, green-sand over shallows (fake transparency); crest scattering; slick tint
float3 deep = float3(0.0035, 0.016, 0.031);
float3 shelf = float3(0.006, 0.03, 0.04);
float3 shal = float3(0.03, 0.062, 0.058);
float3 col = lerp(shelf, deep, smoothstep(15.0, 120.0, depth));
col = lerp(shal, col, smoothstep(0.5, 8.0, depth));
col += float3(0.005, 0.022, 0.02) * saturate(crest * 2.0) * amp;
col *= lerp(1.06, 0.95, macro);
col = lerp(col, float3(0.8, 0.82, 0.83), foam);
nn = normalize(lerp(nn, float3(0, 1, 0), foam * 0.6));
NormalWS = float3(nn.x, nn.z, nn.y);
// distance widens the glitter path (sub-pixel waves); calm slicks stay glassy
Rough = lerp(lerp(0.025, 0.11, smoothstep(400.0, 20000.0, dist)) * lerp(0.6, 1.2, macro), 0.75, foam);
Spec = lerp(0.5, 0.3, foam);
return col;
