// Terrain v2 shading (scripts/terrain_v2/masks_ue.py masks; Godot space inside: x east, y up, z south, metres).
// Inputs: P (abs world pos cm), N (world normal), Cam (cm), TreeFar (m), BounceGain, SunLux, AOStrength,
//   masks MAg/MBg/MCg (80 km, 19.5 m), MAc/MBc/MCc (core 40x30 km, 9.77 m), MDc (core: R tree zone, G lake), Bake (80 km: RGB bounce, A sky),
//   RockM/RockMN (aerial rock scan, macro), RockF/RockFN (rock face), Rock/RockN (cliff close-up), Grav/GravN (rocky ground),
//   ScreeM (aerial scree), Snow/SnowN, SnowM, Ground/GroundN, Floor, Sand/SandN, Noise.
// Outputs: return = base colour; NormalWS, Rough, AOut (sky visibility), Spec, Emis (baked one-bounce GI).
#define S2(T, uv) Texture2DSample(T, T##Sampler, (uv))
#define UNPN(t) float3((t).x * 2.0 - 1.0, -((t).y * 2.0 - 1.0), sqrt(saturate(1.0 - dot((t).xy * 2.0 - 1.0, (t).xy * 2.0 - 1.0))))
#define ROT(uv, a) float2((uv).x * cos(a) - (uv).y * sin(a), (uv).x * sin(a) + (uv).y * cos(a))
// triplanar colour with a different rotation per projection (kills tiling repeats), weights w
#define TRI(T, p, w, sc, a) (S2(T, ROT((p).zy * (sc), a)) * (w).x + S2(T, ROT((p).xz * (sc), a + 1.3)) * (w).y + S2(T, ROT((p).xy * (sc), a + 2.1)) * (w).z)
// whiteout triplanar normal (tangent normals rotated back with their UV rotation), result in world space
#define TRIN(T, p, w, sc, a, out) { float3 t_; float2 r_; \
	t_ = UNPN(S2(T, ROT((p).zy * (sc), a))); r_ = ROT(t_.xy, -(a)); out = float3(0, 0, 0); \
	out += float3(abs(t_.z) * n.x, r_.y + n.y, r_.x + n.z) * (w).x; \
	t_ = UNPN(S2(T, ROT((p).xz * (sc), a + 1.3))); r_ = ROT(t_.xy, -(a + 1.3)); \
	out += float3(r_.x + n.x, abs(t_.z) * n.y, r_.y + n.z) * (w).y; \
	t_ = UNPN(S2(T, ROT((p).xy * (sc), a + 2.1))); r_ = ROT(t_.xy, -(a + 2.1)); \
	out += float3(r_.x + n.x, r_.y + n.y, abs(t_.z) * n.z) * (w).z; out = normalize(out); }
#define LUM(c) dot((c), float3(0.3, 0.59, 0.11))

float3 wp = float3(P.x, P.z, P.y) * 0.01;
float3 cam = float3(Cam.x, Cam.z, Cam.y) * 0.01;
float3 n = normalize(float3(N.x, N.z, N.y));
float dist = length(wp - cam);
float near = 1.0 - smoothstep(150.0, 900.0, dist);
float mid = 1.0 - smoothstep(2500.0, 9000.0, dist);

// ---- masks: refined core inside the core box (300 m blend at its border), 19.5 m world elsewhere
float2 uvg = (wp.xz + 40000.0) / 80000.0;
float2 uvc = (wp.xz - float2(-25000.0, -15000.0)) / float2(40000.0, 30000.0);
float cw = saturate(min(min(uvc.x, 1.0 - uvc.x) * 40000.0, min(uvc.y, 1.0 - uvc.y) * 30000.0) / 300.0);
float4 mA = lerp(S2(MAg, uvg), S2(MAc, uvc), cw);
float4 mB = lerp(S2(MBg, uvg), S2(MBc, uvc), cw);
float4 mC = lerp(S2(MCg, uvg), S2(MCc, uvc), cw);
float4 mD = S2(MDc, uvc) * cw;
float4 bk = S2(Bake, uvg);
float4 nz = S2(Noise, wp.xz / 1900.0);
float4 nz2 = S2(Noise, wp.xz / 230.0 + 0.31);
float4 nz3 = S2(Noise, wp.xz / 41.0 + 0.57);

