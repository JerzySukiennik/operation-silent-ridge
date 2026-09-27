# Builds the 64 s auto-flight (ocean spawn -> canyon run at ~215 m/s, 60-90 m AGL -> overview) and chase-camera keys; writes UE-space keys to flight_keys.json.
import numpy as np, json, os, re
from scipy import ndimage
from scipy.interpolate import CubicSpline
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "game/assets/world/src")
OUT = os.path.join(ROOT, "UE_Test/Niepotrzebne/src/flight_keys.json")
N = 8192
h = -1024.0 + np.fromfile(os.path.join(SRC, "height.r16"), dtype="<u2").reshape(N, N).astype(np.float32) / 65535.0 * 5120.0

def height(x, z):
    # Godot HeightField convention: sample i at -40 km + i * 80 km / 8192
    u = (np.asarray(x) + 40000.0) / 80000.0 * N
    v = (np.asarray(z) + 40000.0) / 80000.0 * N
    return ndimage.map_coordinates(h, [np.atleast_1d(v), np.atleast_1d(u)], order=1, mode="nearest")

src = open(os.path.join(ROOT, "game/scripts/world/world_layout.gd")).read()
P = np.array([[float(a), float(b), float(c)] for a, b, c in re.findall(r"Vector3\(([-\d.]+), ([-\d.]+), ([-\d.]+)\)", src.split("CANYON_PATH")[1])])
CARRIER = np.array([-31000.0, 0.0, 9500.0]); MOUTH = P[0]
V = 215.0; FPS = 30
T_SPAWN, T_CANYON, T_OVER = 8.0, 50.0, 6.0

# ---- canyon: spline through the centreline (xz), start 2 points before the first dry floor (as the Godot autotest)
i0 = next(k for k in range(len(P)) if height(P[k, 0], P[k, 2])[0] > 5.0) - 2
seg = np.r_[0, np.cumsum(np.linalg.norm(np.diff(P[:, [0, 2]], axis=0), axis=1))]
sx = CubicSpline(seg, P[:, 0]); sz = CubicSpline(seg, P[:, 2])
s0 = seg[i0]
tc = np.arange(0, T_CANYON + 1e-6, 1.0 / FPS)
s = s0 + V * tc
# resample by arc length of the smoothed curve (keeps ground speed ~V)
fine = np.linspace(s0, s0 + V * T_CANYON * 1.1, 20000)
fx, fz = sx(fine), sz(fine)
arc = np.r_[0, np.cumsum(np.hypot(np.diff(fx), np.diff(fz)))]
cx = np.interp(V * tc, arc, fx); cz = np.interp(V * tc, arc, fz)
floor = np.maximum(height(cx, cz), 0.0)
# look-ahead max floor over 1.6 s (autotest style), then smooth -> target ~70 m AGL
look = np.array([floor[k:k + int(1.6 * FPS)].max() for k in range(len(floor))])
alt = ndimage.gaussian_filter1d(look, 1.2 * FPS, mode="nearest") + 72.0
alt[: int(2 * FPS)] = np.linspace(floor[0] + 90.0, alt[int(2 * FPS)], int(2 * FPS))  # starts 90 m AGL like the autotest
canyon = np.stack([cx, alt, cz], 1)

# ---- ocean spawn: Godot spawn point 0, level at 600 m, towards the canyon mouth
to_coast = MOUTH - CARRIER; to_coast[1] = 0; to_coast /= np.linalg.norm(to_coast)
right = np.cross(to_coast, [0, 1, 0]); right /= np.linalg.norm(right)
sp = CARRIER + to_coast * 1500.0 + right * (-180.0); sp[1] = 600.0
ts = np.arange(0, T_SPAWN, 1.0 / FPS)
spawn = sp[None, :] + to_coast[None, :] * V * ts[:, None]

def frames(pos):
    """jet forward/up from the trajectory: up = normalised (a + g) (coordinated flight)."""
    dt = 1.0 / FPS
    vel = np.gradient(pos, dt, axis=0); vel = ndimage.gaussian_filter1d(vel, 3, axis=0, mode="nearest")
    acc = np.gradient(vel, dt, axis=0); acc = ndimage.gaussian_filter1d(acc, 6, axis=0, mode="nearest")
    f = vel / np.linalg.norm(vel, axis=1, keepdims=True)
    lift = acc + np.array([0, 9.81, 0])
    lift -= f * np.sum(lift * f, 1, keepdims=True)
    u = lift / np.linalg.norm(lift, axis=1, keepdims=True)
    g = np.linalg.norm(acc + np.array([0, 9.81, 0]), axis=1) / 9.81
    return f, u, g

