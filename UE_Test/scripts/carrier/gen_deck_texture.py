"""Flight deck albedo + paint mask (T_CarrierDeck, 4096 x 1024 RGBA): nonskid base with grime, rubber and soot, landing area
lines, wires, catapult tracks, elevator hazard edges, taxi lines, tie-down pad eyes. A = paint mask (smoother roughness).
Own work. Usage: python3 gen_deck_texture.py <out.png>"""
import sys, math, os
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import layout as L

W, H = 4096, 1024
SS = 2                      # supersampling for the vector markings
rng = np.random.default_rng(72)


def to_px(x, y, ss=SS):
    u = (x - L.TEX_X0) / (L.TEX_X1 - L.TEX_X0)
    v = (y - L.TEX_Y0) / (L.TEX_Y1 - L.TEX_Y0)
    return (u * W * ss, v * H * ss)


def noise(scale_px, octaves=4, seed=0):
    """Smooth value noise at the texture resolution (0..1)."""
    r = np.random.default_rng(seed)
    out = np.zeros((H, W), np.float32)
    amp, tot = 1.0, 0.0
    s = scale_px
    for _ in range(octaves):
        gh, gw = max(2, int(H / s) + 2), max(2, int(W / s) + 2)
        g = r.random((gh, gw)).astype(np.float32)
        img = Image.fromarray((g * 255).astype(np.uint8)).resize((W, H), Image.BICUBIC)
        out += np.asarray(img, np.float32) / 255.0 * amp
        tot += amp
        amp *= 0.5
        s /= 2.0
    return out / tot


# ---------------------------------------------------------------- base nonskid + grime
base = np.full((H, W, 3), (62, 65, 68), np.float32)
n1 = noise(160, 5, 1)
n2 = noise(24, 3, 2)
grain = rng.normal(0.0, 1.0, (H, W)).astype(np.float32)
lum = (n1 - 0.5) * 16 + (n2 - 0.5) * 8 + grain * 3.5
base += lum[..., None]
# warm/cool patches of repainted nonskid
tint = noise(420, 3, 3)
base[..., 0] += (tint - 0.5) * 6
base[..., 2] -= (tint - 0.5) * 4

stain = Image.new("L", (W * SS, H * SS), 0)     # darkening (rubber, soot, oil)
sd = ImageDraw.Draw(stain)


def land_poly(s0, s1, l0, l1):
    return [to_px(*L.land_point(s0, l0)), to_px(*L.land_point(s1, l0)), to_px(*L.land_point(s1, l1)), to_px(*L.land_point(s0, l1))]


# rubber streaks in the landing area (touchdown -> rollout), heaviest around wires 1-4
for k in range(900):
    s = rng.normal(L.TOUCHDOWN_S + 10, 28)
    lat = rng.normal(0.0, 4.5)
    ln = abs(rng.normal(22, 12))
    w = rng.uniform(0.25, 0.7)
    a = int(rng.uniform(10, 45))
    p0 = to_px(*L.land_point(s, lat)); p1 = to_px(*L.land_point(s + ln, lat + rng.normal(0, 0.6)))
    sd.line([p0, p1], fill=a, width=max(1, int(w / 0.085 * SS)))
# soot/steam behind the catapult JBDs and along the tracks
for c in L.CATS:
    d = L.cat_dir(c)
    jx, jy = L.jbd(c)
    for k in range(120):
        t = rng.uniform(-2, 16)
        lat = rng.normal(0, 2.5)
        x, y = c["x"] - d[0] * t + (-d[1]) * lat, c["y"] - d[1] * t + d[0] * lat
        r = rng.uniform(0.6, 2.4) / 0.085 * SS
        px, py = to_px(x, y)
        sd.ellipse([px - r, py - r * 0.7, px + r, py + r * 0.7], fill=int(rng.uniform(6, 22)))
    for k in range(90):
        t = rng.uniform(0, c["stroke"])
        x, y = c["x"] + d[0] * t, c["y"] + d[1] * t
        px, py = to_px(x, y)
        r = rng.uniform(0.3, 1.2) / 0.085 * SS
        sd.ellipse([px - r * 2, py - r, px + r * 2, py + r], fill=int(rng.uniform(5, 18)))
# oil drips / scattered stains
for k in range(700):
    x, y = rng.uniform(-160, 165), rng.uniform(-45, 35)
    px, py = to_px(x, y)
    r = abs(rng.normal(0.35, 0.3)) / 0.085 * SS
    sd.ellipse([px - r, py - r, px + r, py + r], fill=int(rng.uniform(8, 35)))
stain = stain.filter(ImageFilter.GaussianBlur(3 * SS)).resize((W, H), Image.LANCZOS)
st = np.asarray(stain, np.float32) / 255.0
base *= (1.0 - st[..., None] * 0.9)