float slope = 1.0 - n.y;
float snowM = mA.r, forest = mA.g, wet = mA.b, scree = mA.a;
float beach = mB.r, meadow = mB.g, aval = mB.b, hard = mB.a;
float treezone = mD.r, lake = mD.g;

// ---- rock (full triplanar at 3 scales, rotated per scale; no fall-line streak textures)
float3 aw = pow(abs(n), 5.0); aw /= (aw.x + aw.y + aw.z);
float4 rm = TRI(RockM, wp, aw, 1.0 / 52.0, 0.4);
float4 rm2 = TRI(RockM, wp + float3(311.0, 17.0, 127.0), aw, 1.0 / 191.0, 1.9);
float4 rf = TRI(RockF, wp, aw, 1.0 / 13.0, 2.7);
float4 rc = TRI(Rock, wp, aw, 1.0 / 4.1, 0.9);
float rl = LUM(rm.rgb) * 0.55 + LUM(rm2.rgb) * 0.45;
float rfl = LUM(rf.rgb);
float rlum = rl * lerp(1.0, rfl * 2.2, 0.55 * mid) * lerp(1.0, LUM(rc.rgb) * 2.1, 0.45 * near);
// granite/gneiss greys with warm oxidised and cool lichen patches (large-scale noise, not streaks)
float3 rtint = lerp(float3(0.30, 0.30, 0.315), float3(0.33, 0.30, 0.27), smoothstep(0.45, 0.75, nz2.g) * 0.8);
rtint = lerp(rtint, float3(0.22, 0.235, 0.23), smoothstep(0.55, 0.8, nz.b) * 0.6);
float3 rock_col = rtint * rlum * 3.1 * (0.85 + 0.3 * nz3.r);
// relief contrast from the scans' height channels: fractures and recesses darker, proud slabs lighter (reads as rock, not render)
rock_col *= lerp(0.78, 1.1, saturate(rm.a * 1.25)) * lerp(1.0, lerp(0.62, 1.12, saturate(rf.a * 1.3)), mid);
// rock bodies: large darker (amphibolite/gneiss) and lighter (granite) masses
rock_col *= lerp(0.8, 1.15, smoothstep(0.3, 0.7, S2(Noise, wp.xz / 3100.0 + 0.77).g));
// strata: same bed geometry as the carved ledges (phase offset from the mask + elevation); hard beds lighter, partings darker
float bph = atan2(mC.g - 0.5, mC.r - 0.5) / 6.2831853;
float thick = 34.0 + 16.0 * (nz.a - 0.5) * 0.4;
float bed = frac(wp.y / thick + bph + (nz3.b - 0.5) * 0.12 + (nz2.a - 0.5) * 0.08);
float hardbed = smoothstep(0.30, 0.42, bed) * smoothstep(0.95, 0.82, bed);
rock_col *= lerp(0.84, 1.08, hardbed) * lerp(float3(1.0, 1.0, 1.0), float3(1.03, 1.0, 0.95), 1.0 - hardbed);

// ---- scree / gravel
float4 gv = S2(Grav, ROT(wp.xz / 5.3, 0.7));
float4 scm = S2(ScreeM, ROT(wp.xz / 88.0, 2.2) + nz.rg * 0.15);
float3 scree_col = float3(0.31, 0.305, 0.30) * lerp(LUM(scm.rgb) * 2.2, LUM(gv.rgb) * 2.0, 0.5 * near) * (0.85 + 0.3 * nz3.g);

