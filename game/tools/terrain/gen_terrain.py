#!/usr/bin/env python3
# Seeded offline generator for the 80x80 km world: stream-power + droplet + thermal erosion, fjord coast, carved canyon, baked masks.
"""
Regenerate (Mac, ~10 min at full size):
    python3 game/tools/terrain/gen_terrain.py            # full 8192^2 build, writes assets + layout
    python3 game/tools/terrain/gen_terrain.py --size 2048 --preview-only   # fast look-dev
Then the raw height is packed into a Godot resource (done automatically when Godot is found, in a scratch mini project):
    Godot --headless --path <empty project> --script game/tools/terrain/pack_height.gd -- <height.r16> <size> <height.res>

Outputs
    game/assets/world/src/height.r16            raw uint16 LE, SIZE^2, h = HMIN + v/65535*(HMAX-HMIN)  (source, not exported)
    game/assets/world/terrain/height.res        Godot Image FORMAT_R16 (made by pack_height.gd)
    game/assets/world/terrain/masks_a.png       R ambient occlusion, G sun visibility, B log flow, A sediment/scree
    game/assets/world/terrain/masks_b.png       R forest density, G curvature (0.5 = flat), B water, A rock hardness
    game/scripts/world/world_layout.gd          constants: map, sun, canyon path, target, carrier, spawns
Requires numpy, scipy, Pillow and a C compiler (kernels.c is built on demand).
"""
import argparse
import ctypes
import json
import math
import os
import subprocess
import sys
import time

import numpy as np
from PIL import Image
from scipy import ndimage

HERE = os.path.dirname(os.path.abspath(__file__))
GAME = os.path.normpath(os.path.join(HERE, "..", ".."))
MAP = 80000.0
HALF = MAP / 2
HMIN, HMAX = -1024.0, 4096.0
SUN_ELEV_DEG = 40.0
SUN_AZIMUTH_DEG = 200.0  # compass bearing towards the sun (from north, clockwise): SSW
FLOAT_P = ctypes.POINTER(ctypes.c_float)
U8_P = ctypes.POINTER(ctypes.c_uint8)

# ---------------------------------------------------------------- layout
# Canyon control points (x east, z south, metres). The run starts in a fjord at the coast and ends in the target valley.
CANYON_CTRL = [
    (-17500, 9800), (-14800, 9000), (-12400, 9900), (-10000, 8600), (-8000, 6000),
    (-5600, 5000), (-3300, 6300), (-1200, 5400), (400, 2900), (2600, 2200),
    (4800, 3200), (6600, 1600), (7600, -700), (9600, -1900),
]
TARGET = (12400.0, -2700.0)          # centre of the target valley floor
TARGET_FLOOR = 880.0                 # m
TARGET_RADIUS = 1500.0               # rough radius used for masks
TARGET_AXIS = (0.83, -0.56)          # valley trough direction (continues the canyon's last leg)
TARGET_HALF_LEN = 3200.0
TARGET_HALF_WIDTH = 1100.0
FJORD_LEN = 8500.0                   # first metres of the canyon are sea (fjord)
CARRIER = (-31000.0, 9500.0)
CARRIER_HEADING_DEG = 250.0          # ship sails into the wind (WSW)


def log(*a):
    print(time.strftime("%H:%M:%S"), *a, flush=True)


# ---------------------------------------------------------------- native kernels
def load_kernels(build_dir):
    os.makedirs(build_dir, exist_ok=True)
    src = os.path.join(HERE, "kernels.c")
    lib = os.path.join(build_dir, "libkernels" + (".dylib" if sys.platform == "darwin" else ".so"))
    if not os.path.exists(lib) or os.path.getmtime(lib) < os.path.getmtime(src):
        subprocess.check_call(["cc", "-O3", "-shared", "-fPIC", "-o", lib, src, "-lpthread", "-lm"])
    k = ctypes.CDLL(lib)
    return k


def fp(a):
    assert a.dtype == np.float32 and a.flags["C_CONTIGUOUS"]
    return a.ctypes.data_as(FLOAT_P)


