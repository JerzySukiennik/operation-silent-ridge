# Cuts the 8192^2 heightfield into 16x16 tiles of 5 km as GLB meshes for Nanite (full 9.77 m res near the canyon, 19.5 m elsewhere, 39 m far away; underwater-only tiles dropped; crack-free borders + skirts).
import numpy as np, json, os, re, struct
from scipy.spatial import cKDTree
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "game/assets/world/src")
OUT = os.path.join(ROOT, "UE_Test/Niepotrzebne/tiles")
os.makedirs(OUT, exist_ok=True)
N = 8192; CELL = 80000.0 / N; T = 16; TS = N // T  # 512 cells per tile
h = -1024.0 + np.fromfile(os.path.join(SRC, "height.r16"), dtype="<u2").reshape(N, N).astype(np.float32) / 65535.0 * 5120.0
hp = np.pad(h, ((0, 1), (0, 1)), mode="edge")  # sample index 8192 = edge
src = open(os.path.join(ROOT, "game/scripts/world/world_layout.gd")).read()
P = np.array([[float(a), float(b), float(c)] for a, b, c in re.findall(r"Vector3\(([-\d.]+), ([-\d.]+), ([-\d.]+)\)", src.split("CANYON_PATH")[1])])
kd = cKDTree(P[20:125][:, [0, 2]])
FULL_R = float(os.environ.get("FULL_R", 3500)); FAR_R = 25000.0
def tile_dist(i, j):
    # distance from canyon path to the tile rectangle (approx: min over 9 sample points)
    xs = -40000 + np.array([i, i + 0.5, i + 1]) * TS * CELL; zs = -40000 + np.array([j, j + 0.5, j + 1]) * TS * CELL
    pts = np.array([[x, z] for x in xs for z in zs]); d, _ = kd.query(pts)
    # rectangle vs point: check if any path point inside
    inside = ((P[20:125, 0] >= xs[0]) & (P[20:125, 0] <= xs[2]) & (P[20:125, 2] >= zs[0]) & (P[20:125, 2] <= zs[2])).any()
    return 0.0 if inside else d.min() - TS * CELL * 0.5
step = np.zeros((T, T), int)
for j in range(T):
    for i in range(T):
        blk = hp[j * TS:(j + 1) * TS + 1, i * TS:(i + 1) * TS + 1]
        if blk.max() < -3.0:
            step[j, i] = 0; continue
        d = tile_dist(i, j)
        step[j, i] = 1 if d < FULL_R else (2 if d < FAR_R else 4)

def glb(path, pos, nrm, idx):
    pos = pos.astype("<f4"); nrm = nrm.astype("<f4"); idx = idx.astype("<u4")
    bin_ = pos.tobytes() + nrm.tobytes() + idx.tobytes()
    pad = (4 - len(bin_) % 4) % 4; bin_ += b"\0" * pad
    n = len(pos)
    js = {"asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0, "name": os.path.basename(path)[:-4]}],
          "meshes": [{"name": os.path.basename(path)[:-4], "primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1}, "indices": 2, "material": 0}]}],
          "materials": [{"name": "M_TerrainSlot"}],
          "buffers": [{"byteLength": len(bin_)}],
          "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": pos.nbytes, "target": 34962},
                          {"buffer": 0, "byteOffset": pos.nbytes, "byteLength": nrm.nbytes, "target": 34962},
                          {"buffer": 0, "byteOffset": pos.nbytes + nrm.nbytes, "byteLength": idx.nbytes, "target": 34963}],
          "accessors": [{"bufferView": 0, "componentType": 5126, "count": n, "type": "VEC3", "min": pos.min(0).tolist(), "max": pos.max(0).tolist()},
                        {"bufferView": 1, "componentType": 5126, "count": n, "type": "VEC3"},
                        {"bufferView": 2, "componentType": 5125, "count": len(idx), "type": "SCALAR"}]}
    j = json.dumps(js).encode(); j += b" " * ((4 - len(j) % 4) % 4)
    with open(path, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(j) + 8 + len(bin_)))
        f.write(struct.pack("<II", len(j), 0x4E4F534A)); f.write(j)
        f.write(struct.pack("<II", len(bin_), 0x004E4942)); f.write(bin_)