# ---------------------------------------------------------------- vector markings (RGBA, supersampled)
mk = Image.new("RGBA", (W * SS, H * SS), (0, 0, 0, 0))
md = ImageDraw.Draw(mk)
WHITE = (214, 214, 206, 255)
YELLOW = (222, 178, 38, 255)
RED = (176, 40, 34, 255)
BLACK = (22, 22, 22, 255)
GREEN = (60, 140, 70, 255)


def px_w(m):
    return max(1, int(round(m / 0.085 * SS)))


def seg(a, b, col, wm):
    md.line([to_px(*a), to_px(*b)], fill=col, width=px_w(wm))


def dashed(a, b, col, wm, dash, gap):
    ax, ay = a; bx, by = b
    ln = math.hypot(bx - ax, by - ay)
    ux, uy = (bx - ax) / ln, (by - ay) / ln
    t = 0.0
    while t < ln:
        t1 = min(t + dash, ln)
        seg((ax + ux * t, ay + uy * t), (ax + ux * t1, ay + uy * t1), col, wm)
        t += dash + gap


# deck edge line (inset 0.6 m from the outline)
poly = L.OUTLINE + [L.OUTLINE[0]]
for i in range(len(L.OUTLINE)):
    (x0, y0), (x1, y1) = poly[i], poly[i + 1]
    ln = math.hypot(x1 - x0, y1 - y0)
    nx, ny = -(y1 - y0) / ln, (x1 - x0) / ln          # inward for a clockwise (x fwd, y stbd) outline
    seg((x0 + nx * 0.6, y0 + ny * 0.6), (x1 + nx * 0.6, y1 + ny * 0.6), WHITE, 0.25)

# landing area: edges, centreline, ramp ladder, foul line
half = L.LAND_WIDTH / 2
seg(L.land_point(0, -half), L.land_point(L.LAND_LEN, -half), WHITE, 0.45)
seg(L.land_point(0, half), L.land_point(L.LAND_LEN, half), WHITE, 0.45)
dashed(L.land_point(1.0, half + 1.2), L.land_point(L.LAND_LEN, half + 1.2), RED, 0.3, 1.5, 1.5)   # foul line
dashed(L.land_point(4.0, 0), L.land_point(L.LAND_LEN, 0), WHITE, 0.45, 4.5, 4.5)
for k in range(9):                                         # ramp "ladder"
    s = 2.0 + k * 3.2
    seg(L.land_point(s, -3.0), L.land_point(s, 3.0), WHITE, 0.5)
# round-down (ramp) edge: black/white checker along the stern
for k in range(30):
    y0 = -14.5 + k * 1.05
    md.rectangle([to_px(-163.0, y0), to_px(-161.8, y0 + 1.05)], fill=WHITE if k % 2 else BLACK)
# wires: a dark pendant groove + yellow/black sheave ends
for s in L.WIRES_S:
    seg(L.land_point(s, -L.WIRE_HALF - 0.8), L.land_point(s, L.WIRE_HALF + 0.8), (30, 30, 30, 200), 0.35)
    for lat in (-L.WIRE_HALF - 1.6, L.WIRE_HALF + 1.6):
        seg(L.land_point(s - 1.0, lat), L.land_point(s + 1.0, lat), YELLOW, 1.2)
        seg(L.land_point(s - 0.3, lat), L.land_point(s + 0.3, lat), BLACK, 1.2)
# touchdown aim "box"
for lat in (-6.0, 6.0):
    seg(L.land_point(L.TOUCHDOWN_S - 3, lat), L.land_point(L.TOUCHDOWN_S + 3, lat), WHITE, 0.35)

