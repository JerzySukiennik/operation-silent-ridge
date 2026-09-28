# Terrain v2 masks + lighting bake for the UE terrain material, for the whole map (19.5 m, 4096^2) and the refined core (9.77 m,
# 4096 x 3072). Writes PNGs to WORK/tex/:
#   T_mA_{g,c}: R snow, G forest density, B wet (river/lake/wet ground), A scree/talus
#   T_mB_{g,c}: R beach sand, G alpine meadow, B avalanche path/gully, A rock hardness (hard beds / massive rock)
#   T_mC_{g,c}: RG strata phase offset (cos/sin), B bed thickness, A sky visibility (horizon-based)
#   T_bake_g:   RGB one-bounce terrain irradiance (sqrt(4x)), A sky visibility (cosine-weighted) - same encoding as stage 1
#   T_ocean_g:  R depth below sea level (sqrt encoded, 0..255 m), G distance to shore (sqrt encoded, 0..1500 m)
# Snow: altitude snowline lowered on north and lee (east) faces and in concave hollows, scoured on crests, shed from >45 deg rock;
# glaciers from the glacial stage stay white. Forest: below an aspect-dependent treeline, off cliffs, scree, avalanche paths
# and river gravel, clumped. Every rule here is also what scatter.py uses for tree placement.
import os
import numpy as np
from PIL import Image
from scipy import ndimage
import tv2
from tv2 import X0, TO_SUN, CELL2, CORE_CELL, CORE_X, CORE_Z, smoothstep

Image.MAX_IMAGE_PIXELS = None
OUT = os.path.join(tv2.WORK, "tex"); os.makedirs(OUT, exist_ok=True)
S = 7
hG = np.load(os.path.join(tv2.WORK, "h_global_final.npy")).astype(np.float32)
C = tv2.load("core"); Wd = tv2.load("world_4096")


def strata(h, x0, z0, cell):
    """Same bed geometry as gen_core.py (so rock colour bands follow the carved ledges)."""
    rows, cols = h.shape
    X, Z = np.meshgrid(x0 + np.arange(cols) * cell, z0 + np.arange(rows) * cell)
    nz = lambda sc, sd, **kw: tv2.noise(cols, rows, x0, z0, cell, sc, seed=S + sd, **kw)
    dip_dir = np.radians(35.0); dip = np.tan(np.radians(14.0))
    fold = nz(2600.0, 51, octaves=3, warp=0.6, warp_scale=4000.0) * 60.0 + nz(700.0, 52, octaves=3) * 12.0
    thick = 34.0 + 16.0 * nz(3000.0, 53, octaves=2)
    off = (dip * (X * np.cos(dip_dir) + Z * np.sin(dip_dir)) + fold) / thick + 0.5 * nz(1500.0, 54, octaves=2)
    a = 2 * np.pi * (off - np.floor(off))
    return np.cos(a), np.sin(a), (thick - 18.0) / 34.0


