# Places firs like Godot's forest.gd (8 m grid, masks_b.R density, treeline 1550 m, scale 0.55-1.1) inside a corridor around the canyon run; writes UE-space instances per 2 km cell.
import numpy as np, json, os, re
from PIL import Image
from scipy import ndimage
Image.MAX_IMAGE_PIXELS = None
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "game/assets/world")
OUT = os.path.join(ROOT, "UE_Test/Niepotrzebne/src/trees.json")
N = 8192; CELL = 8.0; TREELINE = 1550.0; CORRIDOR = float(os.environ.get("CORRIDOR", 1800))
h = -1024.0 + np.fromfile(os.path.join(SRC, "src/height.r16"), dtype="<u2").reshape(N, N).astype(np.float32) / 65535.0 * 5120.0
mb = np.asarray(Image.open(os.path.join(SRC, "terrain/masks_b.png")))[..., 0].astype(np.float32) / 255.0
src = open(os.path.join(ROOT, "game/scripts/world/world_layout.gd")).read()
P = np.array([[float(a), float(b), float(c)] for a, b, c in re.findall(r"Vector3\(([-\d.]+), ([-\d.]+), ([-\d.]+)\)", src.split("CANYON_PATH")[1])])
path = P[30:120]
x0, x1 = path[:, 0].min() - CORRIDOR, path[:, 0].max() + CORRIDOR
z0, z1 = path[:, 2].min() - CORRIDOR, path[:, 2].max() + CORRIDOR
gx = np.arange(np.floor(x0 / CELL), np.ceil(x1 / CELL)); gz = np.arange(np.floor(z0 / CELL), np.ceil(z1 / CELL))
GX, GZ = np.meshgrid(gx, gz)
rng = np.random.default_rng(1729)
jit = rng.random(GX.shape + (2,)) - 0.5
px = (GX + 0.5 + jit[..., 0] * 0.85) * CELL; pz = (GZ + 0.5 + jit[..., 1] * 0.85) * CELL
# corridor distance to the path polyline (dense samples)
dense = np.concatenate([np.linspace(path[i], path[i + 1], 20, endpoint=False) for i in range(len(path) - 1)])
from scipy.spatial import cKDTree
d, _ = cKDTree(dense[:, [0, 2]]).query(np.stack([px.ravel(), pz.ravel()], 1))
keep = d.reshape(px.shape) < CORRIDOR
px, pz = px[keep], pz[keep]
def samp(img, x, z, n):
    u = ((x + 40000) / (80000 / N) + 0.5) / N * n - 0.5; v = ((z + 40000) / (80000 / N) + 0.5) / N * n - 0.5
    return ndimage.map_coordinates(img, [v, u], order=1, mode="nearest")
dens = samp(mb, px, pz, mb.shape[0]); y = samp(h, px, pz, N)
r = rng.random((len(px), 2))
dens = dens * np.clip((TREELINE + 60 - y) / 200.0, 0, 1)
ok = (dens * 1.15 >= r[:, 0]) & (y >= 3.0)
# keep the flight corridor clear like Godot (floor width): skip trees within 45 m of the centreline on the floor
ok &= d.reshape(keep.shape)[keep] > 45.0
px, pz, y, rr = px[ok], pz[ok], y[ok], r[ok, 1]
sc = (0.55 + 0.55 * rr) * np.interp(y, [TREELINE - 500, TREELINE + 50], [1.0, 0.55])
yaw = rng.random(len(px)) * 360.0
print("trees", len(px), "area km2 %.0f" % (keep.sum() * CELL * CELL / 1e6))
cells = {}
for i in range(len(px)):
    k = "%d_%d" % (int(np.floor(px[i] / 2000)), int(np.floor(pz[i] / 2000)))
    cells.setdefault(k, []).append([round(float(px[i]) * 100, 1), round(float(pz[i]) * 100, 1), round(float(y[i] - 0.4) * 100, 1), round(float(yaw[i]), 1), round(float(sc[i]), 3)])
json.dump(cells, open(OUT, "w"))
print("cells", len(cells), "max per cell", max(len(v) for v in cells.values()))
