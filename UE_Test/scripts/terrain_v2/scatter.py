# Terrain v2 scatter: firs and Nanite rock scans placed from the same masks the material uses, inside the flight corridor.
# Writes WORK/trees.json ({cell: [[x, y, z, yaw, s], ...]}, UE cm) and WORK/rocks.json ({mesh: [[x, y, z, fx, fy, fz, rx, ry, rz,
# ux, uy, uz, s], ...]}: location + images of the local X/Y/Z axes + uniform scale, UE space).
# Rock scans (open shells) are laid onto steep faces with their scanned face along the terrain normal, strata kept level-ish,
# and pushed into the slope by a third of their relief so their borders sink into the ground.
import json, os
import numpy as np
from scipy import ndimage
from scipy.spatial import cKDTree
import tv2
from tv2 import CORE_CELL, CORE_X, CORE_Z, smoothstep

rng = np.random.default_rng(1729)
C = tv2.load("core"); Wd = tv2.load("world_4096")
M = dict(np.load(os.path.join(tv2.WORK, "masks_core.npz")))
h = C["h"].astype(np.float32)
route = Wd["route"]
dense = np.concatenate([np.linspace(route[k], route[k + 1], 8, endpoint=False) for k in range(len(route) - 1)])
kd = cKDTree(dense)
gz, gx = np.gradient(ndimage.gaussian_filter(h, 1.0), CORE_CELL)
inv = 1.0 / np.sqrt(gx * gx + gz * gz + 1.0)
NX, NY, NZ = tv2.f32(-gx * inv), tv2.f32(inv), tv2.f32(-gz * inv)          # Godot-frame normal components (contiguous)
del gx, gz, inv


def smp(a, x, z, cell=CORE_CELL, order=1):
    return tv2.sample_world(a, cell, x, z, CORE_X[0], CORE_Z[0], order)


def mask(name, x, z):
    return smp(M[name].astype(np.float32), x, z, CORE_CELL * 2)


def jitter_grid(spacing, corridor):
    xs = np.arange(CORE_X[0] + spacing, CORE_X[1] - spacing, spacing); zs = np.arange(CORE_Z[0] + spacing, CORE_Z[1] - spacing, spacing)
    X, Z = np.meshgrid(xs, zs); X = X.ravel(); Z = Z.ravel()
    d, _ = kd.query(np.stack([X, Z], 1), distance_upper_bound=corridor + spacing)
    k = d < corridor
    X, Z = X[k], Z[k]
    X = X + (rng.random(len(X)) - 0.5) * spacing * 0.9; Z = Z + (rng.random(len(Z)) - 0.5) * spacing * 0.9
    d, _ = kd.query(np.stack([X, Z], 1))
    return X, Z, d


# ------------------------------------------------------------------ firs
TX, TZ, TD = jitter_grid(9.0, 1700.0)
y = smp(h, TX, TZ)
n_y = smp(NY, TX, TZ)
dens = mask("forest", TX, TZ) * smoothstep(0.72, 0.8, n_y)          # no trees on > ~40 deg ground
dens *= 1 - smoothstep(0.3, 0.6, mask("wet", TX, TZ))
ok = (rng.random(len(TX)) < dens * 1.1) & (y > 3.0) & (TD > 40.0)
TX, TZ, y = TX[ok], TZ[ok], y[ok]
tl = 1280.0
sc = (0.6 + 0.55 * rng.random(len(TX))) * np.interp(y, [tl - 450, tl + 60], [1.0, 0.55])
yaw = rng.random(len(TX)) * 360.0
cells = {}
for i in range(len(TX)):
    key = "%d_%d" % (int(np.floor(TX[i] / 2000)), int(np.floor(TZ[i] / 2000)))
    cells.setdefault(key, []).append([round(float(TX[i]) * 100, 1), round(float(TZ[i]) * 100, 1), round(float(y[i] - 0.4) * 100, 1),
                                      round(float(yaw[i]), 1), round(float(sc[i]), 3)])
json.dump(cells, open(os.path.join(tv2.WORK, "trees.json"), "w"))
print("trees", len(TX), "cells", len(cells), "max/cell", max(len(v) for v in cells.values()))

# ------------------------------------------------------------------ rocks
# scanned face direction (glTF) and relief depth (m) measured from the meshes; glTF (x, y, z) -> UE local (x, z, y)
SCANS = {"namaqualand_cliff_02": ((-0.07, 0.65, 0.76), 6.6), "rock_face_01": ((-0.03, 0.70, 0.71), 3.8),
         "rock_face_02": ((-0.11, 0.55, 0.83), 2.1), "coastal_cliff_02": ((-0.02, 0.27, 0.96), 8.6)}
g2u = lambda v: np.array([v[0], v[2], v[1]], float)


def frame(a, b):
    a = a / np.linalg.norm(a); b = b - a * a.dot(b); b /= np.linalg.norm(b)
    return np.stack([a, b, np.cross(a, b)], 1)                         # columns


rocks = {}


