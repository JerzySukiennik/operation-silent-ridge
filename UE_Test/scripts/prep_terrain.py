# Converts the Godot 8192^2 R16 heightfield into a UE landscape heightmap (8161^2 = 32x32 components of 255 quads) plus helper textures.
import numpy as np, json, sys, os
from PIL import Image
from scipy import ndimage
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "game/assets/world/src")
OUT = os.path.join(ROOT, "UE_Test/Niepotrzebne/src")
os.makedirs(OUT, exist_ok=True)
meta = json.load(open(os.path.join(SRC, "height.json")))
N = meta["size"]
h16 = np.fromfile(os.path.join(SRC, "height.r16"), dtype="<u2").reshape(N, N)
hmin, hmax = meta["hmin"], meta["hmax"]
h = hmin + h16.astype(np.float32) / 65535.0 * (hmax - hmin)
print("height m: min %.1f max %.1f" % (h.min(), h.max()))
M = 8161
c = np.minimum(np.arange(M) * (N / (M - 1.0)), N - 1).astype(np.float32)  # Godot sample i sits at x = -40 km + i * 80 km / 8192
yy, xx = np.meshgrid(c, c, indexing="ij")
hr = ndimage.map_coordinates(h, [yy, xx], order=1)
# UE landscape: world_cm = (v - 32768) * zscale / 128 ; zscale = 1000 -> 7.8125 cm per step, actor z = +1536 m
v = np.clip(np.round((hr - 1536.0) / 0.078125 + 32768.0), 0, 65535).astype("<u2")
v.tofile(os.path.join(OUT, "height_8161.r16"))
Image.fromarray(v.astype(np.uint16)).save(os.path.join(OUT, "height_8161.png"))
print("wrote", M, "max err m", np.abs((v.astype(np.float32) - 32768) * 0.078125 + 1536.0 - hr).max())
# ocean depth texture 4096^2 (19.5 m texels): sqrt-encoded metres below sea level (0..255 m) for shore precision
d = h.reshape(4096, 2, 4096, 2).mean(axis=(1, 3))
dep = np.round(np.sqrt(np.clip(-d, 0, 255) / 255.0) * 255.0).astype(np.uint8)
Image.fromarray(dep).save(os.path.join(OUT, "ocean_depth.png"))
np.save(os.path.join(OUT, "height_1024.npy"), ndimage.zoom(h, 1024 / N, order=1))