// ---- snow
float4 sn = S2(Snow, ROT(wp.xz / 6.0, 1.1));
float4 snm = S2(SnowM, ROT(wp.xz / 170.0, 0.3) + nz.ba * 0.2);
float3 snow_col = float3(0.86, 0.88, 0.91) * (0.93 + 0.1 * LUM(snm.rgb) + 0.04 * (nz3.a - 0.5));

// ---- vegetation / soil
float4 gr = S2(Ground, ROT(wp.xz / 27.0, 0.2));
float4 gr2 = S2(Ground, ROT(wp.xz / 71.0, 1.7));
float gl = lerp(LUM(gr.rgb), LUM(gr2.rgb), smoothstep(0.35, 0.65, nz2.r));
float3 meadow_col = lerp(float3(0.085, 0.10, 0.045), float3(0.12, 0.115, 0.07), nz2.b) * gl * 3.0;   // alpine grass / heather
float3 aval_col = float3(0.075, 0.11, 0.04) * (0.8 + 0.4 * nz3.r);                   // slide alder / avalanche chutes
float3 soil_col = lerp(float3(0.07, 0.08, 0.045), float3(0.11, 0.10, 0.075), smoothstep(0.4, 0.7, nz2.g)) * gl * 3.0;   // shrubs, grass and bare soil
float4 fl = S2(Floor, ROT(wp.xz / 3.7, 0.5));
float3 floor_col = float3(0.028, 0.036, 0.024) * lerp(1.0, 0.7 + 0.6 * LUM(fl.rgb) * 2.0, near);   // mossy understorey in canopy shade
float crowns = smoothstep(0.35, 0.65, S2(Noise, wp.xz / 9.0).r * 0.6 + nz3.g * 0.4);
float3 canopy_col = lerp(float3(0.018, 0.028, 0.02), float3(0.035, 0.05, 0.034), crowns) * (0.85 + 0.3 * nz2.g);
float4 sd = S2(Sand, ROT(wp.xz / 4.5, 0.1));
float3 sand_col = lerp(float3(0.13, 0.13, 0.125), float3(0.22, 0.215, 0.2), smoothstep(0.5, 3.0, wp.y)) * LUM(sd.rgb) * 3.0;   // grey glacial gravel / dark sand
float3 water_col = lerp(float3(0.022, 0.04, 0.042), float3(0.03, 0.075, 0.075), lake) * (0.85 + 0.3 * nz3.b);   // silty glacial water

// ---- weights
float rockT = lerp(0.17, 0.26, saturate(forest * 1.5)) - 0.05 * hard + (nz2.r - 0.5) * 0.09 + (nz3.g - 0.5) * 0.05;
float rock_w = smoothstep(rockT - 0.05, rockT + 0.05, slope);
float scree_w = saturate(scree * 1.4) * (1.0 - rock_w);
float3 col = soil_col;
col = lerp(col, meadow_col, saturate(meadow * 1.3));
col = lerp(col, aval_col, saturate(aval * 1.2) * (1.0 - scree_w));
col = lerp(col, floor_col, saturate(forest * 1.6));
col = lerp(col, scree_col, scree_w);
col = lerp(col, rock_col, rock_w);
col = lerp(col, sand_col, beach * (1.0 - rock_w));