class Gen:
    def __init__(self, seed, build_dir):
        self.seed = seed
        self.k = load_kernels(build_dir)
        self.k.fbm.argtypes = [FLOAT_P, ctypes.c_int, ctypes.c_int, ctypes.c_double, ctypes.c_double, ctypes.c_double,
                               ctypes.c_double, ctypes.c_int, ctypes.c_float, ctypes.c_float, ctypes.c_uint32,
                               ctypes.c_int, ctypes.c_float, ctypes.c_double]
        self.k.fbm_points.argtypes = [FLOAT_P, FLOAT_P, FLOAT_P, ctypes.c_long, ctypes.c_double, ctypes.c_int,
                                      ctypes.c_float, ctypes.c_float, ctypes.c_uint32, ctypes.c_int]
        self.k.stream_power.argtypes = [FLOAT_P, FLOAT_P, FLOAT_P, U8_P, FLOAT_P, ctypes.c_int, ctypes.c_int,
                                        ctypes.c_float, ctypes.c_int, ctypes.c_float, ctypes.c_float, ctypes.c_float,
                                        ctypes.c_int, FLOAT_P]
        self.k.thermal.argtypes = [FLOAT_P, FLOAT_P, ctypes.c_int, ctypes.c_int, ctypes.c_float, ctypes.c_int,
                                   ctypes.c_float, FLOAT_P]
        self.k.droplets.argtypes = [FLOAT_P, ctypes.c_int, ctypes.c_int, ctypes.c_float, ctypes.c_long, ctypes.c_uint32,
                                    ctypes.c_int, ctypes.c_float, ctypes.c_float, ctypes.c_float, ctypes.c_float,
                                    ctypes.c_float, ctypes.c_float, ctypes.c_float, ctypes.c_int, FLOAT_P, FLOAT_P]
        self.k.drainage_area.argtypes = [FLOAT_P, U8_P, ctypes.c_int, ctypes.c_int, ctypes.c_float, FLOAT_P]
        self.k.mfd_area.argtypes = [FLOAT_P, U8_P, ctypes.c_int, ctypes.c_int, ctypes.c_float, ctypes.c_float, FLOAT_P]
        self.k.bake_ao.argtypes = [FLOAT_P, ctypes.c_int, ctypes.c_int, ctypes.c_float, ctypes.c_int, ctypes.c_float, FLOAT_P]
        self.k.bake_shadow.argtypes = [FLOAT_P, ctypes.c_int, ctypes.c_int, ctypes.c_float, ctypes.c_float,
                                       ctypes.c_float, ctypes.c_float, ctypes.c_float, ctypes.c_float, FLOAT_P]

    # world-space noise on an n x n grid covering the map
    def noise(self, n, scale, octaves=6, mode=0, sub=0, lac=2.0, gain=0.5, warp=0.0, warp_scale=1.0):
        out = np.empty((n, n), np.float32)
        dx = MAP / n
        self.k.fbm(fp(out), n, n, -HALF, -HALF, dx, scale, octaves, lac, gain, (self.seed * 131 + sub) & 0xffffffff,
                   mode, warp, warp_scale)
        return out

    def noise_at(self, xs, zs, scale, octaves=5, mode=0, sub=0, lac=2.0, gain=0.5):
        xs = np.ascontiguousarray(xs, np.float32)
        zs = np.ascontiguousarray(zs, np.float32)
        out = np.empty_like(xs)
        self.k.fbm_points(fp(out), fp(xs), fp(zs), xs.size, scale, octaves, lac, gain, (self.seed * 131 + sub) & 0xffffffff, mode)
        return out

    def stream_power(self, h, uplift, kmul, outlet, talus, iters, dt, K, m, therm):
        n = h.shape[0]
        area = np.empty_like(h)
        self.k.stream_power(fp(h), fp(uplift), fp(kmul), outlet.ctypes.data_as(U8_P), fp(talus), n, n, MAP / n,
                            iters, dt, K, m, therm, fp(area))
        return area

    def thermal(self, h, talus, iters, rate=0.5, dep=None):
        n = h.shape[0]
        self.k.thermal(fp(h), fp(talus), n, n, MAP / n, iters, rate, fp(dep) if dep is not None else None)

    def droplets(self, h, drops, radius, dep, hard, seed_sub, erode=0.3, deposit=0.3, capacity=4.0, max_steps=80,
                 inertia=0.05, evaporate=0.015, min_slope=0.01, gravity=4.0):
        n = h.shape[0]
        self.k.droplets(fp(h), n, n, MAP / n, drops, (self.seed * 977 + seed_sub) & 0xffffffff, radius, inertia,
                        capacity, min_slope, deposit, erode, evaporate, gravity, max_steps, fp(dep), fp(hard))

    def area(self, h, outlet, mfd=False):
        n = h.shape[0]
        a = np.empty_like(h)
        if mfd:
            self.k.mfd_area(fp(h), outlet.ctypes.data_as(U8_P), n, n, MAP / n, 1.1, fp(a))
        else:
            self.k.drainage_area(fp(h), outlet.ctypes.data_as(U8_P), n, n, MAP / n, fp(a))
        return a


# ---------------------------------------------------------------- helpers
def grid(n):
    c = (-HALF + np.arange(n, dtype=np.float64) * (MAP / n)).astype(np.float32)
    return np.meshgrid(c, c)  # X[j, i] = x_i, Z[j, i] = z_j


def smoothstep(a, b, x):
    t = np.clip((x - a) / (b - a), 0.0, 1.0)
    return t * t * (3 - 2 * t)


def upsample(h, n):
    f = n / h.shape[0]
    # grid points are at -HALF + i*dx, so a plain zoom with grid-aligned origin keeps world registration
    out = ndimage.zoom(h, f, order=3, mode="nearest", grid_mode=True)
    return np.ascontiguousarray(out.astype(np.float32))


def downsample(a, n):
    f = a.shape[0] // n
    return np.ascontiguousarray(a.reshape(n, f, n, f).mean(axis=(1, 3)).astype(np.float32))


