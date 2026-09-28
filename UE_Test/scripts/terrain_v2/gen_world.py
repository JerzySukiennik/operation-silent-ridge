# Terrain v2, global stages (80 x 80 km): tectonic uplift + stream-power fluvial erosion (1024^2 -> 2048^2), glacial carving
# (U troughs, cirques, hanging valleys, fjords) and post-glacial fluvial/thermal refinement at 4096^2 (19.5 m).
# Usage: python3 gen_world.py [--seed 7] [--final-res 4096]   -> WORK/world_<res>.npz (h, A, kmul, hard, glacier, route)
import argparse, json, os, time
import numpy as np
from scipy import ndimage
from scipy.interpolate import CubicSpline
import tv2
from tv2 import X0, MAP, smoothstep

ap = argparse.ArgumentParser()
ap.add_argument("--seed", type=int, default=7)
ap.add_argument("--final-res", type=int, default=4096)
ap.add_argument("--iters0", type=int, default=320)
ap.add_argument("--uplift", type=float, default=5.0)
ap.add_argument("--ela", type=float, default=1500.0)
args = ap.parse_args()
SEED = args.seed
UPS = args.uplift
T0 = time.time()


def log(*a):
    print("[%6.1fs]" % (time.time() - T0), *a, flush=True)


# ------------------------------------------------------------------ layout (Godot frame, metres)
# Guide line of the antecedent river / weak fault zone: sea -> fjord -> gorge -> target basin. The final valley is produced by the
# erosion model (higher erodibility + slightly lower uplift along the fault zone + a shallow pre-existing river course);
# nothing is cut into the final surface along this line.
GUIDE = np.array([[-24000, 11500], [-20500, 10600], [-17200, 9900], [-14200, 8900], [-11800, 7700], [-9600, 7600],
                  [-7600, 8300], [-5400, 7300], [-3600, 5200], [-1500, 4300], [800, 3400], [2600, 1600], [4200, -300],
                  [6400, -1500], [9000, -2200], [11500, -2600], [14000, -3900], [16500, -6200], [18500, -8800]], float)
TARGET = np.array([11500.0, -2600.0])
CARRIER = np.array([-31000.0, 9500.0])


def guide_dense(step=50.0):
    seg = np.r_[0, np.cumsum(np.linalg.norm(np.diff(GUIDE, axis=0), axis=1))]
    sx, sz = CubicSpline(seg, GUIDE[:, 0]), CubicSpline(seg, GUIDE[:, 1])
    s = np.arange(0, seg[-1], step)
    return np.stack([sx(s), sz(s)], 1), s


def coast_x(z, seed=SEED):
    """Mean coastline (x of the shore) as a function of z: irregular, not a straight ramp."""
    zz = np.atleast_1d(z).astype(np.float32)
    a = np.interp(zz, [-40000, -26000, -12000, 0, 9000, 20000, 40000], [-15500, -18500, -16500, -19500, -21000, -18000, -20500])
    return a


def fields(n):
    cell = MAP / n
    xs = tv2.world_axes(n, cell)
    X, Z = np.meshgrid(xs, xs)
    # coastline: a smooth base line warped by two octaves of noise (headlands and bays), not parallel to anything
    wob = tv2.noise(n, n, X0, X0, cell, 14000.0, octaves=4, seed=SEED + 1, warp=0.6, warp_scale=20000.0)
    xc = coast_x(Z.ravel()).reshape(Z.shape) + wob * 5200.0
    inland = X - xc
    g, _ = guide_dense(100.0)
    from scipy.spatial import cKDTree
    kd = cKDTree(g)
    dC, iC = kd.query(np.stack([X.ravel(), Z.ravel()], 1))
    dC = dC.reshape(X.shape).astype(np.float32)
    sC = (iC.reshape(X.shape) * 100.0).astype(np.float32)
    dT = np.hypot(X - TARGET[0], Z - TARGET[1]).astype(np.float32)
    return cell, X.astype(np.float32), Z.astype(np.float32), inland.astype(np.float32), dC, sC, dT


