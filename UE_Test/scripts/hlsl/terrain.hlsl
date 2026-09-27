// Terrain shading ported from game/shaders/world/terrain.gdshader (Godot space inside: x east, y up, z south, metres).
// Inputs: P (abs world pos cm), N (world normal), Cam (camera pos cm), TreeFar (m, tree cull distance), textures RockM,RockMN,Rock,RockN,Scree,ScreeN,ScreeM,Snow,SnowN,SnowM,Ground,GroundN,Floor,Shore,MasksA,MasksB,Noise.
// Outputs: return = base colour; NormalWS, Rough, AOut, Spec.
#define RockTint float3(0.5, 0.51, 0.54)
#define SnowColor float3(0.88, 0.9, 0.93)
#define CanopyColor float3(0.028, 0.042, 0.034)
#define S2(T, uv) Texture2DSample(T, T##Sampler, (uv))
#define UNPN(t) float3((t).x * 2.0 - 1.0, -((t).y * 2.0 - 1.0), sqrt(saturate(1.0 - dot((t).xy * 2.0 - 1.0, (t).xy * 2.0 - 1.0))))
#define TRI(T, p, w, sc) ((w.x > 0.02 ? S2(T, (p).zy * (sc)) * w.x : 0) + (w.y > 0.02 ? S2(T, (p).xz * (sc)) * w.y : 0) + (w.z > 0.02 ? S2(T, (p).xy * (sc)) * w.z : 0))
float3 wp = float3(P.x, P.z, P.y) * 0.01;
float3 cam = float3(Cam.x, Cam.z, Cam.y) * 0.01;
float3 n = normalize(float3(N.x, N.z, N.y));
float dist = length(wp - cam);
float2 uv = ((wp.xz + 40000.0) / 9.765625 + 0.5) / 8192.0;
float4 ma = S2(MasksA, uv);
float4 mb = S2(MasksB, uv);
float4 nz = S2(Noise, wp.xz / 1900.0);
float4 nz2 = S2(Noise, wp.xz / 230.0);
float4 nz3 = S2(Noise, wp.xz / 37.0);
float slope = 1.0 - n.y;
float slope_d = slope;
float alt = wp.y;
float curv = mb.g - 0.5;
float near = 1.0 - smoothstep(120.0, 700.0, dist);
float mid = 1.0 - smoothstep(2500.0, 9000.0, dist);
float snowline = 520.0, treeline = 1550.0;
float wet = ma.b;
float gully = smoothstep(0.03, 0.14, curv) * smoothstep(0.35, 0.75, wet + curv);
float rock_w = smoothstep(0.28, 0.48, slope + (nz2.r - 0.5) * 0.16 - curv * 0.3 + mb.a * 0.1);
float sline = snowline + (nz.r - 0.5) * 420.0 + (nz2.g - 0.5) * 110.0 - curv * 400.0 - n.z * 90.0;
float snow_alt = smoothstep(sline - 100.0, sline + 220.0, alt);
float hold = 0.34 + gully * 0.3 - max(-curv, 0.0) * 0.5 + (nz3.a - 0.5) * 0.12 + (nz2.b - 0.5) * 0.08;
float snow_w = snow_alt * smoothstep(hold + 0.08, hold - 0.1, slope_d);
float scree_w = ma.a * smoothstep(0.08, 0.35, slope) * (1.0 - rock_w) * smoothstep(treeline - 700.0, treeline - 200.0, alt + nz.g * 300.0);
scree_w = max(scree_w, smoothstep(treeline + 100.0, treeline + 500.0, alt + nz.b * 400.0) * (1.0 - rock_w) * 0.5);
float shore_w = smoothstep(7.0, 1.5, alt + nz2.b * 5.0) * (1.0 - rock_w * 0.6);
float forest = mb.r * smoothstep(treeline + 80.0, treeline - 200.0, alt);
float water = mb.b;
float3 aw = pow(abs(n), 6.0); aw /= (aw.x + aw.y + aw.z);
float4 rm = TRI(RockM, wp, aw, 1.0 / 46.0);
float4 rm2 = TRI(RockM, wp + float3(311.0, 0.0, 127.0), aw, 1.0 / 173.0);
float4 rc = near > 0.0 ? TRI(Rock, wp, aw, 1.0 / 9.0) : float4(0.5, 0.5, 0.5, 0.5);
float3 slab = lerp(rm.rgb, rm2.rgb, 0.4 + 0.3 * (1.0 - mid));
float sl = dot(slab, 0.33);
float3 rock_col = lerp(sl.xxx, slab, 0.3) * float3(1.0, 1.0, 1.04);
rock_col = lerp(rock_col, rock_col * rc.rgb * 2.2, near * 0.5);
rock_col *= RockTint * 1.9 * (0.8 + 0.4 * nz2.a) * (0.88 + 0.24 * nz.a);
float streak = S2(Noise, float2(dot(wp.xz, float2(0.7071, 0.7071)) / 55.0, wp.y / 900.0)).g;
float streak2 = S2(Noise, float2(dot(wp.xz, float2(-0.7071, 0.7071)) / 38.0, wp.y / 700.0)).a;
float band = S2(Noise, float2(wp.y / 260.0, dot(wp.xz, float2(0.6, 0.8)) / 4000.0)).b;
float wall = 1.0 - aw.y;
rock_col *= lerp(1.0, (0.65 + 0.7 * streak * streak2) * (0.8 + 0.4 * band), wall);
rock_col = lerp(rock_col, rock_col * float3(0.85, 0.83, 0.78), smoothstep(0.55, 0.8, band) * wall);
float cliff = smoothstep(0.3, 0.55, slope_d) * rock_w * smoothstep(0.35, 0.6, S2(Noise, wp.xz / 310.0 + 0.37).g);
float yy = wp.y + (S2(Noise, wp.xz / 400.0).r - 0.5) * 30.0 + (nz3.g - 0.5) * 5.0;
float per = 19.0 + 14.0 * nz.b;
float lf = frac(yy / per);
float ledge_top = smoothstep(0.80, 0.9, lf) * (1.0 - smoothstep(0.96, 1.0, lf));
float under = 1.0 - smoothstep(0.0, 0.18, lf);
float2 along = normalize(float2(-n.z, n.x) + 1e-5);
float jf = frac(dot(wp.xz, along) / (13.0 + 9.0 * nz2.g) + nz3.r * 0.6);
float joint = smoothstep(0.0, 0.06, jf) * (1.0 - smoothstep(0.9, 1.0, jf));
rock_col *= lerp(1.0, (1.0 - 0.25 * under) * lerp(0.82, 1.0, joint), cliff);
float rock_h = rm.a * 0.6 + rm2.a * 0.4;
float4 sc = S2(Scree, wp.xz / 3.0);
float4 scm = S2(ScreeM, wp.xz / 85.0 + nz.rg * 0.2);
float3 scree_col = lerp(dot(scm.rgb, 0.33).xxx * float3(0.9, 0.9, 0.93), sc.rgb, near * 0.5) * (0.8 + 0.35 * nz3.r);
float4 sn = S2(Snow, wp.xz / 5.0);
float4 snm = S2(SnowM, wp.xz / 160.0 + nz.ba * 0.25);
float3 snow_col = SnowColor * (0.92 + 0.12 * dot(snm.rgb, 0.33) + 0.05 * (nz3.b - 0.5));
float4 gr = S2(Ground, wp.xz / 26.0);
float2 rot = float2(dot(float2(0.8, -0.6), wp.xz), dot(float2(0.6, 0.8), wp.xz)) / 61.0;
float4 gr2 = S2(Ground, rot);
float3 gcol = lerp(gr.rgb, gr2.rgb, smoothstep(0.35, 0.65, nz2.r));
float gl = dot(gcol, 0.33);
float3 ground_col = lerp(gl.xxx, gcol, 0.4) * float3(0.62, 0.66, 0.55);
float4 fl = S2(Floor, wp.xz / 3.5);
ground_col = lerp(ground_col, fl.rgb * 0.6, smoothstep(0.1, 0.6, mb.r) * 0.6 * near);
ground_col *= 0.75 + 0.4 * nz2.b;
float4 sh = S2(Shore, wp.xz / 18.0);
float3 shore_col = sh.rgb * lerp(0.5, 0.95, smoothstep(0.0, 2.5, alt));
float3 col = ground_col;
float wr = saturate((rock_w * (0.35 + rock_h)) * 1.4);
col = lerp(col, scree_col, saturate(scree_w * (0.35 + sc.a) * 1.4));
col = lerp(col, rock_col, wr);
float sw = saturate(snow_w * 1.5 - (1.0 - snow_w) * rock_h * 0.8 * rock_w + (sn.a - 0.5) * 0.25 * near);
col = lerp(col, snow_col, sw);
col = lerp(col, shore_col, shore_w * (1.0 - sw));
float canopy = forest * smoothstep(TreeFar * 0.75, TreeFar, dist);
float crowns = smoothstep(0.42, 0.58, S2(Noise, wp.xz / 8.0).r * 0.7 + nz3.g * 0.3 + (forest - 0.5) * 0.3);
float3 can = lerp(snow_col * 0.75 * snow_alt + ground_col * (1.0 - snow_alt), CanopyColor * (0.8 + 0.4 * nz3.g), saturate(crowns * 0.35 + 0.6));
col = lerp(col, can, canopy);
col *= 1.0 - forest * (1.0 - canopy) * 0.3;
col = lerp(col, float3(0.035, 0.075, 0.07), water);
// normals (Godot whiteout triplanar)
float3 nn = n;
if (near > 0.0) {
    float3 r = 0;
    float3 tn;
    if (aw.x > 0.02) { tn = UNPN(S2(RockMN, wp.zy / 46.0)); tn = float3(tn.xy + n.zy, abs(tn.z) * n.x); r += tn.zyx * aw.x; }
    if (aw.y > 0.02) { tn = UNPN(S2(RockMN, wp.xz / 46.0)); tn = float3(tn.xy + n.xz, abs(tn.z) * n.y); r += tn.xzy * aw.y; }
    if (aw.z > 0.02) { tn = UNPN(S2(RockMN, wp.xy / 46.0)); tn = float3(tn.xy + n.xy, abs(tn.z) * n.z); r += tn * aw.z; }
    float3 r2 = 0;
    if (aw.x > 0.02) { tn = UNPN(S2(RockN, wp.zy / 9.0)); tn = float3(tn.xy + n.zy, abs(tn.z) * n.x); r2 += tn.zyx * aw.x; }
    if (aw.y > 0.02) { tn = UNPN(S2(RockN, wp.xz / 9.0)); tn = float3(tn.xy + n.xz, abs(tn.z) * n.y); r2 += tn.xzy * aw.y; }
    if (aw.z > 0.02) { tn = UNPN(S2(RockN, wp.xy / 9.0)); tn = float3(tn.xy + n.xy, abs(tn.z) * n.z); r2 += tn * aw.z; }
    float3 rn = normalize(normalize(r) + normalize(r2) * near * 0.6);
    float3 t;
    t = UNPN(S2(GroundN, wp.xz / 26.0)); float3 gn = normalize(float3(n.x + t.x * 0.7, n.y, n.z + t.y * 0.7));
    t = UNPN(S2(SnowN, wp.xz / 5.0)); float3 snn = normalize(float3(n.x + t.x * 0.25, n.y, n.z + t.y * 0.25));
    t = UNPN(S2(ScreeN, wp.xz / 3.0)); float3 scn = normalize(float3(n.x + t.x * 0.9, n.y, n.z + t.y * 0.9));
    float3 dn = lerp(gn, scn, scree_w);
    dn = lerp(dn, rn, wr);
    dn = lerp(dn, snn, sw);
    nn = normalize(lerp(n, dn, near));
    float3 side = normalize(float3(along.x, 0.0, along.y));
    nn = normalize(nn + cliff * (float3(0, 1, 0) * (ledge_top * 0.6 - under * 0.25) + side * (1.0 - joint) * 0.3 * sign(jf - 0.5)));
} else {
    float3 r = 0; float3 tn;
    if (aw.x > 0.02) { tn = UNPN(S2(RockMN, wp.zy / 46.0)); tn = float3(tn.xy + n.zy, abs(tn.z) * n.x); r += tn.zyx * aw.x; }
    if (aw.y > 0.02) { tn = UNPN(S2(RockMN, wp.xz / 46.0)); tn = float3(tn.xy + n.xz, abs(tn.z) * n.y); r += tn.xzy * aw.y; }
    if (aw.z > 0.02) { tn = UNPN(S2(RockMN, wp.xy / 46.0)); tn = float3(tn.xy + n.xy, abs(tn.z) * n.z); r += tn * aw.z; }
    nn = normalize(lerp(n, normalize(r), wr * mid * 0.8 * (1.0 - sw)));
}
NormalWS = float3(nn.x, nn.z, nn.y);
Rough = lerp(lerp(0.93, 0.72, sw), 0.1, water);
Spec = lerp(0.3, 0.5, max(water, sw));
AOut = lerp(0.35, 1.0, ma.r);
return col;