def catmull_rom(points, spacing):
    p = [np.array(points[0]) * 2 - np.array(points[1])] + [np.array(q, float) for q in points] + \
        [np.array(points[-1]) * 2 - np.array(points[-2])]
    out = []
    for i in range(1, len(p) - 2):
        p0, p1, p2, p3 = p[i - 1], p[i], p[i + 1], p[i + 2]
        seg = np.linalg.norm(p2 - p1)
        steps = max(2, int(seg / spacing * 3))
        for t in np.linspace(0, 1, steps, endpoint=False):
            t2, t3 = t * t, t * t * t
            out.append(0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 + (-p0 + 3 * p1 - 3 * p2 + p3) * t3))
    out.append(p[-2])
    out = np.array(out)
    # resample to uniform arc length
    d = np.r_[0, np.cumsum(np.linalg.norm(np.diff(out, axis=0), axis=1))]
    s = np.arange(0, d[-1], spacing)
    return np.stack([np.interp(s, d, out[:, 0]), np.interp(s, d, out[:, 1])], 1), s


class Canyon:
    """Canyon design along the spline: floor height, half-width, wall steepness as functions of arc length s."""

    def __init__(self, seed):
        self.pts, self.s = catmull_rom(CANYON_CTRL, 10.0)
        # extend the path into the valley centre so the carve meets the basin
        self.length = float(self.s[-1])
        rng = np.random.default_rng(seed + 5)
        self.wphase = rng.uniform(0, 100, 4)
        self.land_start = FJORD_LEN

    def floor(self, s):
        s = np.asarray(s, np.float32)
        t = np.clip((s - self.land_start) / (self.length - self.land_start), 0, 1)
        land = 4.0 + (TARGET_FLOOR - 4.0) * (0.55 * t + 0.45 * t * t)
        return np.where(s < self.land_start, -45.0 + 30.0 * smoothstep(self.land_start - 1500, self.land_start, s), land)

    def half_width(self, s):
        s = np.asarray(s, np.float32)
        ph = self.wphase
        v = 0.5 + 0.25 * np.sin(s / 1900.0 + ph[0]) + 0.15 * np.sin(s / 730.0 + ph[1]) + 0.1 * np.sin(s / 310.0 + ph[2])
        w = 32.0 + 85.0 * np.clip(v, 0, 1)  # floor 64..234 m
        fj = smoothstep(self.land_start + 800, self.land_start - 1500, s)
        return w + fj * 260.0

    def wall_min(self, s):
        s = np.asarray(s, np.float32)
        v = 0.5 + 0.5 * np.sin(s / 2300.0 + self.wphase[3])
        base = 380.0 + 480.0 * v
        end = smoothstep(self.length - 2500, self.length - 300, s)
        return base * (1 - 0.6 * end)

    def field(self, n):
        """distance to centreline (m), arc length of nearest centre point, both n x n."""
        dx = MAP / n
        ij = np.round((self.pts + HALF) / dx).astype(int)
        ok = (ij[:, 0] >= 0) & (ij[:, 0] < n) & (ij[:, 1] >= 0) & (ij[:, 1] < n)
        ij, sv = ij[ok], self.s[ok]
        mask = np.ones((n, n), bool)
        mask[ij[:, 1], ij[:, 0]] = False
        sgrid = np.zeros((n, n), np.float32)
        sgrid[ij[:, 1], ij[:, 0]] = sv
        dist, (iz, ix) = ndimage.distance_transform_edt(mask, return_indices=True)
        return (dist * dx).astype(np.float32), sgrid[iz, ix]


# ---------------------------------------------------------------- pipeline
def coast_x(Z, g):
    n = Z.shape[0]
    z = Z[:, 0].astype(np.float64)
    rng = np.random.default_rng(g.seed + 11)
    ph = rng.uniform(0, 100, 3)
    cx = -9500 + 1800 * np.sin(z / 9000 + ph[0]) + 900 * np.sin(z / 3700 + ph[1]) + 400 * np.sin(z / 1500 + ph[2])
    cx -= 6500 * np.exp(-((z - 8500) / 7000) ** 2)  # headland that the fjord of the canyon run cuts through
    cx = np.repeat(cx[:, None], n, axis=1).astype(np.float32)
    # ragged coast: peninsulas, sounds and islands
    return cx - 4200 * g.noise(n, 8000, 4, 0, sub=12, warp=0.8, warp_scale=10000) - 1400 * g.noise(n, 2600, 3, 0, sub=13)


def land_mask(g, X, Z):
    return smoothstep(-5000, 6000, X - coast_x(Z, g))


def fixed_features(g, n, canyon):
    """cells held at a prescribed base level during stream power: canyon floor and target valley floor."""
    X, Z = grid(n)
    d, s = canyon.field(n)
    fl = canyon.floor(s)
    cell = MAP / n
    corridor = (d < max(1.5 * cell, 90.0)) & (s < canyon.length - 100)
    tv = target_valley(g, X, Z)
    valley = tv < 1.0
    fixed = corridor | valley
    hfix = np.where(valley, TARGET_FLOOR + 25.0 * tv, fl).astype(np.float32)
    return fixed, hfix


def target_valley(g, X, Z):
    """normalised distance field of the target valley: <1 inside the flat floor (an elongated, warped glacial trough)."""
    ang = math.atan2(TARGET_AXIS[1], TARGET_AXIS[0])
    ca, sa = math.cos(ang), math.sin(ang)
    lx = (X - TARGET[0]) * ca + (Z - TARGET[1]) * sa
    lz = -(X - TARGET[0]) * sa + (Z - TARGET[1]) * ca
    n = X.shape[0]
    wob = g.noise(n, 1800, 3, 0, sub=90) * 0.18
    return (np.sqrt((lx / TARGET_HALF_LEN) ** 2 + (lz / TARGET_HALF_WIDTH) ** 2) * (1.0 + wob)).astype(np.float32)


