# Look-dev previews for terrain v2 without UE: shaded relief map with the route, and raymarched perspective "aerial photo" views
# (sun + shadows + sky occlusion + haze, simple slope/altitude colouring). Usage: preview.py <world npz name> [core npz name] [--views a,b]
import argparse, os
import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage
import tv2
from tv2 import X0, TO_SUN, smoothstep

ap = argparse.ArgumentParser()
ap.add_argument("world")
ap.add_argument("core", nargs="?")
ap.add_argument("--views", default="map,overview,fjord,entrance,gorge1,gorge2,target,spawn")
ap.add_argument("--out", default=os.path.join(tv2.WORK, "prev"))
ap.add_argument("--w", type=int, default=1280)
args = ap.parse_args()
os.makedirs(args.out, exist_ok=True)
W = tv2.load(args.world)
h = tv2.f32(W["h"]); n = h.shape[0]; cell = tv2.MAP / n
route = W["route"]
C = tv2.load(args.core) if args.core else None


def colours(h, cell, x0, z0, masks=None):
    """Linear albedo x lighting per cell (preview-only look)."""
    nrm = tv2.normals(h, cell)
    slope = 1.0 - nrm[..., 1]
    shade = np.clip(nrm @ TO_SUN, 0, 1) * tv2.sun_vis(h, cell, maxdist=15000.0)
    skyv = tv2.ao(h, cell, dirs=12, maxdist=2500.0)
    alt = h
    rows, cols = h.shape
    nz = tv2.noise(cols, rows, x0, z0, cell, 700.0, octaves=4, seed=91)
    # early-summer look: snow holds on gentle high ground and north faces, forest up to ~1300 m, meadows/scree above
    sline = 1750.0 + 160.0 * nz + 260.0 * nrm[..., 2]              # +z = south: sunny south faces melt higher
    snow = smoothstep(sline - 60, sline + 160, alt) * smoothstep(0.45, 0.28, slope)
    rock = smoothstep(0.24, 0.4, slope)
    tl = 1250.0 + 120.0 * nz + 120.0 * nrm[..., 2]
    forest = smoothstep(tl + 60, tl - 120, alt) * smoothstep(0.36, 0.24, slope) * (alt > 4)
    meadow = (1 - forest) * smoothstep(0.3, 0.15, slope) * smoothstep(tl + 500, tl, alt) * (alt > 4)
    beach = smoothstep(5.0, 1.0, alt) * (alt > -1)
    c = np.zeros(h.shape + (3,), np.float32)
    c[:] = np.array([0.16, 0.155, 0.15])                      # bare ground / scree
    c = c * (1 - meadow[..., None]) + np.array([0.09, 0.1, 0.05]) * meadow[..., None]
    c = c * (1 - forest[..., None]) + np.array([0.03, 0.042, 0.03]) * forest[..., None]
    c = c * (1 - rock[..., None]) + np.array([0.19, 0.19, 0.2]) * (0.8 + 0.3 * nz[..., None]) * rock[..., None]
    c = c * (1 - beach[..., None]) + np.array([0.35, 0.32, 0.26]) * beach[..., None]
    c = c * (1 - snow[..., None]) + np.array([0.8, 0.82, 0.86]) * snow[..., None]
    E = 2.2 * shade[..., None] * np.array([1.0, 0.96, 0.9]) + 0.55 * skyv[..., None] * np.array([0.55, 0.7, 1.0])
    return tv2.f32(c * E)


def tonemap(img):
    x = np.clip(img, 0, None) * 0.8
    a, b, c_, d, e = 2.51, 0.03, 2.43, 0.59, 0.14
    y = np.clip((x * (a * x + b)) / (x * (c_ * x + d) + e), 0, 1)
    return (np.power(y, 1 / 2.2) * 255).astype(np.uint8)


cg = colours(h, cell, X0, X0)
if C is not None:
    hc = tv2.f32(C["h"]); cc_ = colours(hc, tv2.CORE_CELL, tv2.CORE_X[0], tv2.CORE_Z[0])
else:
    hc = None