def build(h, cell, x0, z0, A_in=None, scree_in=None, hard_in=None, ice_in=None, tag="g"):
    rows, cols = h.shape
    nz = lambda sc, sd, **kw: tv2.noise(cols, rows, x0, z0, cell, sc, seed=S + sd, **kw)
    n = tv2.normals(h, cell)
    slope = 1.0 - n[..., 1]
    sea = h < 0.0
    outl = sea.astype(np.uint8); outl[:, 0] = 1
    A = tv2.mfd(h, outl, cell, 1.1) if A_in is None else A_in
    lap = ndimage.gaussian_filter(h, 60.0 / cell) - h                   # >0 in hollows, <0 on crests (m)
    lap_big = ndimage.gaussian_filter(h, 400.0 / cell) - ndimage.gaussian_filter(h, 60.0 / cell)
    n1 = nz(900.0, 81, octaves=4, warp=0.5, warp_scale=1500.0)
    n2 = nz(160.0, 82, octaves=3)
    # ---- snow
    sline = 1500.0 + 150.0 * n1 + 240.0 * n[..., 2] - 110.0 * n[..., 0] - np.clip(lap * 6.0, -120, 160) - np.clip(lap_big * 1.5, -80, 120)
    snow = smoothstep(sline - 90.0, sline + 150.0, h + 25.0 * n2)
    snow *= smoothstep(0.42, 0.26, slope + 0.05 * n2)                  # sheds from > ~50 deg, thin on 40-50 deg
    if ice_in is not None:
        snow = np.maximum(snow, smoothstep(0.3, 0.8, ice_in) * smoothstep(1300.0, 1600.0, h) * smoothstep(0.5, 0.3, slope))
    # ---- wetness: rivers, lakes (flat filled water), wet ground
    lA = np.log10(np.maximum(A, 1.0))
    # rivers as thin continuous lines from D8 area (MFD spreads into blobs on floodplains); width grows with discharge
    Ad = np.empty_like(h); tv2.K.drainage_area(tv2.fp(tv2.f32(h)), tv2.u8p(outl), cols, rows, cell, tv2.fp(Ad))
    lAd = np.log10(np.maximum(Ad, 1.0))
    river = (lAd > 6.7).astype(np.float32)
    river = np.maximum(river, ndimage.binary_dilation(lAd > 7.6, iterations=max(1, int(12.0 / cell))).astype(np.float32))
    river = ndimage.gaussian_filter(river, 0.6) * (h > 0.5)
    lake = np.zeros_like(h)                       # no flat-fill lakes any more (no-fill erosion keeps real relief)
    wet = np.clip(river, 0, 1)
    wet_soft = ndimage.gaussian_filter(np.maximum(wet, smoothstep(6.0, 7.0, lA)), 40.0 / cell)
    # ---- scree / talus
    if scree_in is not None:
        scree = smoothstep(0.3, 3.0, scree_in)
    else:
        scree = smoothstep(0.2, 0.3, slope) * smoothstep(0.42, 0.34, slope) * smoothstep(900.0, 1500.0, h + 200 * n1)
    # ---- avalanche paths: steep, high source areas routed downhill, runout dies on gentle ground
    src = (smoothstep(0.35, 0.55, slope) * smoothstep(1100.0, 1500.0, h)).astype(np.float32)
    carry = (0.985 * smoothstep(0.08, 0.2, slope)).astype(np.float32)
    av = tv2.accum(h, outl, src, carry)
    aval = smoothstep(25.0, 140.0, av) * smoothstep(0.1, 0.22, slope)
    # ---- forest
    tl = 1280.0 + 130.0 * n1 + 150.0 * n[..., 2]                        # south faces grow higher
    clump = smoothstep(-0.35, 0.35, nz(320.0, 83, octaves=4, warp=0.6, warp_scale=500.0) + 0.3)
    forest = smoothstep(tl + 40.0, tl - 160.0, h) * smoothstep(0.42, 0.3, slope) * clump
    forest *= (1 - scree) * (1 - aval * 0.9) * (1 - wet) * (1 - snow) * smoothstep(3.0, 12.0, h)
    # ---- beach / meadow / hardness
    land = ~sea
    dsea = ndimage.distance_transform_edt(land) * cell
    beach = smoothstep(90.0, 20.0, dsea) * smoothstep(0.12, 0.05, slope) * smoothstep(7.0, 2.0, h) * land
    delta = smoothstep(7.0, 7.8, lA) * smoothstep(4.0, 1.0, h) * land
    beach = np.maximum(beach, delta)
    meadow = smoothstep(0.34, 0.18, slope) * (1 - forest) * (1 - snow) * smoothstep(tl - 200, tl + 50, h) * smoothstep(tl + 700, tl + 350, h)
    meadow = np.maximum(meadow, (1 - forest) * smoothstep(0.1, 0.03, slope) * smoothstep(3, 10, h) * (1 - wet) * 0.8)
    hard = np.clip((hard_in - 0.2) / 1.4, 0, 1) if hard_in is not None else smoothstep(0.3, 0.6, slope)
    # ---- strata + sky visibility
    cs, sn, th = strata(h, x0, z0, cell)
    sky = tv2.ao(h, cell, dirs=16, maxdist=2500.0 if tag == "c" else 4000.0)
    sky = np.clip(sky * (1 + n[..., 1]) / 2 / np.percentile(sky[land], 99.5), 0, 1)

    def png(name, chans):
        a = np.stack([np.clip(c, 0, 1) * 255 for c in chans], -1).round().astype(np.uint8)
        Image.fromarray(a, "RGBA").save(os.path.join(OUT, name + ".png"))
        print("wrote", name, a.shape, flush=True)
    png("T_mA_" + tag, [snow, forest, np.maximum(wet, wet_soft * 0.6), scree])
    png("T_mB_" + tag, [beach, meadow, aval, hard])
    png("T_mC_" + tag, [cs * 0.5 + 0.5, sn * 0.5 + 0.5, th, sky])
    return dict(snow=snow, forest=forest, wet=wet, scree=scree, aval=aval, beach=beach, sky=sky, lake=lake)