def uplift_field(g, n, X, land):
    big = g.noise(n, 38000, 5, 0, sub=1, warp=0.6, warp_scale=30000)
    mid = g.noise(n, 12000, 5, 1, sub=2, warp=0.4, warp_scale=15000)
    inland = 0.75 + 0.25 * smoothstep(-12000, 8000, X)
    upl = land * inland * np.clip(0.45 + 0.4 * (big * 0.5 + 0.5) + 0.35 * mid, 0.2, 1.2)
    return (upl * 14.0).astype(np.float32)


def stage_a(g, n, canyon):
    log(f"stage A: base massif + stream power at {n}")
    X, Z = grid(n)
    land = land_mask(g, X, Z)
    big = g.noise(n, 38000, 5, 0, sub=1, warp=0.6, warp_scale=30000)
    upl = uplift_field(g, n, X, land)
    h = (land * (150 + 250 * (big * 0.5 + 0.5)) + (1 - land) * -350.0).astype(np.float32)
    h += g.noise(n, 3000, 4, 0, sub=3) * 30.0
    outlet = np.zeros((n, n), np.uint8)
    outlet[land < 0.03] = 1
    border = np.zeros((n, n), bool)
    border[0, :] = border[-1, :] = border[:, -1] = True
    outlet[border] = 1
    h[border & (land > 0.03)] = 350.0
    fixed, hfix = fixed_features(g, n, canyon)
    h[fixed] = hfix[fixed]
    outlet[fixed] = 1
    kmul = np.clip(1.0 + 0.45 * g.noise(n, 7000, 4, 0, sub=4), 0.4, 2.0).astype(np.float32)
    talus = np.full((n, n), math.tan(math.radians(36)), np.float32)
    g.stream_power(h, upl, kmul, outlet, talus, 260, 1.0, 0.08, 0.5, 6)
    land_h = h[(land > 0.5)]
    p = np.percentile(land_h, 99.7)
    scale = float(np.clip(2700.0 / p, 0.7, 1.25))
    log(f"  raw p99.7 = {p:.0f} m, max {h.max():.0f}, scale {scale:.2f}")
    global STAGE_A_SCALE
    STAGE_A_SCALE = scale
    h = np.where(h > 0, h * scale, h).astype(np.float32)
    return h, outlet


def stage_b(g, h, n, canyon):
    log(f"stage B: refine + stream power at {n}")
    h = upsample(h, n)
    X, Z = grid(n)
    land = land_mask(g, X, Z)
    rel = np.clip(h / 2500.0, 0, 1)
    h += (g.noise(n, 2600, 5, 1, sub=20, warp=0.5, warp_scale=4000) - 0.35) * 220.0 * rel * land
    outlet = np.zeros((n, n), np.uint8)
    outlet[land < 0.03] = 1
    border = np.zeros((n, n), bool)
    border[0, :] = border[-1, :] = border[:, -1] = True
    outlet[border] = 1
    fixed, hfix = fixed_features(g, n, canyon)
    h[fixed] = hfix[fixed]
    outlet[fixed] = 1
    upl = uplift_field(g, n, X, land) * float(STAGE_A_SCALE)
    kmul = np.clip(1.0 + 0.45 * g.noise(n, 7000, 4, 0, sub=4), 0.4, 2.0).astype(np.float32)
    kmul *= np.clip(1.0 + 0.25 * g.noise(n, 3000, 4, 0, sub=21), 0.6, 1.4).astype(np.float32)
    talus = np.full((n, n), math.tan(math.radians(38)), np.float32)
    area = g.stream_power(h, upl, kmul, outlet, talus, 60, 1.0, 0.08, 0.5, 4)
    # glacial widening: broaden valleys with large catchments into U-shapes
    la = np.log10(np.maximum(area, 1.0))
    valley = smoothstep(6.3, 8.4, la) * land
    carve = ndimage.gaussian_filter(valley, sigma=380.0 / (MAP / n))
    carve = carve / max(carve.max(), 1e-6)
    h -= (carve * 260.0 * smoothstep(-200, 600, h)).astype(np.float32)
    return h, land


