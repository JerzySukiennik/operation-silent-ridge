# Terrain v2 -> UE: Nanite GLB tiles + the C++ height lookup file (Content/Data/terrain.osrh, format OSR2).
# Lattice of 32 x 32 cells of 2.5 km. Each cell gets a grid step: 4.88 m near the flight corridor, 9.77 m in the rest of the
# playable core, 19.5 m around it, 39 m far away, none over open sea. Core cells become 2.5 km tiles, the rest 5 km tiles (2x2 cells).
# Heights are quantised to the same uint16 values in the meshes and in the lookup; edges next to a coarser neighbour are
# linearly interpolated (crack-free), and the lookup stores the edge-fixed samples, so crash/radar altitude match the render exactly.
import json, os, struct
import numpy as np
from scipy.spatial import cKDTree
import tv2
from tv2 import glb
from tv2 import X0, CELL2, CORE_CELL, CORE_X, CORE_Z, CORE_W, CORE_H

OUT = os.path.join(tv2.WORK, "tiles"); os.makedirs(OUT, exist_ok=True)
DATA = os.path.join(tv2.ROOT, "UE_Test", "SilentRidgeUE", "Content", "Data"); os.makedirs(DATA, exist_ok=True)
HMIN, HR = -1024.0, 5120.0
LAT = 32; LC = 2500.0; FN = int(round(LC / CORE_CELL))          # 512 fine nodes per lattice cell
W = tv2.load("world_4096"); C = tv2.load("core")
hg = W["h"].astype(np.float32); hc = C["h"].astype(np.float32)
q = lambda h: np.clip(np.round((h - HMIN) / HR * 65535.0), 0, 65535).astype(np.uint16)
dq = lambda v: (HMIN + v.astype(np.float64) * HR / 65535.0).astype(np.float32)
CI0 = int(round((CORE_X[0] - X0) / CORE_CELL)); CJ0 = int(round((CORE_Z[0] - X0) / CORE_CELL))   # fine index of the core origin
# keep the 19.5 m world consistent with the refined core (far tiles, bakes and masks read it)
gi0, gj0 = CI0 // 4, CJ0 // 4
hg[gj0:gj0 + CORE_H // 4, gi0:gi0 + CORE_W // 4] = hc[::4, ::4]
np.save(os.path.join(tv2.WORK, "h_global_final.npy"), hg)
NF = int(round(tv2.MAP / CORE_CELL))                           # 16384 fine nodes across the map


def height_nodes(I, J):
    """Quantised heights at fine-node indices (arrays). Core nodes read the 4.88 m core, others the 19.5 m world."""
    I = np.clip(I, 0, NF); J = np.clip(J, 0, NF)
    inc = (I >= CI0) & (I <= CI0 + CORE_W - 1) & (J >= CJ0) & (J <= CJ0 + CORE_H - 1)
    out = np.empty(I.shape, np.float32)
    out[inc] = hc[J[inc] - CJ0, I[inc] - CI0]
    gi = np.clip(np.round(I[~inc] / 4.0).astype(int), 0, hg.shape[1] - 1); gj = np.clip(np.round(J[~inc] / 4.0).astype(int), 0, hg.shape[0] - 1)
    out[~inc] = hg[gj, gi]
    return dq(q(out))


# ------------------------------------------------------------------ lattice steps
route = W["route"]
dense = np.concatenate([np.linspace(route[k], route[k + 1], 8, endpoint=False) for k in range(len(route) - 1)])
kd = cKDTree(dense)
step = np.zeros((LAT, LAT), int)                                # in fine nodes: 1, 2, 4, 8 ; 0 = no tile
incore = np.zeros((LAT, LAT), bool)
for j in range(LAT):
    for i in range(LAT):
        x0 = X0 + i * LC; z0 = X0 + j * LC
        core = CORE_X[0] <= x0 < CORE_X[1] and CORE_Z[0] <= z0 < CORE_Z[1]
        incore[j, i] = core
        I = np.arange(i * FN, (i + 1) * FN + 1, 8); J = np.arange(j * FN, (j + 1) * FN + 1, 8)
        II, JJ = np.meshgrid(I, J)
        if height_nodes(II, JJ).max() < -3.0:
            continue
        pts = np.array([[x0 + a * LC, z0 + b * LC] for a in (0, 0.25, 0.5, 0.75, 1) for b in (0, 0.25, 0.5, 0.75, 1)])
        d = kd.query(pts)[0].min()
        if core:
            step[j, i] = 1 if d < 3200 else (2 if d < 7500 else 4)
        else:
            step[j, i] = 4 if d < 22000 else 8
# 5 km outer tiles need one step for their 2x2 cells
for j in range(0, LAT, 2):
    for i in range(0, LAT, 2):
        if not incore[j, i]:
            s = step[j:j + 2, i:i + 2]
            if s.max() > 0:
                step[j:j + 2, i:i + 2] = s[s > 0].min()
print("\n".join("".join(".abcd"[[0, 1, 2, 4, 8].index(v)] for v in row) for row in step))


def nb_step(j, i):
    return step[j, i] if 0 <= j < LAT and 0 <= i < LAT else 0


cell_grids = {}                                                 # (j, i) -> quantised uint16 grid of that lattice cell
tiles = []
manifest = []; tris_total = 0
for j in range(LAT):
    for i in range(LAT):
        s = step[j, i]
        if s == 0:
            continue
        span = 1 if incore[j, i] else 2
        if not incore[j, i] and (i % 2 or j % 2):
            continue
        # tile node grid
        nn = span * FN // s + 1
        I = i * FN + np.arange(nn) * s; J = j * FN + np.arange(nn) * s
        II, JJ = np.meshgrid(I, J)
        H = height_nodes(II, JJ)
        # crack fix, per lattice segment of each edge
        m = FN // s                                            # nodes per lattice segment
        for seg in range(span):
            a, b = seg * m, (seg + 1) * m + 1
            for side, (nj, ni) in {"W": (j + seg, i - 1), "E": (j + seg, i + span), "N": (j - 1, i + seg), "S": (j + span, i + seg)}.items():
                ns = nb_step(nj, ni)
                if ns > s:
                    k = ns // s
                    e = H[a:b, 0] if side == "W" else H[a:b, -1] if side == "E" else H[0, a:b] if side == "N" else H[-1, a:b]
                    e[:] = np.interp(np.arange(len(e)), np.arange(0, len(e), k), e[::k])
        H = dq(q(H))
        for sj in range(span):
            for si in range(span):
                cell_grids[(j + sj, i + si)] = q(H[sj * m:(sj + 1) * m + 1, si * m:(si + 1) * m + 1])
        # normals from the (unfixed) neighbourhood at the same step
        Ie = np.r_[I[0] - s, I, I[-1] + s]; Je = np.r_[J[0] - s, J, J[-1] + s]
        He = height_nodes(*np.meshgrid(Ie, Je))
        He[1:-1, 1:-1] = H
        dx = (He[1:-1, 2:] - He[1:-1, :-2]) / (2 * s * CORE_CELL); dz = (He[2:, 1:-1] - He[:-2, 1:-1]) / (2 * s * CORE_CELL)
        nr = np.stack([-dx, np.ones_like(dx), -dz], -1); nr /= np.linalg.norm(nr, axis=-1, keepdims=True)
        ext = span * LC
        cx = X0 + i * LC + ext / 2; cz = X0 + j * LC + ext / 2
        XX, ZZ = np.meshgrid(np.arange(nn) * s * CORE_CELL - ext / 2, np.arange(nn) * s * CORE_CELL - ext / 2)
        pos = np.stack([XX, H, ZZ], -1).reshape(-1, 3); nr = nr.reshape(-1, 3)
        a0 = (np.arange(nn - 1)[None, :] + np.arange(nn - 1)[:, None] * nn).ravel()
        idx = np.stack([a0, a0 + nn, a0 + 1, a0 + 1, a0 + nn, a0 + nn + 1], 1).ravel()
        ep, en, ei = [], [], []
        base = len(pos)
        for e in [np.arange(nn) * nn, (nn - 1) + np.arange(nn) * nn, np.arange(nn), (nn - 1) * nn + np.arange(nn)]:
            top = pos[e]; bot = top.copy(); bot[:, 1] -= 20.0
            b0 = base + sum(len(x) for x in ep)
            ep.append(bot); en.append(nr[e])
            t0, t1 = e[:-1], e[1:]; b_0 = b0 + np.arange(len(e) - 1); b_1 = b0 + np.arange(1, len(e))
            ei.append(np.stack([t0, b_0, t1, t1, b_0, b_1, t0, t1, b_0, t1, b_1, b_0], 1).ravel())
        pos = np.concatenate([pos] + ep); nr = np.concatenate([nr] + en); idx = np.concatenate([idx] + ei)
        name = "SM_T2_%02d_%02d" % (i, j)
        glb(os.path.join(OUT, name + ".glb"), pos, nr, idx)
        tris_total += len(idx) // 3
        manifest.append({"name": name, "x": cx * 100, "y": cz * 100, "step_m": s * CORE_CELL, "tris": int(len(idx) // 3)})
json.dump(manifest, open(os.path.join(OUT, "manifest.json"), "w"), indent=0)
print("tiles", len(manifest), "tris %.1fM" % (tris_total / 1e6), {int(v): int((step == v).sum()) for v in np.unique(step)})

# ------------------------------------------------------------------ OSR2 lookup file
# header: "OSR2", lattice n (u32), lattice cell size m (f32), fine cell m (f32), hmin (f32), hrange (f32)
# table: n*n x (u32 step in fine nodes, 0 = none; u64 byte offset of the (FN/step+1)^2 u16 block)
blocks = []; table = []; off = 0
for j in range(LAT):
    for i in range(LAT):
        g = cell_grids.get((j, i))
        if g is None:
            table.append((0, 0)); continue
        table.append((int(step[j, i]), off)); blocks.append(g.tobytes()); off += g.nbytes
path = os.path.join(DATA, "terrain.osrh")
with open(path, "wb") as f:
    f.write(b"OSR2" + struct.pack("<Iffff", LAT, LC, CORE_CELL, HMIN, HR))
    for st, of in table:
        f.write(struct.pack("<IQ", st, of))
    for b in blocks:
        f.write(b)
print("wrote", path, "%.1f MB" % (os.path.getsize(path) / 1e6))