# ------------------------------------------------------------------ core (9.77 m)
hc = C["h"][::2, ::2].astype(np.float32).copy()
cc = CORE_CELL * 2
Ac = ndimage.zoom(C["A"], 0.5, order=0) if False else None
mc = build(hc, cc, CORE_X[0], CORE_Z[0], None, ndimage.maximum_filter(C["scree"], 2)[::2, ::2], C["hard"][::2, ::2], C["ice"][::2, ::2].astype(np.float32), "c")
np.savez_compressed(os.path.join(tv2.WORK, "masks_core.npz"), **{k: v.astype(np.float16) for k, v in mc.items()})
# T_mD_c (2048 x 1536): R = tree zone (firs are placed within 1.7 km of the route, scatter.py), G = lake surface
from scipy.spatial import cKDTree
route = Wd["route"]
dense = np.concatenate([np.linspace(route[k], route[k + 1], 8, endpoint=False) for k in range(len(route) - 1)])
xs = CORE_X[0] + np.arange(2048) * (CORE_X[1] - CORE_X[0]) / 2048; zs = CORE_Z[0] + np.arange(1536) * (CORE_Z[1] - CORE_Z[0]) / 1536
XX, ZZ = np.meshgrid(xs, zs)
dd = cKDTree(dense).query(np.stack([XX.ravel(), ZZ.ravel()], 1))[0].reshape(XX.shape)
tz = smoothstep(1720.0, 1600.0, dd)
lk = ndimage.zoom(mc["lake"], (1536 / mc["lake"].shape[0], 2048 / mc["lake"].shape[1]), order=1)
a = np.stack([tz, np.clip(lk, 0, 1), np.zeros_like(tz), np.ones_like(tz)], -1)
Image.fromarray((a * 255).round().astype(np.uint8), "RGBA").save(os.path.join(OUT, "T_mD_c.png"))
print("wrote T_mD_c", flush=True)
# ------------------------------------------------------------------ global (19.5 m)
mg = build(hG, CELL2, X0, X0, None, None, Wd["hard"], Wd["ice"].astype(np.float32), "g")