def shape_features(g, h, n, canyon, final=False):
    """fjord subsidence, target valley floor, canyon inner gorge (steep rock walls cut into the valley)."""
    X, Z = grid(n)
    cx = coast_x(Z, g)
    d, s = canyon.field(n)
    if not final:
        # drown the valleys near the coast: fjords and sounds
        sub = 480.0 * smoothstep(cx + 8000, cx - 7000, X) * smoothstep(300, 1500, d) * np.clip(0.55 + 0.9 * (g.noise(n, 5000, 3, 0, sub=14) * 0.5 + 0.5), 0.4, 1.5)
        h -= sub.astype(np.float32)
        h[:] = np.where(X < cx - 9000, np.minimum(h, -250.0 - 150.0 * smoothstep(cx - 9000, cx - 25000, X)), h)
    tv = target_valley(g, X, Z)
    floor_t = TARGET_FLOOR + 25.0 * np.minimum(tv, 1.0) + np.maximum(tv - 1.0, 0.0) ** 1.6 * 900.0
    h[:] = np.where(tv < 2.5, np.minimum(h, floor_t), h)
    fl = canyon.floor(s)
    hw = canyon.half_width(s)
    # buttresses/spurs and bays along the canyon, then fall-line couloirs cut into the walls (canyon-aligned noise space)
    wn = g.noise(n, 900, 4, 0, sub=31, warp=0.5, warp_scale=1100)
    wn2 = g.noise(n, 320, 3, 0, sub=32)
    dd = d + wn * 85.0 + wn2 * 30.0
    t = np.maximum(dd - hw, 0.0)
    steep = 1.3 + 1.7 * (g.noise(n, 1700, 3, 0, sub=33) * 0.5 + 0.5)  # 52..72 deg: slabs to near-vertical cliffs
    q = t * steep / 70.0
    ter = (np.floor(q) + smoothstep(0.25, 0.75, q - np.floor(q))) * 70.0  # soft ledges
    near = d < 3000
    gul = np.zeros_like(h)
    gul[near] = g.noise_at(s[near] / 140.0 * 100.0, t[near] / 700.0 * 100.0, 100.0, 4, 1, sub=34)
    couloir = smoothstep(0.55, 0.95, gul) * smoothstep(0, 60, t) * 55.0
    gorge = canyon.wall_min(s)
    wall = fl + np.where(t * steep < gorge, 0.6 * t * steep + 0.4 * ter,
                         gorge + (t - gorge / steep) * 0.9) - couloir  # steep gorge, then ~42 deg valley sides
    end_fade = smoothstep(canyon.length - 600, canyon.length + 300, s) * smoothstep(0, 400, d)
    if final:
        return d, s  # walls were shaped before erosion; the final pass only keeps the corridor (enforce_canyon)
    active = (d < 5000) & (end_fade < 1.0)
    cut = np.minimum(h, wall)
    h[:] = np.where(active, h + (cut - h) * (1 - end_fade), h)
    return d, s


def stage_d(g, h, n, canyon, dep):
    log(f"stage D: detail + droplets at {n}")
    h = upsample(h, n)
    X, Z = grid(n)
    slope = np.hypot(*np.gradient(h, MAP / n))
    rock = smoothstep(0.35, 0.9, slope)
    h += ((g.noise(n, 900, 5, 1, sub=40, warp=0.6, warp_scale=1400) - 0.3) * (18 + 50 * rock) *
          smoothstep(0, 200, h)).astype(np.float32)
    d, s = canyon.field(n)
    hard = (1.0 + 0.8 * (g.noise(n, 2500, 4, 0, sub=41) * 0.5 + 0.5) + 1.2 * smoothstep(900, 150, d)).astype(np.float32)
    g.droplets(h, int(n * n * 0.12), 3, dep, hard, 1, erode=0.25, deposit=0.25)
    talus = np.tan(np.radians(36 + 16 * smoothstep(1.2, 2.2, hard) + 24 * smoothstep(700, 120, d))).astype(np.float32)
    g.thermal(h, talus, 20, 0.4, dep)
    return h, hard


def stage_e(g, h, n, canyon, dep_lo):
    log(f"stage E: final detail at {n}")
    h = upsample(h, n)
    dep = upsample(dep_lo, n) * 0.25
    slope = np.hypot(*np.gradient(h, MAP / n))
    rock = smoothstep(0.45, 1.0, slope)
    h += ((g.noise(n, 220, 5, 1, sub=50, warp=0.8, warp_scale=300) - 0.3) * (3 + 14 * rock) *
          smoothstep(0, 60, h)).astype(np.float32)
    d, s = canyon.field(n)
    hard = (1.0 + 0.6 * (g.noise(n, 1200, 4, 0, sub=51) * 0.5 + 0.5) + 1.2 * smoothstep(700, 120, d)).astype(np.float32)
    g.droplets(h, int(n * n * 0.05), 2, dep, hard, 2, erode=0.2, deposit=0.35, max_steps=60)
    talus = np.tan(np.radians(37 + 14 * smoothstep(1.2, 2.0, hard) + 24 * smoothstep(600, 100, d))).astype(np.float32)
    g.thermal(h, talus, 8, 0.4, dep)
    return h, dep, hard


def coastal_cliffs(g, h, n):
    """steepen the waterline: land drops into the sea as rocky cliffs, sea floor falls away quickly."""
    near_sea = ndimage.distance_transform_edt(h > 0) * (MAP / n)  # for sea cells: distance to land
    near_land = ndimage.distance_transform_edt(h <= 0) * (MAP / n)  # for land cells: distance to sea
    cliff = g.noise(n, 2500, 3, 0, sub=60) * 0.5 + 0.5
    lift = (18.0 + 55.0 * cliff) * smoothstep(0, 25, near_land) * smoothstep(400, 80, near_land)
    h = np.where(h > 0, h + lift * smoothstep(120, 5, h) * smoothstep(-10, 5, h), h)
    h = np.where(h <= 0, np.minimum(h, -4.0 - near_sea * 0.35), h)
    return h.astype(np.float32)