def render(name, cam, look, fov=62.0, w=args.w):
    H = int(w * 9 / 16)
    cam = np.array(cam, float); f = np.array(look, float) - cam; f /= np.linalg.norm(f)
    r = np.cross(f, [0, 1, 0]); r /= np.linalg.norm(r); u = np.cross(r, f)
    out = np.empty((H, w, 3), np.float32)
    a = lambda v: tv2.f32(np.asarray(v, np.float32))
    nullp = None
    tv2.K.render(tv2.fp(h), n, n, cell, X0, X0, tv2.fp(cg),
                 tv2.fp(hc) if hc is not None else nullp, hc.shape[1] if hc is not None else 0, hc.shape[0] if hc is not None else 0,
                 tv2.CORE_CELL, tv2.CORE_X[0], tv2.CORE_Z[0], tv2.fp(cc_) if hc is not None else nullp,
                 tv2.fp(a(cam)), tv2.fp(a(f)), tv2.fp(a(-r)), tv2.fp(a(u)), float(np.tan(np.radians(fov / 2))), w, H,
                 tv2.fp(a(TO_SUN)), 26000.0, tv2.fp(a([0.62, 0.72, 0.85])), tv2.fp(a([0.22, 0.42, 0.85])), tv2.fp(a([0.7, 0.8, 0.92])),
                 tv2.fp(a([0.01, 0.035, 0.05])), tv2.fp(out))
    Image.fromarray(tonemap(out)).save(os.path.join(args.out, name + ".jpg"), quality=90)
    print("view", name)


def hat(x, z):
    return float(max(tv2.sample_world(h, cell, x, z)[0], 0.0))


def along(frac, up=80.0, ahead=1500.0):
    """camera on the route at fraction frac (0 = target end, 1 = sea end), flying towards the target (up-canyon)."""
    d = np.r_[0, np.cumsum(np.linalg.norm(np.diff(route, axis=0), axis=1))]
    s = frac * d[-1]
    p = np.array([np.interp(s, d, route[:, 0]), np.interp(s, d, route[:, 1])])
    q = np.array([np.interp(max(s - ahead, 0), d, route[:, 0]), np.interp(max(s - ahead, 0), d, route[:, 1])])
    y = hat(*p) + up
    return [p[0], y, p[1]], [q[0], hat(*q) + up * 0.6, q[1]]


views = args.views.split(",")
if "map" in views:
    nrm = tv2.normals(h, cell)
    hs = np.clip(nrm @ np.array([-0.5, 0.7, -0.5]) / np.linalg.norm([-0.5, 0.7, -0.5]), 0, 1)
    base = np.clip(h / 2600.0, 0, 1)
    rgb = np.stack([0.35 + 0.5 * base, 0.4 + 0.45 * base, 0.35 + 0.4 * base], -1) * (0.35 + 0.75 * hs[..., None])
    rgb[h < 0] = np.array([0.1, 0.2, 0.35]) * (0.6 + 0.4 * np.clip(1 + h[h < 0] / 400, 0, 1))[..., None]
    im = Image.fromarray((np.clip(rgb, 0, 1) * 255).astype(np.uint8)).resize((2048, 2048), Image.LANCZOS)
    d = ImageDraw.Draw(im); s = 2048 / tv2.MAP
    d.line([((x - X0) * s, (z - X0) * s) for x, z in route[::4]], fill=(255, 60, 40), width=3)
    for (x, z), c in [((-31000, 9500), (255, 255, 0)), ((11500, -2600), (255, 0, 255))]:
        d.ellipse([(x - X0) * s - 8, (z - X0) * s - 8, (x - X0) * s + 8, (z - X0) * s + 8], outline=c, width=3)
    d.rectangle([(tv2.CORE_X[0] - X0) * s, (tv2.CORE_Z[0] - X0) * s, (tv2.CORE_X[1] - X0) * s, (tv2.CORE_Z[1] - X0) * s], outline=(255, 255, 255))
    im.save(os.path.join(args.out, "map.jpg"), quality=88)
    im.crop((int((tv2.CORE_X[0] - X0) * s), int((tv2.CORE_Z[0] - X0) * s), int((tv2.CORE_X[1] - X0) * s), int((tv2.CORE_Z[1] - X0) * s))).save(os.path.join(args.out, "map_core.jpg"), quality=90)
    print("view map")
if "overview" in views:
    m = route[int(len(route) * 0.8)]
    render("overview", [m[0] - 6000, 4200, m[1] + 9000], [m[0] + 14000, 700, m[1] - 6000])
if "spawn" in views:
    render("spawn", [-29500, 600, 9450], [-17500, 400, 9800])
for nm, fr, up in [("fjord", 0.93, 250.0), ("entrance", 0.75, 90.0), ("gorge1", 0.55, 80.0), ("gorge2", 0.35, 80.0), ("target", 0.12, 150.0)]:
    if nm in views:
        c, l = along(fr, up)
        render(nm, c, l)
