# Offline lighting bake for the fixed sun (Lumen replacement): cosine-weighted sky visibility + one-bounce terrain irradiance (RGB, relative to sun irradiance) from horizon marching on the heightfield. Output: Niepotrzebne/src/bake_2048.png (RGB bounce x4, A sky visibility).
import numpy as np, os, json
from PIL import Image
from scipy import ndimage
Image.MAX_IMAGE_PIXELS = None
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "game/assets/world")
OUT = os.path.join(ROOT, "UE_Test/Niepotrzebne/src")
N = 8192; R = int(os.environ.get("BAKE_RES", 2048)); CELL = 80000.0 / R
DIRS = int(os.environ.get("BAKE_DIRS", 24))
h = -1024.0 + np.fromfile(os.path.join(SRC, "src/height.r16"), dtype="<u2").reshape(N, N).astype(np.float32) / 65535.0 * 5120.0
k = N // R
H = h.reshape(R, k, R, k).mean(axis=(1, 3))
ma = np.asarray(Image.open(os.path.join(SRC, "terrain/masks_a.png")).resize((R, R), Image.BILINEAR)).astype(np.float32) / 255.0
mb = np.asarray(Image.open(os.path.join(SRC, "terrain/masks_b.png")).resize((R, R), Image.BILINEAR)).astype(np.float32) / 255.0
# grid axes: col = +x (east), row = +z (south); Godot sun dir (x, y_up, z)
S = np.array([-0.26200, 0.64279, 0.71985])
gz, gx = np.gradient(H, CELL)
n = np.stack([-gx, np.ones_like(H), -gz], -1); n /= np.linalg.norm(n, axis=-1, keepdims=True)
ndl = np.clip(n @ S, 0, 1)
sunvis = ma[..., 1]
slope = 1 - n[..., 1]
ss = lambda a, b, x: np.clip((x - a) / (b - a), 0, 1) ** 2 * (3 - 2 * np.clip((x - a) / (b - a), 0, 1))
snow = ss(420, 740, H) * ss(0.45, 0.3, slope)
forest = mb[..., 0] * ss(1630, 1350, H) * (1 - snow * 0.5)
rock = np.clip(1 - snow - forest, 0, 1)
alb = (snow[..., None] * np.array([0.8, 0.82, 0.86]) + forest[..., None] * np.array([0.035, 0.05, 0.04])
       + rock[..., None] * np.array([0.24, 0.24, 0.25]))
water = H < 0.5
alb[water] = np.array([0.03, 0.05, 0.06])
Lout = alb * (ndl * sunvis)[..., None]            # radiance * pi / E_sun of every texel
Hw = np.maximum(H, 0.0)                            # the ocean surface occludes at sea level
# horizon march
dists = np.unique(np.round(np.geomspace(1, 7000.0 / CELL, 44)).astype(int))
P = int(dists.max()) + 2
Hp = np.pad(Hw, P, mode="edge"); Lp = np.pad(Lout, ((P, P), (P, P), (0, 0)), mode="edge")
sky = np.zeros((R, R), np.float32); bounce = np.zeros((R, R, 3), np.float32)
for di in range(DIRS):
    a = 2 * np.pi * (di + 0.5) / DIRS
    dx, dz = np.cos(a), np.sin(a)
    best = np.full((R, R), -1e9, np.float32)          # max tan(elevation)
    Lh = np.zeros((R, R, 3), np.float32)
    for d in dists:
        ox, oz = int(round(dx * d)), int(round(dz * d))
        dist = np.hypot(ox, oz) * CELL
        if dist == 0:
            continue
        sh = Hp[P + oz:P + oz + R, P + ox:P + ox + R]
        t = (sh - Hw) / dist
        upd = t > best
        best = np.where(upd, t, best)
        Lh = np.where(upd[..., None], Lp[P + oz:P + oz + R, P + ox:P + ox + R], Lh)
    el = np.arctan(np.maximum(best, 0.0))
    # tilt: horizon relative to the local tangent plane in this azimuth
    tan_plane = -(gx * dx + gz * dz)                  # slope of the surface itself along the direction
    el_plane = np.arctan(tan_plane)
    el_eff = np.maximum(el, el_plane)
    sky += np.cos(el_eff) ** 2
    bounce += Lh * (np.sin(el_eff) ** 2 - np.sin(np.maximum(el_plane, 0)) ** 2).clip(0)[..., None]
    print("dir", di, flush=True)
sky /= DIRS; bounce /= DIRS
sky *= (1 + n[..., 1]) / 2 + 0.0
sky = np.clip(sky / np.percentile(sky[~water], 99.5), 0, 1)
print("sky vis p5/p50/p95", np.percentile(sky, [5, 50, 95]).round(3), "bounce p50/p95/max", np.percentile(bounce.mean(-1), [50, 95]).round(4), bounce.max().round(3))
enc = np.dstack([np.clip(np.sqrt(np.clip(bounce * 4.0, 0, 1)) * 255, 0, 255), np.clip(sky * 255, 0, 255)]).astype(np.uint8)
Image.fromarray(enc, "RGBA").save(os.path.join(OUT, "bake_%d.png" % R))
Image.fromarray((np.clip(sky, 0, 1) * 255).astype(np.uint8)).save(os.path.join(OUT, "bake_sky_preview.png"))
Image.fromarray(np.clip(np.sqrt(bounce * 4) * 255, 0, 255).astype(np.uint8)).save(os.path.join(OUT, "bake_bounce_preview.png"))
print("wrote bake")