def tectonics(n):
    cell, X, Z, inland, dC, sC, dT = fields(n)
    big = tv2.noise(n, n, X0, X0, cell, 26000.0, octaves=5, seed=SEED + 2, warp=0.8, warp_scale=30000.0)
    # en-echelon structural grain trending NNW (rotated, stretched ridged noise) -> ranges offset along strike, not a comb
    ang = np.radians(-18.0)
    Xr = (X * np.cos(ang) - Z * np.sin(ang)) * 1.0
    Zr = (X * np.sin(ang) + Z * np.cos(ang)) * 0.42
    grain = np.empty_like(X)
    tv2.K.fbm_points(tv2.fp(grain), tv2.fp(tv2.f32(Xr.ravel())), tv2.fp(tv2.f32(Zr.ravel())), grain.size, 9000.0, 4, 2.0, 0.5,
                     SEED + 3, 1)
    grain = grain.reshape(X.shape)
    land = smoothstep(-3000.0, 16000.0, inland)
    belt = np.exp(-((X - 6000.0) / 24000.0) ** 2)
    U = land * (0.45 + 0.55 * belt) * (1.0 + 0.45 * big) * (0.88 + 0.22 * grain)
    # offshore islands / skerries: isolated uplift blobs in the coastal band
    isl = np.clip(tv2.noise(n, n, X0, X0, cell, 5000.0, octaves=4, seed=SEED + 8, warp=0.7, warp_scale=7000.0) - 0.32, 0, None)
    isl = (isl * smoothstep(-14000.0, -3000.0, inland) * smoothstep(0.0, -2500.0, inland)).astype(np.float32)
    U += isl * 0.8
    U *= 1.0 - 0.25 * np.exp(-(dT / 5000.0) ** 2)                       # target basin (graben)
    wid = 1300.0 * (1.0 + 0.45 * tv2.noise(n, n, X0, X0, cell, 6000.0, octaves=3, seed=SEED + 9))
    U *= 1.0 - 0.3 * np.exp(-(dC / wid) ** 2)                            # strike-slip fault zone: a little less uplift
    U = np.clip(U, 0.0, None).astype(np.float32)
    # lithology: erodibility contrasts with sharp-ish contacts (plutons = hard, schist belts = soft)
    lith = tv2.noise(n, n, X0, X0, cell, 11000.0, octaves=5, seed=SEED + 4, warp=1.2, warp_scale=16000.0)
    pl = tv2.noise(n, n, X0, X0, cell, 7000.0, octaves=3, seed=SEED + 5, warp=0.9, warp_scale=9000.0)
    kmul = np.exp(0.55 * lith) * np.where(pl > 0.28, 0.55, 1.0)
    kmul *= 1.0 + 0.9 * np.exp(-(dC / 800.0) ** 2)                      # crushed fault-zone rock erodes fast
    kmul = ndimage.gaussian_filter(kmul, 1.0).astype(np.float32)
    hard = np.clip(1.0 / kmul, 0.2, 2.0).astype(np.float32)
    # initial surface: low rolling land, an antecedent river course along the guide, a continental shelf offshore
    h = (40.0 + 180.0 * smoothstep(0, 20000, inland) + 90.0 * tv2.noise(n, n, X0, X0, cell, 16000.0, octaves=5, seed=SEED + 6)
         + 30.0 * tv2.noise(n, n, X0, X0, cell, 3000.0, octaves=4, seed=SEED + 7))
    prof = 3.0 + 260.0 * (sC / sC.max())
    h = np.maximum(h, 4.0 + 0.004 * np.clip(inland, 0, None))
    h = np.where(inland > -500, np.minimum(h, np.maximum(prof + dC * 0.035, 3.0)), h)
    sea = -25.0 - 0.025 * np.clip(-inland, 0, None)
    h = np.where(inland < 0, np.minimum(sea, h * 0 - 5.0), h)
    h = np.where(isl > 0.01, np.maximum(h, 10.0 + isl * 420.0), h)
    h = np.maximum(h, -600.0).astype(np.float32)
    return cell, h, U, kmul, hard, inland, dC, sC, dT


def outlets_for(h, border=True):
    """Base level = the open ocean only (the below-sea component touching the west edge); small below-sea pockets inland are
    lakes to be filled by the flow routing, not sinks."""
    lab, nl = ndimage.label(h < 0.0)
    keep = np.unique(lab[:, 0]); keep = keep[keep > 0]
    o = np.isin(lab, keep).astype(np.uint8)
    if border:
        o[0, :] = 1; o[-1, :] = 1; o[:, 0] = 1; o[:, -1] = 1
    return o


def talus_for(hard, cell, lo=0.62, hi=1.05):
    # tangent of the stable hillslope angle at this grid scale (steeper for hard rock)
    t = lo + (hi - lo) * np.clip((hard - 0.4) / 1.4, 0, 1)
    return (t * (1.0 + 0.25 * np.clip(40.0 / cell - 0.5, 0, 2))).astype(np.float32)


