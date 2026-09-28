# Terrain v2, core refinement: the 40 x 30 km playable region at 4.88 m. Upsamples the 19.5 m world, adds geology-driven detail
# (dipping strata with hard/soft beds, two joint sets, crest roughness), re-erodes at full resolution (stream power for fine
# gully networks + talus with per-bed repose angles -> cliff bands, ledges, scree cones) and writes WORK/core.npz.
# Usage: python3 gen_core.py [--seed 7] [--world world_4096]
import argparse, time
import numpy as np
from scipy import ndimage
import tv2
from tv2 import X0, CELL2, CORE_CELL, CORE_X, CORE_Z, CORE_W, CORE_H, smoothstep

ap = argparse.ArgumentParser()
ap.add_argument("--seed", type=int, default=7)
ap.add_argument("--world", default="world_4096")
ap.add_argument("--fluvial", type=int, default=6)
args = ap.parse_args()
S = args.seed
T0 = time.time()


def log(*a):
    print("[%6.1fs]" % (time.time() - T0), *a, flush=True)


W = tv2.load(args.world)
hg = W["h"]; n = hg.shape[0]; cg = tv2.MAP / n
assert abs(cg - CELL2) < 1e-6, "core refinement expects the 19.5 m world"
i0 = int(round((CORE_X[0] - X0) / cg)); j0 = int(round((CORE_Z[0] - X0) / cg))
wg, hgt = CORE_W // 4, CORE_H // 4
M = 8                                                    # margin (global cells) so the cubic upsample has context
blk = hg[j0 - M:j0 + hgt + M + 1, i0 - M:i0 + wg + M + 1]
# node-exact cubic B-spline upsample: core sample (r, c) sits at global (j0 + r/4, i0 + c/4)
rr = M + np.arange(CORE_H) / 4.0; cc = M + np.arange(CORE_W) / 4.0
R, C = np.meshgrid(rr, cc, indexing="ij")
h = ndimage.map_coordinates(blk, [R, C], order=3, mode="nearest").astype(np.float32)
base = h.copy()
log("upsampled core", h.shape, "h %.0f..%.0f" % (h.min(), h.max()))


def up(a, order=1):
    b = a[j0 - M:j0 + hgt + M + 1, i0 - M:i0 + wg + M + 1].astype(np.float32)
    return ndimage.map_coordinates(b, [R, C], order=order, mode="nearest").astype(np.float32)


hard_g = up(W["hard"]); A_g = up(W["A"]); ice_g = up(W["ice"].astype(np.float32))
xs = CORE_X[0] + np.arange(CORE_W) * CORE_CELL; zs = CORE_Z[0] + np.arange(CORE_H) * CORE_CELL
X, Z = np.meshgrid(xs, zs)
X = X.astype(np.float32); Z = Z.astype(np.float32)
nz = lambda scale, seed, **kw: tv2.noise(CORE_W, CORE_H, CORE_X[0], CORE_Z[0], CORE_CELL, scale, seed=S + seed, **kw)

