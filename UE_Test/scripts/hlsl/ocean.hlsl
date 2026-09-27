// Ocean shading ported from game/shaders/world/ocean.gdshader (pixel-only: Gerstner swell as analytic normals + FFT detail normal maps, depth colour, shore foam).
// Inputs: P (abs world pos cm), Cam (cm), T (time s), textures WaveA, WaveB, Noise, Depth (sqrt-encoded metres below sea level / 255).
#define DeepColor float3(0.006, 0.026, 0.05)
#define ShallowColor float3(0.03, 0.16, 0.17)
#define SSSColor float3(0.02, 0.2, 0.18)
#define S2(T_, uv) Texture2DSample(T_, T_##Sampler, (uv))
#define UNPW(t) float3((t).x * 2.0 - 1.0, sqrt(saturate(1.0 - dot((t).xy * 2.0 - 1.0, (t).xy * 2.0 - 1.0))), -((t).y * 2.0 - 1.0))
float3 wp = float3(P.x, P.z, P.y) * 0.01;
float3 cam = float3(Cam.x, Cam.z, Cam.y) * 0.01;
float dist = length(wp - cam);
float2 uv = ((wp.xz + 40000.0) / 19.53125 + 0.5) / 4096.0;
float dsq = S2(Depth, uv).r;
float depth = (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) ? 255.0 : dsq * dsq * 255.0;
float fade = (1.0 - smoothstep(2500.0, 9000.0, dist)) * smoothstep(0.3, 10.0, depth);
// swell: 4 Gerstner components, normals only
float3 n = float3(0, 1, 0);
float crest = 0;
{
    float2 dirs[4] = { float2(0.94, 0.34), float2(0.71, 0.70), float2(0.99, -0.12), float2(0.55, 0.83) };
    float lens[4] = { 118.0, 61.0, 37.0, 19.0 };
    float amps[4] = { 0.9, 0.45, 0.25, 0.12 };
    [unroll] for (int i = 0; i < 4; i++) {
        float k = 6.2831853 / lens[i];
        float w = sqrt(9.81 * k);
        float ph = k * dot(dirs[i], wp.xz) - w * T;
        float a = amps[i] * fade;
        n.x -= dirs[i].x * k * a * cos(ph);
        n.z -= dirs[i].y * k * a * cos(ph);
        crest += a * sin(ph) / (amps[0] + 0.001);
    }
}
float dfade = 1.0 - smoothstep(300.0, 6000.0, dist);
float3 da = UNPW(S2(WaveA, wp.xz / 64.0 + float2(T * 0.018, T * 0.006)));
float3 db = UNPW(S2(WaveB, wp.xz / 12.0 + float2(-T * 0.011, T * 0.024)));
float3 dn = normalize(float3(da.x + db.x * 0.6, 1.0, da.z + db.z * 0.6));
n = normalize(float3(n.x + dn.x * dfade, n.y, n.z + dn.z * dfade));
float3 col = lerp(ShallowColor, DeepColor, 1.0 - exp(-depth / 14.0));
float3 v = normalize(cam - wp);
float sss = pow(saturate(dot(v, -normalize(float3(-0.262, 0.0, 0.720)))), 3.0) * saturate(crest * 3.0);
col += SSSColor * sss * 0.35 * fade;
float4 nz = S2(Noise, wp.xz / 45.0 + float2(T * 0.01, 0.0));
float4 nz2 = S2(Noise, wp.xz / 9.0 - float2(0.0, T * 0.02));
float surf = smoothstep(3.5, 0.2, depth + (nz.r - 0.5) * 3.0);
float pulse = 0.5 + 0.5 * sin(T * 1.1 - depth * 1.6 + nz.g * 6.0);
float lace = smoothstep(0.5, 0.72, nz2.b * 0.7 + S2(Noise, wp.xz / 3.1 + T * 0.03).r * 0.3 + surf * 0.15);
float foam = surf * lerp(0.35, 1.0, pulse) * lace;
foam += smoothstep(0.55, 0.9, crest + nz2.r * 0.25) * 0.35 * fade;
foam = saturate(foam);
col = lerp(col, float3(0.82, 0.86, 0.88), foam);
NormalWS = float3(n.x, n.z, n.y);
Rough = lerp(lerp(0.035, 0.12, smoothstep(200.0, 12000.0, dist)), 0.7, foam);
return col;