# ------------------------------------------------------------------ bake: one-bounce + cosine sky visibility at 39 m (as stage 1)
R = 2048; H = hG.reshape(R, 2, R, 2).mean(axis=(1, 3)); CELL = tv2.MAP / R
gz, gx = np.gradient(H, CELL)
nb = np.stack([-gx, np.ones_like(H), -gz], -1); nb /= np.linalg.norm(nb, axis=-1, keepdims=True)
ndl = np.clip(nb @ TO_SUN, 0, 1)
sunvis = tv2.sun_vis(tv2.f32(H), CELL)
d = lambda k: mg[k].reshape(R, 2, R, 2).mean(axis=(1, 3))
snow, forest = d("snow"), d("forest")
rock = np.clip(1 - snow - forest, 0, 1)
alb = snow[..., None] * np.array([0.8, 0.82, 0.86]) + forest[..., None] * np.array([0.035, 0.05, 0.04]) + rock[..., None] * np.array([0.22, 0.22, 0.23])
alb[H < 0.5] = np.array([0.03, 0.05, 0.06])
Lout = alb * (ndl * sunvis)[..., None]
Hw = np.maximum(H, 0.0)
dists = np.unique(np.round(np.geomspace(1, 7000.0 / CELL, 44)).astype(int)); P = int(dists.max()) + 2
Hp = np.pad(Hw, P, mode="edge"); Lp = np.pad(Lout, ((P, P), (P, P), (0, 0)), mode="edge")
sky = np.zeros((R, R), np.float32); bounce = np.zeros((R, R, 3), np.float32)
DIRS = 24
for di in range(DIRS):
    a = 2 * np.pi * (di + 0.5) / DIRS; dx, dz = np.cos(a), np.sin(a)
    best = np.full((R, R), -1e9, np.float32); Lh = np.zeros((R, R, 3), np.float32)
    for dd in dists:
        ox, oz = int(round(dx * dd)), int(round(dz * dd)); dist = np.hypot(ox, oz) * CELL
        if dist == 0:
            continue
        sh = Hp[P + oz:P + oz + R, P + ox:P + ox + R]
        t = (sh - Hw) / dist; upd = t > best
        best = np.where(upd, t, best); Lh = np.where(upd[..., None], Lp[P + oz:P + oz + R, P + ox:P + ox + R], Lh)
    el = np.arctan(np.maximum(best, 0.0)); el_plane = np.arctan(-(gx * dx + gz * dz)); el_eff = np.maximum(el, el_plane)
    sky += np.cos(el_eff) ** 2
    bounce += Lh * (np.sin(el_eff) ** 2 - np.sin(np.maximum(el_plane, 0)) ** 2).clip(0)[..., None]
sky /= DIRS; bounce /= DIRS
sky *= (1 + nb[..., 1]) / 2
sky = np.clip(sky / np.percentile(sky[H > 0.5], 99.5), 0, 1)
enc = np.dstack([np.clip(np.sqrt(np.clip(bounce * 4.0, 0, 1)) * 255, 0, 255), np.clip(sky * 255, 0, 255)]).astype(np.uint8)
Image.fromarray(enc, "RGBA").resize((4096, 4096), Image.BILINEAR).save(os.path.join(OUT, "T_bake_g.png"))
print("wrote bake", flush=True)

# ------------------------------------------------------------------ ocean depth + shore distance (19.5 m)
land = hG >= 0.0
dshore = ndimage.distance_transform_edt(~land) * CELL2
dep = np.round(np.sqrt(np.clip(-hG, 0, 255) / 255.0) * 255).astype(np.uint8)
shd = np.round(np.sqrt(np.clip(dshore, 0, 1500) / 1500.0) * 255).astype(np.uint8)
# swell exposure: share of the WSW-SW swell directions with >= 12 km of open water upwind (fjords/bays ~0, open coast ~1)
wc = (hG < 0.0)[::4, ::4]; c4 = CELL2 * 4
exp_ = np.zeros(wc.shape, np.float32)
dirs = [np.radians(a) for a in (160, 170, 180, 190, 200, 210)]       # upwind directions (towards -x, i.e. the open sea)
steps = np.arange(1, int(12000 / c4) + 1)
PADC = int(steps[-1]) + 2
wp_ = np.pad(wc, PADC, constant_values=True)                        # beyond the map edge = open ocean
R0, C0 = wc.shape
for a in dirs:
    ok = np.ones(wc.shape, bool)
    for k in steps:
        dx, dz = int(round(np.cos(a) * k)), int(round(np.sin(a) * k))
        ok &= wp_[PADC + dz:PADC + dz + R0, PADC + dx:PADC + dx + C0]
    exp_ += ok
exp_ = ndimage.gaussian_filter(exp_ / len(dirs), 3.0)
expo = np.round(np.clip(ndimage.zoom(exp_, 4, order=1), 0, 1) * 255).astype(np.uint8)
Image.fromarray(np.dstack([dep, shd, expo]), "RGB").save(os.path.join(OUT, "T_ocean_g.png"))
print("wrote ocean; masks done", flush=True)