# ------------------------------------------------------------------ geology
nrm = tv2.normals(ndimage.gaussian_filter(h, 2.0), CORE_CELL)
slope = 1.0 - nrm[..., 1]
steep = smoothstep(0.12, 0.35, slope)                     # ~28 deg .. ~50 deg
land = smoothstep(0.0, 6.0, h)
# dipping strata: bed coordinate = elevation + regional dip + folds; bed thickness varies -> cliff bands that cross contours
dip_dir = np.radians(35.0); dip = np.tan(np.radians(14.0))
fold = nz(2600.0, 51, octaves=3, warp=0.6, warp_scale=4000.0) * 60.0 + nz(700.0, 52, octaves=3) * 12.0
bedc = h + dip * (X * np.cos(dip_dir) + Z * np.sin(dip_dir)) + fold
thick = 30.0 + 22.0 * nz(3000.0, 53, octaves=2)
ph = bedc / thick + 0.5 * nz(1500.0, 54, octaves=2)
bedfrac = ph - np.floor(ph)
# only some beds are cliff formers (random per bed) -> irregular cliff bands, not a periodic staircase
bed_r = np.modf(np.sin(np.floor(ph) * 12.9898 + 4.1) * 43758.5453)[0] % 1.0
hard_bed = smoothstep(0.30, 0.42, bedfrac) * smoothstep(0.95, 0.82, bedfrac) * (np.abs(bed_r) > 0.58)
hard_bed *= smoothstep(-0.25, 0.25, nz(650.0, 56, octaves=3, warp=0.8, warp_scale=900.0))    # beds pinch out (lenticular)
massive = smoothstep(1.2, 1.7, hard_g)                    # plutons: massive rock, jointed not bedded
hard = np.clip(0.35 + 0.9 * hard_bed * (1 - massive) + 0.95 * massive + 0.25 * nz(900.0, 55, octaves=3), 0.15, 1.6).astype(np.float32)
log("geology fields")

# ------------------------------------------------------------------ geometric detail
relief = np.clip((h - ndimage.minimum_filter(ndimage.uniform_filter(h, 9), size=int(1200 / CORE_CELL) | 1)) / 500.0, 0, 1)
convex = ndimage.gaussian_filter(h, 6.0) - ndimage.gaussian_filter(h, 24.0)
crest = smoothstep(2.0, 18.0, convex) * relief
# crest roughness: warped ridged noise, stronger on crests (arete teeth) and steep ground
rid = nz(380.0, 61, octaves=5, mode=1, warp=0.7, warp_scale=700.0) - 0.42
rough = nz(90.0, 62, octaves=4, warp=0.4, warp_scale=160.0)
h += (rid * (10.0 + 26.0 * crest) * (0.35 + 0.65 * steep) * land).astype(np.float32)
h += (rough * (1.5 + 4.5 * steep) * land).astype(np.float32)
# meso-scale relief on big walls: buttresses, aretes and couloirs (warped ridged noise, 60-400 m), strongest on steep high relief
meso = nz(260.0, 63, octaves=4, mode=1, warp=1.1, warp_scale=380.0) - 0.45
h += (meso * 34.0 * steep ** 1.5 * (0.4 + 0.6 * relief) * land).astype(np.float32)
# two joint sets (NNE and WNW): elongated grooves on steep faces -> buttresses and chimneys that are not fall-line stripes
for k, (ang, amp) in enumerate([(np.radians(22.0), 7.0), (np.radians(-68.0), 5.0)]):
    u = tv2.f32(X * np.cos(ang) + Z * np.sin(ang)); v = tv2.f32(-X * np.sin(ang) + Z * np.cos(ang))
    g = np.empty_like(u)
    tv2.K.fbm_points(tv2.fp(g), tv2.fp(tv2.f32((u / 5.0).ravel())), tv2.fp(tv2.f32(v.ravel())), g.size, 70.0, 3, 2.0, 0.5, S + 70 + k, 1)
    g = g.reshape(u.shape)
    h -= (smoothstep(0.55, 0.9, g) * amp * steep * land * (0.5 + 0.8 * massive + 0.3 * hard_bed)).astype(np.float32)
# strata relief: hard beds stand proud, soft partings recess (only where the slope exposes them)
h += ((hard_bed - 0.55) * 1.5 * smoothstep(0.3, 0.5, slope) * (1 - massive) * land).astype(np.float32)
log("detail added")