manifest = []; total_tris = 0
for j in range(T):
    for i in range(T):
        s = step[j, i]
        if s == 0:
            continue
        r = np.arange(0, TS + 1, s)
        gi = i * TS + r; gj = j * TS + r
        H = hp[np.ix_(gj, gi)].copy()
        # crack-free borders: match coarser neighbours by linear interpolation along the shared edge
        def nb(jj, ii):
            return step[jj, ii] if 0 <= jj < T and 0 <= ii < T else 0
        for side, ns in [("W", nb(j, i - 1)), ("E", nb(j, i + 1)), ("N", nb(j - 1, i)), ("S", nb(j + 1, i))]:
            if ns > s:
                k = ns // s
                edge = H[:, 0] if side == "W" else H[:, -1] if side == "E" else H[0, :] if side == "N" else H[-1, :]
                coarse = edge[::k]
                edge[:] = np.interp(np.arange(len(edge)), np.arange(0, len(edge), k), coarse)
        n = len(r)
        # normals from central differences on the tile grid (edge-clamped from the global field)
        gi2 = np.clip(np.r_[gi[0] - s, gi, gi[-1] + s], 0, N); gj2 = np.clip(np.r_[gj[0] - s, gj, gj[-1] + s], 0, N)
        Hx = hp[np.ix_(gj2, gi2)]
        dx = (Hx[1:-1, 2:] - Hx[1:-1, :-2]) / (2 * s * CELL); dz = (Hx[2:, 1:-1] - Hx[:-2, 1:-1]) / (2 * s * CELL)
        nrm = np.stack([-dx, np.ones_like(dx), -dz], -1); nrm /= np.linalg.norm(nrm, axis=-1, keepdims=True)
        cx = -40000 + (i + 0.5) * TS * CELL; cz = -40000 + (j + 0.5) * TS * CELL
        X, Z = np.meshgrid(r * CELL - TS * CELL * 0.5, r * CELL - TS * CELL * 0.5)
        pos = np.stack([X, H, Z], -1).reshape(-1, 3); nr = nrm.reshape(-1, 3)
        a = (np.arange(n - 1)[None, :] + np.arange(n - 1)[:, None] * n).ravel()
        # glTF CCW seen from +Y: (a, a+n, a+1), (a+1, a+n, a+n+1)
        idx = np.stack([a, a + n, a + 1, a + 1, a + n, a + n + 1], 1).ravel()
        # skirts: 20 m curtain under each edge
        extra_p, extra_n, extra_i = [], [], []
        base = len(pos)
        for e in [np.arange(n), (n - 1) * n + np.arange(n)[::-1], np.arange(n)[::-1] * n, np.arange(n) * n + n - 1]:
            pass
        loops = [np.arange(n) * n, (n - 1) + np.arange(n) * n, np.arange(n), (n - 1) * n + np.arange(n)]
        for e in loops:
            top = pos[e]; bot = top.copy(); bot[:, 1] -= 20.0
            b0 = base + sum(len(x) for x in extra_p)
            extra_p.append(bot); extra_n.append(nr[e])
            m = len(e)
            t0 = e[:-1]; t1 = e[1:]; b_0 = b0 + np.arange(m - 1); b_1 = b0 + np.arange(1, m)
            # both windings so the curtain is visible from either side
            extra_i.append(np.stack([t0, b_0, t1, t1, b_0, b_1, t0, t1, b_0, t1, b_1, b_0], 1).ravel())
        pos = np.concatenate([pos] + extra_p); nr = np.concatenate([nr] + extra_n); idx = np.concatenate([idx] + extra_i)
        name = "SM_Tile_%02d_%02d" % (i, j)
        glb(os.path.join(OUT, name + ".glb"), pos, nr, idx)
        total_tris += len(idx) // 3
        manifest.append({"name": name, "x": cx * 100, "y": cz * 100, "step": int(s), "tris": int(len(idx) // 3)})
json.dump(manifest, open(os.path.join(OUT, "manifest.json"), "w"), indent=0)
print("tiles", len(manifest), "full", int((step == 1).sum()), "half", int((step == 2).sum()), "quarter", int((step == 4).sum()), "skipped", int((step == 0).sum()), "tris %.1fM" % (total_tris / 1e6))
print("\n".join("".join(".124"[[0, 1, 2, 4].index(v)] if v in (0, 1, 2, 4) else "?" for v in row) for row in step))