# ------------------------------------------------------------------ stage A: 1024^2 tectonic + fluvial steady state
n0 = 1024
cell0, h, U, kmul, hard, inland, dC, sC, dT = tectonics(n0)
out = outlets_for(h)
tal = talus_for(hard, cell0)
# antecedent trunk river: its longitudinal profile is a base-level boundary condition during the fluvial stages A and B
# (released for the glacial and post-glacial stages, which shape the valley freely)
gd, gs = guide_dense(20.0)
tang = np.gradient(gd, axis=0); tang /= np.linalg.norm(tang, axis=1, keepdims=True)
nrmv = np.stack([-tang[:, 1], tang[:, 0]], 1)
rng = np.random.default_rng(SEED)
ph = rng.uniform(0, 6.28, 3)
off = 800.0 * np.sin(gs / 1110.0 + ph[0]) + 150.0 * np.sin(gs / 480.0 + ph[1])   # 7 km / 3 km meander wavelengths
pts = gd + nrmv * off[:, None]


def pin_line(n, inland_n, h_n):
    c = MAP / n
    ii = ((pts[:, 0] - X0) / c).round().astype(int); jj = ((pts[:, 1] - X0) / c).round().astype(int)
    k0 = int(np.argmax(inland_n[jj, ii] > 300.0))
    z = 5.0 + 1150.0 * np.clip((gs - gs[k0]) / (gs[-1] - gs[k0]), 0, 1) ** 1.6    # target valley floor ~650 m
    pin = np.zeros(h_n.shape, np.uint8)
    for a_, b_, z_ in zip(jj[k0:], ii[k0:], z[k0:]):
        pin[a_, b_] = 1; h_n[a_, b_] = z_
    return pin


pin = pin_line(n0, inland, h)
outA = np.maximum(out, pin)
log("stage A init: h %.0f..%.0f" % (h.min(), h.max()))
A = tv2.stream_power(h, (U * UPS).astype(np.float32), kmul, outA, tal, None, cell0, args.iters0, 1.0, 0.0105, 0.5, 2, 0.5)
log("stage A done: land h p50/p95/max %.0f/%.0f/%.0f" % tuple(np.percentile(h[h > 0], [50, 95, 100])))
tv2.save("stageA", h=h, A=A)

# ------------------------------------------------------------------ stage B: 2048^2 refinement
n1 = 2048
cell1, _, U1, kmul1, hard1, inland1, dC1, sC1, dT1 = tectonics(n1)
h1 = tv2.resample(h, (n1, n1))
h1 += 12.0 * tv2.noise(n1, n1, X0, X0, cell1, 900.0, octaves=4, seed=SEED + 11) * (h1 > 5)
sea1 = h1 < 0
out1 = np.maximum(outlets_for(h1), pin_line(n1, inland1, h1))
tal1 = talus_for(hard1, cell1)
A1 = tv2.stream_power(h1, (U1 * UPS).astype(np.float32), kmul1, out1, tal1, None, cell1, 40, 1.0, 0.0105, 0.5, 2, 0.5)
log("stage B done: land h p50/p95/max %.0f/%.0f/%.0f" % tuple(np.percentile(h1[h1 > 0], [50, 95, 100])))
tv2.save("stageB", h=h1, A=A1, kmul=kmul1, hard=hard1, inland=inland1)


# ------------------------------------------------------------------ stage C: glacial carving on the fluvial landscape
def carve_u(h, ice, H, W, bed, cell):
    """Parabolic (U) cross-profile around every glacier flow-line cell: bed + H (d/W)^2 up to the ice surface; only lowers."""
    dist, ind = ndimage.distance_transform_edt(~ice, return_indices=True, sampling=cell)
    iy, ix = ind
    Hc, Wc, bc = H[iy, ix], W[iy, ix], bed[iy, ix]
    # continuous parabola (no reach cut-off, which left vertical steps where it met the original slope); a trough is carved
    # only where the parabola lies below the fluvial surface, so walls meet the old slopes at a trimline shoulder
    prof = bc + Hc * (dist / np.maximum(Wc, 1.0)) ** 2
    carved = np.minimum(prof, h)
    cut = ndimage.gaussian_filter(h - carved, 1.5)
    return (h - cut).astype(np.float32)