// ---- normals
float3 nn = n;
float3 rn, rn2, rn3;
TRIN(RockMN, wp, aw, 1.0 / 52.0, 0.4, rn);
TRIN(RockFN, wp, aw, 1.0 / 13.0, 2.7, rn2);
TRIN(RockN, wp, aw, 1.0 / 4.1, 0.9, rn3);
float3 rockn = normalize(rn * 1.0 + rn2 * 0.8 * mid + rn3 * 0.7 * near - n * (0.8 * mid + 0.7 * near));
float3 t;
t = UNPN(S2(GroundN, ROT(wp.xz / 27.0, 0.2))); t.xy = ROT(t.xy, -0.2); float3 gnn = normalize(float3(n.x + t.x * 0.6, n.y, n.z + t.y * 0.6));
t = UNPN(S2(GravN, ROT(wp.xz / 5.3, 0.7))); t.xy = ROT(t.xy, -0.7); float3 scn = normalize(float3(n.x + t.x * 0.9, n.y, n.z + t.y * 0.9));
t = UNPN(S2(SnowN, ROT(wp.xz / 6.0, 1.1))); t.xy = ROT(t.xy, -1.1); float3 snn = normalize(float3(n.x + t.x * 0.3, n.y, n.z + t.y * 0.3));
t = UNPN(S2(SandN, ROT(wp.xz / 4.5, 0.1))); t.xy = ROT(t.xy, -0.1); float3 sdn = normalize(float3(n.x + t.x * 0.5, n.y, n.z + t.y * 0.5));
float3 dn = lerp(gnn, scn, scree_w);
dn = lerp(dn, rockn, rock_w);
dn = lerp(dn, sdn, beach * (1.0 - rock_w));
nn = normalize(lerp(n, dn, max(mid, rock_w * 0.6)));

// ---- snow: aspect/altitude mask, shed from steep ground per pixel, dusting ledges (detail normal facing up) on rock
float snow_flat = saturate(snowM * 1.35) * smoothstep(0.5, 0.3, slope + (nz3.a - 0.5) * 0.1);
float snow_ledge = saturate(snowM * 1.6) * smoothstep(0.78, 0.93, rockn.y) * rock_w;
float sw = saturate(max(snow_flat, snow_ledge) + (sn.a - 0.5) * 0.2 * near);
sw = smoothstep(0.25, 0.75, sw);
col = lerp(col, snow_col, sw);
nn = normalize(lerp(nn, snn, sw * 0.8));

// ---- forest canopy where no real trees are drawn (outside the tree corridor or beyond their cull distance)
float canopy = saturate(forest * 1.4) * (1.0 - sw) * (1.0 - treezone * (1.0 - smoothstep(TreeFar * 0.75, TreeFar, dist)));
col = lerp(col, canopy_col, canopy);
nn = normalize(lerp(nn, n, canopy * 0.7));
col *= 1.0 - saturate(forest * 1.4) * (1.0 - canopy) * 0.35;                           // under-canopy shade

// ---- rivers and lakes
// channel edge wobbles with noise (hides the flow-grid staircase); pale gravel bars border the water
float wedge = wet + (nz3.r - 0.5) * 0.35 + (S2(Noise, wp.xz / 13.0).g - 0.5) * 0.2;
float water = saturate(max(smoothstep(0.62, 0.8, wedge), lake)) * (1.0 - rock_w * 0.8);
float bars = smoothstep(0.35, 0.55, wedge) * (1.0 - water) * (1.0 - rock_w) * (1.0 - sw);
col = lerp(col, float3(0.2, 0.195, 0.185) * (0.8 + 0.4 * LUM(gv.rgb) * 2.0), bars * 0.85);
col = lerp(col, water_col, water);
col = lerp(col, col * 0.72, saturate(wet * 1.5) * (1.0 - water) * 0.6);               // damp ground along streams
nn = normalize(lerp(nn, float3(0, 1, 0) + (nz3.xyz - 0.5) * 0.03, water));

NormalWS = float3(nn.x, nn.z, nn.y);
Rough = lerp(lerp(lerp(0.9, 0.82, rock_w), 0.62, sw), 0.05, water);
Spec = lerp(0.35, 0.5, max(water, sw));
// ---- baked lighting: fine sky visibility from the masks (core) / bake (world), one bounce as emissive
float skyv = lerp(bk.a, mC.a, cw);
float micro = lerp(1.0, saturate(rm.a * 1.4 + 0.2), rock_w * 0.5) * lerp(1.0, saturate(rf.a * 1.3 + 0.3), rock_w * 0.4 * mid);
AOut = lerp(1.0, saturate(skyv * 1.05) * micro, AOStrength);
float3 bounce = bk.rgb * bk.rgb * 0.25;
Emis = col * bounce * BounceGain * SunLux / 3.14159;
return col;
