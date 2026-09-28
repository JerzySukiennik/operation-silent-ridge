// Shared swell set (vertex WPO and pixel normal must agree): 8 Gerstner waves, incommensurate wavelengths, spread around a
// swell from the SW (travelling NE); amplitude modulated over kilometres by a macro field (wind patches vs calm slicks).
// Needs: float3 wp (Godot metres), float T, float amp (depth/exposure/distance fade), float macro. Produces: float3 disp, gn, crest.
float3 disp = 0; float3 gn = float3(0, 1, 0); float crest = 0;
{
	const float4 WV[8] = { float4(-22.0, 137.0, 0.75, 0.0), float4(-35.0, 91.3, 0.45, 1.7), float4(-8.0, 58.7, 0.28, 4.1), float4(-51.0, 37.1, 0.16, 2.3),
	                       float4(12.0, 23.9, 0.09, 5.2), float4(-70.0, 15.3, 0.05, 0.9), float4(-28.0, 9.7, 0.03, 3.3), float4(31.0, 6.1, 0.015, 1.1) };
	[unroll] for (int i = 0; i < 8; i++) {
		float a = radians(WV[i].x); float2 dir = float2(cos(a), sin(a));
		float k = 6.2831853 / WV[i].y; float w = sqrt(9.81 * k);
		float A = WV[i].z * amp * lerp(0.55, 1.3, macro);
		float ph = k * dot(dir, wp.xz) - w * T + WV[i].w;
		float Q = 0.6 / (k * max(WV[i].z, 1e-3) * 8.0);
		float c = cos(ph), s = sin(ph);
		disp.xz += Q * A * dir * c;
		disp.y += A * s;
		gn.xz -= dir * k * A * c;
		gn.y -= Q * k * A * s;
		crest += Q * k * A * s;
	}
	gn = normalize(gn);
}