# ------------------------------------------------------------------ full-resolution erosion
sea = h < 0.0
lab, nl = ndimage.label(sea)
keep = np.unique(np.r_[lab[:, 0], lab[0, :], lab[-1, :]]); keep = keep[keep > 0]
outl = np.isin(lab, keep).astype(np.uint8)
outl[:, -1] = 1; outl[0, :] = np.maximum(outl[0, :], 0)
# rivers entering the core from outside keep their drainage area (external inflow on the border ring)
ring = np.zeros(h.shape, bool); ring[:2, :] = ring[-2:, :] = True; ring[:, :2] = ring[:, -2:] = True
ext = np.where(ring & ~outl.astype(bool), np.maximum(A_g - CORE_CELL ** 2 * 16, 0) * 0.5, 0).astype(np.float32)
# border cells fixed (so the refined region stays continuous with the 19.5 m world)
fix = outl.copy(); fix[:1, :] = 1; fix[-1:, :] = 1; fix[:, :1] = 1; fix[:, -1:] = 1
# repose angle: soil/regolith slopes stay smooth up to ~40 deg; only already-steep hard rock holds cliffs (up to ~70 deg)
rockiness = smoothstep(0.25, 0.45, slope)
talus = (0.84 + 1.7 * np.clip(hard - 0.35, 0, None) ** 1.2 * rockiness).astype(np.float32)
kmul = (1.3 / np.maximum(hard, 0.3)).astype(np.float32)
tv2.K.set_flow(1.2, 1)
tv2.K.set_amax(4e5)                                      # fine pass builds gully networks; trunk rivers keep the 19.5 m profile
A = tv2.stream_power(h, None, kmul, fix, talus, ext, CORE_CELL, args.fluvial, 1.0, 0.0105 * 0.22, 0.5, 2, 0.35)
tv2.K.set_amax(1e30)
log("fluvial refine done")
# valley floors are not flat: colluvial aprons and fans rise towards the walls, hummocky moraine / bars undulate the floor
flat = smoothstep(0.08, 0.03, slope) * land
steepm = slope > 0.3
dw = ndimage.distance_transform_edt(~steepm) * CORE_CELL
apron = 26.0 * np.exp(-dw / 170.0) * (0.6 + 0.4 * nz(400.0, 64, octaves=3))
hum = 4.5 * nz(230.0, 65, octaves=4, warp=0.6, warp_scale=300.0) + 2.0 * nz(60.0, 66, octaves=3)
h += ((apron + hum) * flat).astype(np.float32)
# rivers: floodplain (smoothed floor) + a shallow channel scaled with discharge, instead of an incised slot
lA = np.log10(np.maximum(ndimage.maximum_filter(A_g, 3), A), dtype=np.float32)
fpm = ndimage.gaussian_filter(smoothstep(6.6, 7.6, lA) * smoothstep(0.12, 0.04, slope), 8.0)
h = (h * (1 - 0.8 * fpm) + ndimage.gaussian_filter(h, 7.0) * 0.8 * fpm).astype(np.float32)
chan = ndimage.gaussian_filter(smoothstep(6.3, 7.0, lA), 1.5)
h -= (chan * (1.2 + 1.6 * np.clip(lA - 6.5, 0, 2)) * land).astype(np.float32)
dep = np.zeros_like(h)
tv2.thermal(h, talus, CORE_CELL, 25, 0.3, dep)
log("talus done, scree volume %.2e m3" % (dep.sum() * CORE_CELL ** 2))

# ------------------------------------------------------------------ keep the border identical to the 19.5 m world
bw = int(300 / CORE_CELL)
dist_b = np.minimum(np.minimum(np.arange(CORE_H)[:, None], CORE_H - 1 - np.arange(CORE_H)[:, None]),
                    np.minimum(np.arange(CORE_W)[None, :], CORE_W - 1 - np.arange(CORE_W)[None, :]))
wgt = smoothstep(0.0, float(bw), dist_b.astype(np.float32))
h = (base + (h - base) * wgt).astype(np.float32)
tv2.save("core", h=h, A=A, hard=hard, scree=dep.astype(np.float32), bedfrac=bedfrac.astype(np.float16), ice=ice_g.astype(np.float16))
log("saved core: h %.0f..%.0f" % (h.min(), h.max()))
