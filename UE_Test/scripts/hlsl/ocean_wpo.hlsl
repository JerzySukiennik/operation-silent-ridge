// Ocean v2 vertex displacement (see ocean_waves.inc). Inputs: P (abs world pos cm, without offsets), Cam (cm), T (s), Ocean (R depth, G shore, B swell exposure), Noise. Returns WPO (cm).
float3 wp = float3(P.x, P.z, P.y) * 0.01; float3 cam = float3(Cam.x, Cam.z, Cam.y) * 0.01;
float4 oc = Texture2DSampleLevel(Ocean, OceanSampler, (wp.xz + 40000.0) / 80000.0, 0);
float depth = oc.r * oc.r * 255.0;
float macro = Texture2DSampleLevel(Noise, NoiseSampler, wp.xz / 3710.0 + 0.13, 0).g * 0.65 + Texture2DSampleLevel(Noise, NoiseSampler, wp.xz / 1270.0 + 0.61, 0).b * 0.35;
macro = smoothstep(0.25, 0.75, macro);
float amp = smoothstep(0.5, 12.0, depth) * (0.12 + 0.88 * oc.b) * (1.0 - smoothstep(4000.0, 9000.0, length(wp.xz - cam.xz)));
#include "/OSR/ocean_waves.inc"
return float3(disp.x, disp.z, disp.y) * 100.0;