def to_ue(v):  # Godot (x east, y up, z south) metres -> UE (X east, Y south, Z up)
    v = np.asarray(v); return np.stack([v[..., 0], v[..., 2], v[..., 1]], -1)

def rotator(F, U):
    yaw = np.degrees(np.arctan2(F[:, 1], F[:, 0]))
    pitch = np.degrees(np.arctan2(F[:, 2], np.hypot(F[:, 0], F[:, 1])))
    Y = np.radians(yaw); Pr = np.radians(pitch)
    Y0 = np.stack([-np.sin(Y), np.cos(Y), 0 * Y], 1)
    Z0 = np.stack([-np.sin(Pr) * np.cos(Y), -np.sin(Pr) * np.sin(Y), np.cos(Pr)], 1)
    roll = np.degrees(np.arctan2(np.sum(U * Y0, 1), np.sum(U * Z0, 1)))
    return np.stack([np.unwrap(np.radians(roll)), np.radians(pitch), np.unwrap(np.radians(yaw))], 1) * 180 / np.pi

def chase(pos, f, u):
    """Godot ChaseCamera: frame slerps to the jet attitude at k=7/s; cam = pivot + frame*(0, 3.4, +19); aim = pivot + up*1.6."""
    dt = 1.0 / FPS; k = 1 - np.exp(-dt * 7.0)
    lf, lu = f[0].copy(), u[0].copy(); cams, cf, cu = [], [], []
    for i in range(len(pos)):
        lf = lf + (f[i] - lf) * k; lf /= np.linalg.norm(lf)
        lu = lu + (u[i] - lu) * k; lu -= lf * lu.dot(lf); lu /= np.linalg.norm(lu)
        c = pos[i] - lf * 19.0 + lu * 3.4
        c[1] = max(c[1], height(c[0], c[2])[0] + 2.5, 1.0)
        aim = pos[i] + lu * 1.6
        d = aim - c; d /= np.linalg.norm(d)
        up = lu - d * lu.dot(d); up /= np.linalg.norm(up)
        cams.append(c); cf.append(d); cu.append(up)
    return np.array(cams), np.array(cf), np.array(cu)

out = {"fps": FPS, "shots": []}
t0 = 0.0
for name, pos, dur in [("spawn", spawn, T_SPAWN), ("canyon", canyon, T_CANYON)]:
    f, u, g = frames(pos)
    c, cf, cu = chase(pos, f, u)
    jr = rotator(to_ue(f), to_ue(u)); cr = rotator(to_ue(cf), to_ue(cu))
    agl = pos[:, 1] - np.maximum(height(pos[:, 0], pos[:, 2]), 0)
    print(name, "frames", len(pos), "AGL min/med/max %.0f/%.0f/%.0f" % (agl.min(), np.median(agl), agl.max()),
          "max G %.1f" % g.max(), "max bank %.0f" % np.abs(jr[:, 0] - 0).max(), "dist km %.1f" % (np.sum(np.linalg.norm(np.diff(pos, axis=0), axis=1)) / 1000))
    out["shots"].append({"name": name, "start": t0, "dur": dur,
        "jet_loc": (to_ue(pos) * 100).round(1).tolist(), "jet_rot": jr.round(3).tolist(),
        "cam_loc": (to_ue(c) * 100).round(1).tolist(), "cam_rot": cr.round(3).tolist()})
    t0 += dur
# overview: Godot hero "overview_4000m": pos = mouth(300 m in) + (-6000, 4000, 9000), look at mouth + (14000, 600, -6000); slow 6 s push
m300 = P[0] + (P[2] - P[0]) * 0.75
a = m300 + np.array([-6000, 4000, 9000.0]); b = a + np.array([900, -150, -600.0]); look = m300 + np.array([14000, 600, -6000.0])
to = np.arange(0, T_OVER, 1.0 / FPS)
cp = a[None] + (b - a)[None] * (to / T_OVER)[:, None]
d = look[None] - cp; d /= np.linalg.norm(d, axis=1, keepdims=True)
upv = np.array([0, 1.0, 0])[None] - d * d[:, 1:2]; upv /= np.linalg.norm(upv, axis=1, keepdims=True)
out["shots"].append({"name": "overview", "start": t0, "dur": T_OVER, "jet_loc": None, "jet_rot": None,
    "cam_loc": (to_ue(cp) * 100).round(1).tolist(), "cam_rot": rotator(to_ue(d), to_ue(upv)).round(3).tolist()})
out["total"] = t0 + T_OVER
json.dump(out, open(OUT, "w"))
print("total", out["total"], "->", OUT)