# catapults: shuttle slot, track edges, launch line, hold-back box, JBD recess
for c in L.CATS:
    d = L.cat_dir(c); n = (-d[1], d[0])
    x0, y0 = c["x"], c["y"]
    x1, y1 = x0 + d[0] * c["stroke"], y0 + d[1] * c["stroke"]
    seg((x0 - d[0] * 2, y0 - d[1] * 2), (x1 + d[0] * 3, y1 + d[1] * 3), (16, 16, 16, 255), 0.28)
    for sgn in (-1, 1):
        dashed((x0 + n[0] * 1.4 * sgn, y0 + n[1] * 1.4 * sgn), (x1 + n[0] * 1.4 * sgn, y1 + n[1] * 1.4 * sgn), WHITE, 0.2, 2.0, 2.0)
    seg((x0 - n[0] * 4, y0 - n[1] * 4), (x0 + n[0] * 4, y0 + n[1] * 4), YELLOW, 0.3)          # launch bar line
    seg((x1 - n[0] * 3, y1 - n[1] * 3), (x1 + n[0] * 3, y1 + n[1] * 3), WHITE, 0.3)          # end of stroke
    for k in range(6):                                                                       # lead-in guide chevrons
        t = -4.0 - k * 3.0
        a = (x0 + d[0] * t, y0 + d[1] * t)
        seg((a[0] - d[0] * 1.2 + n[0] * 1.6, a[1] - d[1] * 1.2 + n[1] * 1.6), a, YELLOW, 0.22)
        seg((a[0] - d[0] * 1.2 - n[0] * 1.6, a[1] - d[1] * 1.2 - n[1] * 1.6), a, YELLOW, 0.22)
    jx, jy = L.jbd(c)                                                                        # JBD recess (panel lies here)
    q = [(jx + n[0] * L.JBD_WIDTH / 2, jy + n[1] * L.JBD_WIDTH / 2), (jx - n[0] * L.JBD_WIDTH / 2, jy - n[1] * L.JBD_WIDTH / 2)]
    q += [(q[1][0] + d[0] * L.JBD_LEN, q[1][1] + d[1] * L.JBD_LEN), (q[0][0] + d[0] * L.JBD_LEN, q[0][1] + d[1] * L.JBD_LEN)]
    md.polygon([to_px(*p) for p in q], fill=(34, 35, 36, 255))
    for i in range(4):
        seg(q[i], q[(i + 1) % 4], YELLOW, 0.2)

# elevators: yellow/black hazard edge
for e in L.ELEVATORS:
    x0, x1, y0, y1 = e["x0"], e["x1"], e["y0"], e["y1"]
    step = 1.2
    for side in (y0 + 0.5, y1 - 0.5):
        k = 0; x = x0
        while x < x1:
            seg((x, side), (min(x + step, x1), side), YELLOW if k % 2 == 0 else BLACK, 0.5)
            x += step; k += 1
    for side in (x0 + 0.5, x1 - 0.5):
        k = 0; y = y0
        while y < y1:
            seg((side, y), (side, min(y + step, y1)), YELLOW if k % 2 == 0 else BLACK, 0.5)
            y += step; k += 1

# taxi guide lines (yellow): from the parking row / elevators to the bow cats, around the island (safe walkway: white)
pj = L.PLAYER_JET
for c in L.CATS[:2]:
    d = L.cat_dir(c)
    seg((c["x"] - d[0] * 22, c["y"] - d[1] * 22), (c["x"] - d[0] * 4, c["y"] - d[1] * 4), YELLOW, 0.2)
seg((pj["x"] - 20, pj["y"] + 2), (pj["x"] + 14, pj["y"] - 4), YELLOW, 0.2)
seg((-70, 18.5), (100, 18.5), YELLOW, 0.18)
isl = L.ISLAND
for a, b in [((isl["x0"] - 2, isl["y0"] - 2), (isl["x1"] + 2, isl["y0"] - 2)), ((isl["x0"] - 2, isl["y0"] - 2), (isl["x0"] - 2, 31)),
             ((isl["x1"] + 2, isl["y0"] - 2), (isl["x1"] + 2, 31))]:
    seg(a, b, WHITE, 0.3)
# IFLOLS / LSO platform area outlines
lx, ly = L.LSO_PLATFORM
md.rectangle([to_px(lx - 3, ly - 1.5), to_px(lx + 3, ly + 2.5)], outline=WHITE, width=px_w(0.2))

mk = mk.resize((W, H), Image.LANCZOS)
m = np.asarray(mk, np.float32) / 255.0
alpha = m[..., 3:4]
paint = m[..., :3] * 255.0
# paint wears: modulate with grime noise, keep stains visible on top
wear = (0.78 + 0.22 * noise(60, 4, 7))[..., None] * (1.0 - st[..., None] * 0.55)
paint = paint * wear
col = base * (1 - alpha) + paint * alpha

# tie-down pad eyes: dark rings on a 3.6 m grid over the deck (not inside markings)
pe = Image.new("L", (W, H), 0)
pd = ImageDraw.Draw(pe)
for x in np.arange(-160, 168, 3.6):
    for y in np.arange(-48, 38, 3.6):
        px, py = to_px(x + (y * 0.37) % 1.2, y, 1)
        pd.ellipse([px - 1.1, py - 1.1, px + 1.1, py + 1.1], outline=255)
pe = np.asarray(pe, np.float32)[..., None] / 255.0
col = col * (1 - pe * 0.45)

rgba = np.concatenate([np.clip(col, 0, 255), np.clip(alpha * 255.0, 0, 255)], axis=2).astype(np.uint8)
out = sys.argv[1] if len(sys.argv) > 1 else "T_CarrierDeck.png"
Image.fromarray(rgba, "RGBA").save(out)
print("wrote", out)