def glacial(h, cell, inland, A, seed, ela=1350.0):
    """Alpine glaciers: ice routed down the drainage with a signed mass balance (terminus where the catchment balance reaches 0);
    depth and width scale with ice flux, so tributaries carve shallower beds than trunks (hanging valleys) and heads become
    cirques. Then an ice-sheet-age fjord pass overdeepens the big trunk valleys near the coast below sea level."""
    n = h.shape[0]
    outl = outlets_for(h)
    rec, order, filled = tv2.receivers(h, outl)
    ela_f = ela + 150.0 * tv2.noise(n, n, X0, X0, cell, 20000.0, octaves=3, seed=seed + 21)
    nz = tv2.normals(ndimage.gaussian_filter(filled, 3), cell)
    aspect_bias = -140.0 * nz[..., 2] + 60.0 * nz[..., 0]          # north (-z) / east faces hold more ice
    b = np.clip((filled - ela_f + aspect_bias) * 0.007, -10.0, 1.6)
    Q = tv2.f32((b * cell * cell).ravel())
    fl = np.empty(n * n, np.float32)
    tv2.K.ice_flux(order.ctypes.data_as(tv2.IP), rec.ctypes.data_as(tv2.IP), tv2.fp(Q), n * n, cell, tv2.fp(fl))
    flux = fl.reshape(h.shape)
    ice = flux > 1.5e5
    H = np.where(ice, np.clip(50.0 * (flux / 1.5e5) ** 0.3, 50.0, 520.0), 0.0).astype(np.float32)
    W = (2.2 * H + 50.0).astype(np.float32)
    bed = np.where(ice, filled - 0.45 * H, 1e9).astype(np.float32).ravel()
    msk = np.ascontiguousarray(ice.ravel().astype(np.uint8))
    tv2.K.monotone_bed(order.ctypes.data_as(tv2.IP), rec.ctypes.data_as(tv2.IP), tv2.u8p(msk), tv2.fp(bed), n * n, 0.01 * cell)
    bed = np.maximum(bed.reshape(h.shape), 4.0)
    out = carve_u(h, ice, H, W, bed, cell)
    log("alpine glaciers: %.1f%% of cells, cut max %.0f m" % (100 * ice.mean(), (h - out).max()))
    # fjords: the lower course of big rivers (along-flow distance to the sea < Lf) overdeepened below sea level, sill at the mouth
    ds = np.empty(n * n, np.float32)
    tv2.K.dist_downstream(order.ctypes.data_as(tv2.IP), rec.ctypes.data_as(tv2.IP), n * n, n, cell, tv2.fp(ds))
    ds = ds.reshape(h.shape)
    lf = 10000.0 + 2500.0 * tv2.noise(n, n, X0, X0, cell, 15000.0, octaves=2, seed=seed + 23)
    fj = (A > 6e7) & (ds < lf) & (ds > 0) & (h > -50.0) & (inland < lf + 4000.0)
    Hf = np.where(fj, np.clip(220.0 * (A / 6e7) ** 0.3, 220.0, 520.0), 0.0).astype(np.float32)
    Wf = (1.9 * Hf + 100.0).astype(np.float32)
    t = np.clip(ds / np.maximum(lf, 1.0), 0.0, 1.0)                 # 0 at the mouth, 1 at the fjord head
    depth = Hf * (0.18 + 0.5 * np.sin(np.pi * np.clip(0.15 + t, 0, 1))) * (1.0 - smoothstep(0.75, 1.0, t))
    bedf = np.where(fj, np.minimum(filled - 0.4 * Hf, -depth + filled.clip(0, None) * smoothstep(0.8, 1.0, t)), 1e9).astype(np.float32)
    out = carve_u(out, fj, Hf, Wf, bedf, cell)
    log("fjords: %.2f%% of cells" % (100 * fj.mean()))
    # below-sea pockets not connected to the open sea become lakes (raised to +2 m, later filled flat by the flow routing)
    pocket = (out < 0.0) & (outlets_for(out, border=False) == 0)
    out = np.where(pocket, 2.0, out).astype(np.float32)
    log("lake pockets filled: %.3f%% cells" % (100 * pocket.mean()))
    return out, flux, ice | fj


n2 = args.final_res
cell2 = MAP / n2
if n2 != n1:
    _, _, U2, kmul2, hard2, inland2, dC2, sC2, dT2 = tectonics(n2)
    h2 = tv2.resample(h1, (n2, n2))