def enforce_canyon(h, n, canyon):
    d, s = canyon.field(n)
    fl = canyon.floor(s)
    hw = canyon.half_width(s)
    live = (s > 50) & (s < canyon.length - 300)
    riv = smoothstep(hw * 0.28 + 6, hw * 0.28 - 2, d) * (s > canyon.land_start)
    floor_h = fl + 1.5 * smoothstep(0.3 * hw, hw, d) - 2.2 * riv
    corridor = live & (d < hw)
    h = np.where(corridor, np.minimum(h, floor_h), h)
    # smooth, gently sloping floor edges into the walls
    edge = live & (d >= hw) & (d < hw + 60)
    h = np.where(edge, np.minimum(h, fl + 1.5 + (d - hw) * 1.2), h)
    return h.astype(np.float32), d, s, riv


def masks(g, h8, n_mask, canyon, dep, hard):
    log(f"masks at {n_mask}")
    h = downsample(h8, n_mask)
    cell = MAP / n_mask
    ao = np.empty_like(h)
    g.k.bake_ao(fp(h), n_mask, n_mask, cell, 16, 2500.0, fp(ao))
    sh = np.empty_like(h)
    az = math.radians(SUN_AZIMUTH_DEG)
    sx, sz = math.sin(az), -math.cos(az)
    g.k.bake_shadow(fp(h), n_mask, n_mask, cell, sx, sz, math.radians(SUN_ELEV_DEG), math.radians(1.2), 25000.0, fp(sh))
    outlet = np.zeros(h.shape, np.uint8)
    outlet[h <= 0] = 1
    outlet[0, :] = outlet[-1, :] = outlet[:, 0] = outlet[:, -1] = 1
    area = g.area(h, outlet, mfd=True)
    flow = np.clip((np.log10(np.maximum(area, 1)) - 3.0) / 5.0, 0, 1)
    depm = np.clip(np.sqrt(np.maximum(downsample(dep, n_mask), 0) / 6.0), 0, 1)
    gy, gx = np.gradient(h, cell)
    slope = np.hypot(gx, gy)
    lap = ndimage.gaussian_filter(h, 1.0) - ndimage.gaussian_filter(h, 4.0)
    curv = np.clip(0.5 - lap / 60.0, 0, 1)  # >0.5 concave (gullies), <0.5 convex (ridges)
    # forest: altitude band, slope limit, avalanche chutes, clearings
    X, Z = grid(n_mask)
    treeline = 1550 + 180 * g.noise(n_mask, 5000, 3, 0, sub=70)
    alt = smoothstep(treeline + 60, treeline - 250, h) * smoothstep(3, 25, h)
    slope_ok = smoothstep(1.4, 0.9, slope)
    chute = smoothstep(0.55, 0.85, curv) * smoothstep(0.45, 0.8, slope) * smoothstep(0.4, 0.7, flow)
    clump = smoothstep(-0.55, 0.1, g.noise(n_mask, 900, 4, 0, sub=71)) * (0.75 + 0.25 * smoothstep(-0.3, 0.3, g.noise(n_mask, 180, 3, 0, sub=72)))
    d, s = canyon.field(n_mask)
    hw = canyon.half_width(s)
    canyon_floor = 0.0
    wet = smoothstep(0.55, 0.8, flow) * smoothstep(0.2, 0.05, slope)
    basin = smoothstep(1.0, 0.6, target_valley(g, X, Z)) * 0.7
    forest = alt * slope_ok * clump * (1 - chute) * (1 - canyon_floor) * (1 - 0.7 * wet) * (1 - basin)
    forest *= np.clip(0.55 + 0.45 * np.sqrt(np.maximum(ao, 0)), 0, 1)
    forest = np.clip(forest * 1.45, 0, 1)
    # keep the flight corridor and the river free of trees
    riv_band = smoothstep(hw * 0.28 + 25, hw * 0.28 + 5, d) * (s < canyon.length - 200)
    corridor = smoothstep(hw * 0.95, hw * 0.75, d) * (s < canyon.length - 300)
    forest *= (1 - np.maximum(riv_band, corridor))
    # water: canyon river and wide valley rivers
    riv = smoothstep(hw * 0.28 + 6, hw * 0.28 - 2, d) * (s > canyon.land_start) * (s < canyon.length - 200)
    rivers = smoothstep(0.8, 0.9, flow) * smoothstep(0.05, 0.02, slope)
    water = np.clip(np.maximum(riv, rivers) * (h > 0), 0, 1)
    hardm = np.clip((downsample(hard, n_mask) - 1.0) / 3.5, 0, 1)
    to8 = lambda a: np.clip(np.round(a * 255), 0, 255).astype(np.uint8)
    ma = np.stack([to8(ao), to8(sh), to8(flow), to8(depm)], -1)
    mb = np.stack([to8(forest), to8(curv), to8(water), to8(hardm)], -1)
    return ma, mb, sh, ao


def hillshade(h, cell, az=315, alt=45):
    gy, gx = np.gradient(h, cell)
    a, e = math.radians(az), math.radians(alt)
    nx, ny, nz = -gx, -gy, np.ones_like(h)
    ln = np.sqrt(nx * nx + ny * ny + 1)
    lx, ly, lz = math.sin(a) * math.cos(e), -math.cos(a) * math.cos(e), math.sin(e)
    return np.clip((nx * lx + ny * ly + nz * lz) / ln, 0, 1)