def place(name, X, Z, scale, embed_frac, spin_deg):
    f_loc, depth = SCANS[name]
    Lf = frame(g2u(f_loc), np.array([0.0, 0.0, 1.0]))
    out = rocks.setdefault(name, [])
    YV = smp(h, X, Z); NG = np.stack([smp(NX, X, Z), smp(NY, X, Z), smp(NZ, X, Z)], 1)
    for x, z, s, yv, ng in zip(X, Z, scale, YV, NG):
        nu = np.array([ng[0], ng[2], ng[1]])                              # UE (x, y, z_up)
        Wf = frame(nu, np.array([0.0, 0.0, 1.0]))
        R = Wf @ Lf.T
        a = np.radians((rng.random() - 0.5) * 2 * spin_deg)
        k = nu / np.linalg.norm(nu); K_ = np.array([[0, -k[2], k[1]], [k[2], 0, -k[0]], [-k[1], k[0], 0]])
        R = (np.eye(3) + np.sin(a) * K_ + (1 - np.cos(a)) * K_ @ K_) @ R
        p = np.array([x, z, yv]) - nu * depth * s * embed_frac
        out.append([round(p[0] * 100, 1), round(p[1] * 100, 1), round(p[2] * 100, 1)] + [round(v, 4) for v in R[:, 0]] +
                   [round(v, 4) for v in R[:, 1]] + [round(v, 4) for v in R[:, 2]] + [round(float(s), 3)])


# cliff faces: steep, hard rock within 1.6 km of the route
CX, CZ, CD = jitter_grid(34.0, 1600.0)
ny = smp(NY, CX, CZ)
hard = smp(C["hard"].astype(np.float32), CX, CZ)
p = smoothstep(0.8, 0.64, ny) * (0.35 + 0.65 * smoothstep(0.3, 0.8, hard)) * (smp(h, CX, CZ) > 5)
k = rng.random(len(CX)) < p * 0.8
CX, CZ = CX[k], CZ[k]
pick = rng.random(len(CX))
for name, lo, hi, smin, smax in [("namaqualand_cliff_02", 0.0, 0.45, 1.3, 2.8), ("rock_face_01", 0.45, 0.75, 4.0, 9.0), ("rock_face_02", 0.75, 1.01, 6.0, 13.0)]:
    sel = (pick >= lo) & (pick < hi)
    place(name, CX[sel], CZ[sel], smin + (smax - smin) * rng.random(sel.sum()) ** 1.5, 0.38, 25.0)
# sea cliffs along the coast of the core
SX, SZ, SD = jitter_grid(45.0, 12000.0)
ys = smp(h, SX, SZ); nys = smp(NY, SX, SZ)
near_sea = smp(ndimage.minimum_filter(h, 25), SX, SZ) < -1.0
k = (ys > -3) & (ys < 45) & (nys < 0.8) & near_sea & (rng.random(len(SX)) < 0.6)
place("coastal_cliff_02", SX[k], SZ[k], 1.0 + 1.6 * rng.random(k.sum()), 0.35, 12.0)
json.dump(rocks, open(os.path.join(tv2.WORK, "rocks.json"), "w"))

# boulders (closed meshes): scree fans and the valley floors near the river, random orientation
BX, BZ, BD = jitter_grid(22.0, 1400.0)
scr = mask("scree", BX, BZ); flo = smoothstep(0.985, 0.995, smp(NY, BX, BZ)) * smoothstep(250, 60, BD)
p = scr * 0.35 + flo * 0.04
k = (rng.random(len(BX)) < p) & (smp(h, BX, BZ) > 2)
BX, BZ = BX[k], BZ[k]
bould = {}
for name, s0, s1 in [("boulder_01", 1.6, 5.0), ("rock_09", 35.0, 90.0)]:
    sel = rng.random(len(BX)) < 0.5
    X, Z = BX[sel], BZ[sel]; BX, BZ = BX[~sel], BZ[~sel]
    L = []
    YB = smp(h, X, Z)
    for x, z, yb in zip(X, Z, YB):
        q = rng.normal(size=4); q /= np.linalg.norm(q); w, a, b, c = q
        R = np.array([[1 - 2 * (b * b + c * c), 2 * (a * b - c * w), 2 * (a * c + b * w)], [2 * (a * b + c * w), 1 - 2 * (a * a + c * c), 2 * (b * c - a * w)],
                      [2 * (a * c - b * w), 2 * (b * c + a * w), 1 - 2 * (a * a + b * b)]])
        s = s0 + (s1 - s0) * rng.random() ** 2
        yv = float(yb) - 0.25 * s * (1.0 if name == "boulder_01" else 0.05)
        L.append([round(x * 100, 1), round(z * 100, 1), round(yv * 100, 1)] + [round(v, 4) for v in R[:, 0]] + [round(v, 4) for v in R[:, 1]] +
                 [round(v, 4) for v in R[:, 2]] + [round(float(s), 3)])
    rocks[name] = L
json.dump(rocks, open(os.path.join(tv2.WORK, "rocks.json"), "w"))
print("rocks", {k: len(v) for k, v in rocks.items()})