else:
    U2, kmul2, hard2, inland2, dC2, sC2, dT2, h2 = U1, kmul1, hard1, inland1, dC1, sC1, dT1, h1.copy()
# drainage area on the current surface (stage-B areas stop at the pinned trunk, whose cells were sinks)
A2pre = np.empty_like(h2)
tv2.K.drainage_area(tv2.fp(h2), tv2.u8p(outlets_for(h2)), n2, n2, cell2, tv2.fp(A2pre))
h2g, flux, ice = glacial(h2, cell2, inland2, A2pre, SEED, args.ela)
tv2.save("stageC_pre", h=h2g, flux=flux)

# ------------------------------------------------------------------ stage D: post-glacial fluvial incision + talus at 19.5 m
det = (tv2.noise(n2, n2, X0, X0, cell2, 420.0, octaves=4, seed=SEED + 31, mode=1, warp=0.5, warp_scale=900.0) - 0.45)
relief = np.clip((h2g - ndimage.minimum_filter(h2g, size=int(1500 / cell2))) / 600.0, 0, 1)
h2g += (det * 28.0 * relief * (h2g > 5)).astype(np.float32)
out2 = outlets_for(h2g)
tal2 = talus_for(hard2, cell2)
tv2.K.set_flow(1.2, 1)  # post-glacial: keep glacial overdeepenings (no flat fill plates)
tv2.K.set_amax(2e6)     # post-glacial: gullies re-dissect the walls, trunk rivers must not re-incise a slot into the trough floors
A2 = tv2.stream_power(h2g, (U2 * UPS * 0.3).astype(np.float32), kmul2, out2, tal2, None, cell2, 24, 1.0, 0.0105 * 0.6, 0.5, 1, 0.4)
tv2.K.set_amax(1e30)
tv2.K.set_flow(1.2, 0)
log("stage D done: land h p50/p95/max %.0f/%.0f/%.0f" % tuple(np.percentile(h2g[h2g > 0], [50, 95, 100])))


# ------------------------------------------------------------------ route: the trunk river from the target basin to the sea
def trace_route(h, cell):
    outl = outlets_for(h)
    rec, order, filled = tv2.receivers(h, outl)
    n = h.shape[0]
    j = int(round((TARGET[1] - X0) / cell)); i = int(round((TARGET[0] - X0) / cell))
    # start at the lowest cell of the basin within 1.5 km of the target, then follow D8 receivers to the sea
    r = int(1500 / cell)
    blk = filled[j - r:j + r, i - r:i + r]
    jj, ii = np.unravel_index(np.argmin(blk), blk.shape)
    k = (j - r + jj) * n + (i - r + ii)
    pts = []
    while True:
        pts.append(k)
        nk = rec[k]
        if nk == k:
            break
        k = nk
    pts = np.array(pts)
    x = X0 + (pts % n) * cell; z = X0 + (pts // n) * cell
    return np.stack([x, z], 1), filled.ravel()[pts]


# de-hatching: a 1-cell Gaussian removes the 2-3 cell D8/talus stripes (keeps >= 8-cell landforms); the 4.9 m core adds non-grid detail
h2g = np.where(h2g > 0.0, ndimage.gaussian_filter(h2g, 1.0), h2g).astype(np.float32)
# islands / coastal hills outside the refined core: rocky knolls instead of smooth cones (ridged detail scaled with height)
isl_det = tv2.noise(n2, n2, X0, X0, cell2, 350.0, octaves=5, seed=SEED + 91, mode=1, warp=0.8, warp_scale=500.0) - 0.45
coastal = smoothstep(4000.0, -500.0, inland2) * smoothstep(0.0, 40.0, h2g)
h2g = (h2g + isl_det * 40.0 * coastal * np.clip(h2g / 150.0, 0, 1)).astype(np.float32)
route, floor = trace_route(h2g, cell2)
L = np.sum(np.linalg.norm(np.diff(route, axis=0), axis=1))
log("route: %d cells, %.1f km, floor %.0f -> %.0f m; ends at (%.0f, %.0f)" % (len(route), L / 1000, floor[0], floor[-1], route[-1, 0], route[-1, 1]))
tv2.save("world_%d" % n2, h=h2g, A=A2, kmul=kmul2, hard=hard2, flux=flux, ice=ice.astype(np.uint8), inland=inland2,
         route=route, route_floor=floor)
log("saved world_%d" % n2)