def preview(h, path, size=2048, canyon=None, extra=None):
    n = h.shape[0]
    if n > size:
        h = downsample(h, size)
    cell = MAP / h.shape[0]
    hs = hillshade(h, cell, az=SUN_AZIMUTH_DEG, alt=SUN_ELEV_DEG)
    col = np.zeros(h.shape + (3,), np.float32)
    t = np.clip(h / 2800.0, 0, 1)[..., None]
    land = np.array([0.30, 0.38, 0.22]) * (1 - t) + np.array([0.55, 0.52, 0.48]) * t
    snow = smoothstep(1300, 1700, h)[..., None]
    land = land * (1 - snow) + np.array([0.95, 0.95, 0.97]) * snow
    sea = np.array([0.05, 0.18, 0.32]) * (1 + np.clip(h, -600, 0)[..., None] / 900.0)
    col = np.where(h[..., None] > 0, land * (0.25 + 0.85 * hs[..., None]), sea)
    img = (np.clip(col, 0, 1) * 255).astype(np.uint8)
    im = Image.fromarray(img)
    if canyon is not None:
        from PIL import ImageDraw
        dr = ImageDraw.Draw(im)
        k = h.shape[0] / MAP
        pts = [((p[0] + HALF) * k, (p[1] + HALF) * k) for p in canyon.pts[::20]]
        dr.line(pts, fill=(255, 40, 40), width=1)
        for p, c in ((TARGET, (255, 255, 0)), (CARRIER, (0, 255, 255))):
            x, y = (p[0] + HALF) * k, (p[1] + HALF) * k
            dr.ellipse([x - 5, y - 5, x + 5, y + 5], outline=c, width=2)
    im.save(path)


# ---------------------------------------------------------------- outputs
def write_height(h, path):
    v = np.clip(np.round((h - HMIN) / (HMAX - HMIN) * 65535.0), 0, 65535).astype("<u2")
    v.tofile(path)
    return v


def height_from_u16(v, x, z, n):
    """bilinear sample, identical to World.height_at (used for layout numbers)."""
    dx = MAP / n
    u, w = (x + HALF) / dx, (z + HALF) / dx
    i, j = int(math.floor(u)), int(math.floor(w))
    fu, fw = u - i, w - j

    def m(k):
        p = 2 * (n - 1)
        k %= p
        return k if k <= n - 1 else p - k

    def t(a, b):
        return HMIN + v[m(b), m(a)] / 65535.0 * (HMAX - HMIN)

    return (t(i, j) * (1 - fu) + t(i + 1, j) * fu) * (1 - fw) + (t(i, j + 1) * (1 - fu) + t(i + 1, j + 1) * fu) * fw


def write_layout(path, v, n, canyon):
    pts = canyon.pts[::20]  # every 200 m
    lines = []
    for p in pts:
        y = height_from_u16(v, float(p[0]), float(p[1]), n)
        lines.append(f"\tVector3({p[0]:.1f}, {max(y, 0.0):.1f}, {p[1]:.1f}),")
    ty = height_from_u16(v, TARGET[0], TARGET[1], n)
    az = math.radians(SUN_AZIMUTH_DEG)
    el = math.radians(SUN_ELEV_DEG)
    to_sun = (math.sin(az) * math.cos(el), math.sin(el), -math.cos(az) * math.cos(el))
    mouth = canyon.pts[0]
    txt = f"""# Generated by tools/terrain/gen_terrain.py (seed {canyon_seed}) — world layout constants; do not edit by hand.
class_name WorldLayout
extends RefCounted

const MAP_SIZE := {MAP:.1f}
const HEIGHT_SIZE := {n}
const HEIGHT_MIN := {HMIN:.1f}
const HEIGHT_MAX := {HMAX:.1f}
const SUN_ELEVATION_DEG := {SUN_ELEV_DEG:.1f}
const SUN_AZIMUTH_DEG := {SUN_AZIMUTH_DEG:.1f}
## unit vector pointing towards the sun
const TO_SUN := Vector3({to_sun[0]:.5f}, {to_sun[1]:.5f}, {to_sun[2]:.5f})
const TARGET := Vector3({TARGET[0]:.1f}, {ty:.1f}, {TARGET[1]:.1f})
const CARRIER := Vector3({CARRIER[0]:.1f}, 0.0, {CARRIER[1]:.1f})
const CARRIER_HEADING_DEG := {CARRIER_HEADING_DEG:.1f}
const CANYON_MOUTH := Vector3({mouth[0]:.1f}, 0.0, {mouth[1]:.1f})
const CANYON_LENGTH := {canyon.length:.1f}
const FJORD_LENGTH := {FJORD_LEN:.1f}
## canyon centreline every 200 m, y = terrain floor height (sea level in the fjord)
const CANYON_PATH: Array[Vector3] = [
{chr(10).join(lines)}
]
"""
    open(path, "w").write(txt)


canyon_seed = 0
STAGE_A_SCALE = 1.0


def main():
    global canyon_seed
    ap = argparse.ArgumentParser()
    ap.add_argument("--seed", type=int, default=1729)
    ap.add_argument("--size", type=int, default=8192)
    ap.add_argument("--preview-only", action="store_true")
    ap.add_argument("--masks-only", action="store_true", help="recompute masks from the existing height.r16")
    ap.add_argument("--work", default=os.path.join(HERE, "build"))
    a = ap.parse_args()
    canyon_seed = a.seed
    os.makedirs(a.work, exist_ok=True)
    if os.path.abspath(a.work).startswith(GAME):
        open(os.path.join(a.work, ".gdignore"), "w").close()  # keep Godot from importing the previews
    g = Gen(a.seed, a.work)
    canyon = Canyon(a.seed)
    log(f"canyon length {canyon.length:.0f} m, land part {canyon.length - FJORD_LEN:.0f} m")
    n_final = a.size
    n_a = 1024
    n_b = 2048
    n_d = min(4096, n_final)
    t0 = time.time()
    src = os.path.join(GAME, "assets", "world", "src")
    ter = os.path.join(GAME, "assets", "world", "terrain")
    if a.masks_only:
        v = np.fromfile(os.path.join(src, "height.r16"), "<u2")
        n_final = int(math.isqrt(v.size))
        h = (HMIN + v.reshape(n_final, n_final).astype(np.float32) / 65535.0 * (HMAX - HMIN)).astype(np.float32)
        dep = np.load(os.path.join(src, "deposit.npy")) if os.path.exists(os.path.join(src, "deposit.npy")) else None
        hard = np.load(os.path.join(src, "hardness.npy")) if os.path.exists(os.path.join(src, "hardness.npy")) else None
        n_mask = min(4096, n_final)
        if dep is None:
            old = np.asarray(Image.open(os.path.join(ter, "masks_a.png")))[..., 3].astype(np.float32) / 255.0
            dep = upsample((old ** 2 * 6.0).astype(np.float32), n_final)
        if hard is None:
            hard = np.ones_like(h)
        ma, mb, sh, ao = masks(g, h, n_mask, canyon, dep if dep.shape[0] == n_final else upsample(dep, n_final),
                               hard if hard.shape[0] == n_final else upsample(hard, n_final))
        Image.fromarray(ma, "RGBA").save(os.path.join(ter, "masks_a.png"))
        Image.fromarray(mb, "RGBA").save(os.path.join(ter, "masks_b.png"))
        log("masks rewritten")
        return
    h, _ = stage_a(g, n_a, canyon)
    preview(h, os.path.join(a.work, "prev_a.png"), canyon=canyon)
    h, land = stage_b(g, h, n_b, canyon)
    shape_features(g, h, n_b, canyon)
    preview(h, os.path.join(a.work, "prev_b.png"), canyon=canyon)
    dep = np.zeros((n_d, n_d), np.float32)
    h, hard = stage_d(g, h, n_d, canyon, dep)
    preview(h, os.path.join(a.work, "prev_d.png"), canyon=canyon)
    if n_final > n_d:
        h, dep, hard = stage_e(g, h, n_final, canyon, dep)
    h = coastal_cliffs(g, h, n_final)
    shape_features(g, h, n_final, canyon, final=True)
    h, d, s, riv = enforce_canyon(h, n_final, canyon)
    log(f"height range {h.min():.0f} .. {h.max():.0f}, {time.time() - t0:.0f} s")
    preview(h, os.path.join(a.work, "prev_final.png"), canyon=canyon)
    n_mask = min(4096, n_final)
    ma, mb, sh, ao = masks(g, h, n_mask, canyon, dep if dep.shape[0] == n_final else upsample(dep, n_final), hard if hard.shape[0] == n_final else upsample(hard, n_final))
    Image.fromarray(ma[..., :3]).save(os.path.join(a.work, "prev_mask_a.png"))
    Image.fromarray(mb[..., :3]).save(os.path.join(a.work, "prev_mask_b.png"))
    if a.preview_only:
        np.save(os.path.join(a.work, f"height_{n_final}.npy"), h)
        log("preview only, done")
        return
    os.makedirs(src, exist_ok=True)
    np.save(os.path.join(src, "deposit.npy"), downsample(dep, 4096) if dep.shape[0] > 4096 else dep)
    np.save(os.path.join(src, "hardness.npy"), downsample(hard, 4096) if hard.shape[0] > 4096 else hard)
    os.makedirs(ter, exist_ok=True)
    v = write_height(h, os.path.join(src, "height.r16"))
    json.dump({"size": n_final, "hmin": HMIN, "hmax": HMAX, "seed": a.seed},
              open(os.path.join(src, "height.json"), "w"))
    Image.fromarray(ma, "RGBA").save(os.path.join(ter, "masks_a.png"))
    Image.fromarray(mb, "RGBA").save(os.path.join(ter, "masks_b.png"))
    write_layout(os.path.join(GAME, "scripts", "world", "world_layout.gd"), v.reshape(n_final, n_final), n_final, canyon)
    godot = os.environ.get("GODOT", "/Applications/Godot.app/Contents/MacOS/Godot")
    if os.path.exists(godot):
        # run in a throw-away mini project so the shared project's .godot cache is never touched
        mini = os.path.join(a.work, "pack_project")
        os.makedirs(mini, exist_ok=True)
        open(os.path.join(mini, "project.godot"), "w").write("config_version=5\n")
        subprocess.check_call([godot, "--headless", "--path", mini, "--script", os.path.join(HERE, "pack_height.gd"), "--",
                               os.path.join(src, "height.r16"), str(n_final), os.path.join(ter, "height.res")])
    log(f"done in {time.time() - t0:.0f} s")


if __name__ == "__main__":
    main()
