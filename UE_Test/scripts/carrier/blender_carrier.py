"""Procedural Nimitz-class carrier (own work, true dimensions from layout.py) for UE: hull, flight deck, island, sponsons,
catwalks, elevators, JBD panel, hangar interior, collision mesh; optional candidate renders.
Blender frame = UE carrier-local with Y mirrored (Blender Y = port); glTF -> UE import then gives X bow, Y starboard, Z up.
Usage: Blender -b --python blender_carrier.py -- <out_dir> [--render <jpg>] [--deck-tex <png>]"""
import bpy, bmesh, sys, os, math
from mathutils import Vector, Matrix

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import layout as L

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = argv[0] if argv else "/tmp/carrier_out"
RENDER = "--render" in argv
DECKTEX = argv[argv.index("--deck-tex") + 1] if "--deck-tex" in argv else None
os.makedirs(OUT, exist_ok=True)

bpy.ops.wm.read_factory_settings(use_empty=True)
HT = L.HULL_TOP
DZ = L.DECK_Z

MATS = {}


def mat(name, rgb, rough=0.6, metal=0.0, emit=None):
    if name in MATS:
        return MATS[name]
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = next(n for n in m.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    b.inputs["Base Color"].default_value = (*rgb, 1.0)
    b.inputs["Roughness"].default_value = rough
    b.inputs["Metallic"].default_value = metal
    if emit:
        b.inputs["Emission Color"].default_value = (*emit, 1.0)
        b.inputs["Emission Strength"].default_value = 4.0
    MATS[name] = m
    return m


M_HULL = mat("M_Hull", (0.16, 0.175, 0.185), 0.55)
M_DECK = mat("M_Deck", (0.06, 0.065, 0.07), 0.85)
M_DARK = mat("M_Dark", (0.10, 0.11, 0.12), 0.6)
M_GLASS = mat("M_Glass", (0.02, 0.03, 0.035), 0.08, 0.3)
M_NET = mat("M_Net", (0.05, 0.05, 0.05), 0.9)
M_HANGAR = mat("M_Hangar", (0.45, 0.47, 0.48), 0.6)
M_HFLOOR = mat("M_HangarFloor", (0.10, 0.11, 0.12), 0.8)
M_LAMP = mat("M_Lamp", (0.9, 0.9, 0.85), 0.5, 0.0, (1.0, 0.97, 0.9))
M_JBD = mat("M_Jbd", (0.22, 0.23, 0.24), 0.7, 0.2)

# ---------------------------------------------------------------- mesh builder


class Builder:
    def __init__(self, name):
        self.name = name
        self.bm = bmesh.new()
        self.mats = []
        self.uv = self.bm.loops.layers.uv.new("UVMap")

    def mi(self, m):
        if m not in self.mats:
            self.mats.append(m)
        return self.mats.index(m)

    def face(self, pts, m, uvs=None):
        """pts in UE carrier-local (x fwd, y stbd, z up) -> Blender (y mirrored). Winding given for UE is reversed by the mirror."""
        vs = [self.bm.verts.new((p[0], -p[1], p[2])) for p in pts]
        try:
            f = self.bm.faces.new(vs)
        except ValueError:
            return None
        f.material_index = self.mi(m)
        if uvs is None:
            uvs = [box_uv(p) for p in pts]
        for lp, uv in zip(f.loops, uvs):
            lp[self.uv].uv = uv
        return f

    def quad(self, a, b, c, d, m, uvs=None):
        return self.face([a, b, c, d], m, uvs)

    def box(self, x0, x1, y0, y1, z0, z1, m, top=True, bottom=False):
        P = lambda x, y, z: (x, y, z)
        # outward normals in the UE frame (the mirror flips the winding, so faces are listed clockwise-from-outside in UE)
        self.quad(P(x0, y0, z0), P(x0, y0, z1), P(x0, y1, z1), P(x0, y1, z0), m)   # -X
        self.quad(P(x1, y1, z0), P(x1, y1, z1), P(x1, y0, z1), P(x1, y0, z0), m)   # +X
        self.quad(P(x1, y0, z0), P(x1, y0, z1), P(x0, y0, z1), P(x0, y0, z0), m)   # -Y
        self.quad(P(x0, y1, z0), P(x0, y1, z1), P(x1, y1, z1), P(x1, y1, z0), m)   # +Y
        if top:
            self.quad(P(x0, y0, z1), P(x1, y0, z1), P(x1, y1, z1), P(x0, y1, z1), m)
        if bottom:
            self.quad(P(x0, y1, z0), P(x1, y1, z0), P(x1, y0, z0), P(x0, y0, z0), m)

    def prism(self, poly, z0, z1, m, top=True, bottom=False, deck_uv=False):
        """Extruded polygon (poly clockwise seen from above in UE x-fwd/y-stbd, i.e. counter-clockwise in a right-handed view)."""
        n = len(poly)
        for i in range(n):
            a, b = poly[i], poly[(i + 1) % n]
            self.quad((a[0], a[1], z0), (a[0], a[1], z1), (b[0], b[1], z1), (b[0], b[1], z0), m)
        if top:
            pts = [(p[0], p[1], z1) for p in poly]
            self.face(pts, m, [deck_uv_of(p) for p in pts] if deck_uv else None)
        if bottom:
            pts = [(p[0], p[1], z0) for p in reversed(poly)]
            self.face(pts, m)

    def cyl(self, cx, cy, z0, z1, r, m, seg=12, r_top=None, top=True):
        rt = r if r_top is None else r_top
        ring0 = [(cx + r * math.cos(2 * math.pi * i / seg), cy + r * math.sin(2 * math.pi * i / seg)) for i in range(seg)]
        ring1 = [(cx + rt * math.cos(2 * math.pi * i / seg), cy + rt * math.sin(2 * math.pi * i / seg)) for i in range(seg)]
        for i in range(seg):
            j = (i + 1) % seg
            self.quad((*ring0[i], z0), (*ring1[i], z1), (*ring1[j], z1), (*ring0[j], z0), m)
        if top and rt > 0:
            self.face([(*ring1[i], z1) for i in range(seg)], m)

    def finish(self, collection=None):
        me = bpy.data.meshes.new(self.name)
        bmesh.ops.recalc_face_normals(self.bm, faces=self.bm.faces) if False else None
        self.bm.to_mesh(me)
        self.bm.free()
        for m in self.mats:
            me.materials.append(m)
        ob = bpy.data.objects.new(self.name, me)
        bpy.context.scene.collection.objects.link(ob)
        return ob


def box_uv(p):
    return (p[0] * 0.25 + p[2] * 0.25, p[1] * 0.25 + p[2] * 0.1)


def deck_uv_of(p):
    u = (p[0] - L.TEX_X0) / (L.TEX_X1 - L.TEX_X0)
    v = (p[1] - L.TEX_Y0) / (L.TEX_Y1 - L.TEX_Y0)
    return (u, 1.0 - v)     # glTF/Blender V is bottom-up; the PNG row 0 is the port side


# ---------------------------------------------------------------- hull


def smooth(e0, e1, x):
    t = min(max((x - e0) / (e1 - e0), 0.0), 1.0)
    return t * t * (3 - 2 * t)


def b_wl(x):
    bow = max(0.0, min(1.0, (157.0 - x) / 115.0)) ** 0.62
    stern = 0.70 + 0.30 * smooth(-163.0, -105.0, x)
    return 20.4 * min(bow, stern)


def b_top(x):
    bow = max(0.0, min(1.0, (L.BOW_X - x) / 105.0)) ** 0.42
    stern = 0.90 + 0.10 * smooth(-163.0, -120.0, x)
    return L.HULL_HALF_BEAM_TOP * min(bow, stern)


def z_bot(x):
    if x > 118.0:
        return -11.3 + (11.3 + HT - 0.6) * smooth(118.0, L.BOW_X, x) ** 0.9
    if x < -120.0:
        return -11.3 + 7.5 * smooth(-120.0, -163.0, x)
    return -11.3


Z_LEVELS = [-11.3, -11.0, -10.2, -8.6, -6.4, -3.8, -1.4, 0.0, 1.2, 4.0, 8.4, 12.3, 16.3, HT]


def section(x):
    zb = z_bot(x)
    bw, bt = b_wl(x), b_top(x)
    pts = [(0.0, zb)]
    for zl in Z_LEVELS:
        z = zb + (zl + 11.3) / (HT + 11.3) * (HT - zb)
        if z <= 0.0:
            s = (z - zb) / max(-zb, 1e-3)
            hw = bw * (s ** 0.28 if s > 0 else 0.0)
        else:
            hw = bw + (bt - bw) * (z / HT) ** 1.35
        pts.append((hw, z))
    return pts


XS = sorted(set([round(x, 3) for x in
                 [-163.0 + i * 3.0 for i in range(0, 62)] + [24.0, 50.0] + [x for x in [21.0, 27.0, 33.0, 39.0, 45.0, 53.0]] +
                 [120.0 + i * 2.5 for i in range(0, 20)] + [L.BOW_X - 0.4]]))
XS = [x for x in XS if -163.0 <= x <= L.BOW_X - 0.4]
door = next(e for e in L.ELEVATORS if e.get("working"))
hb = Builder("CarrierHull")
secs = [section(x) for x in XS]
for i in range(len(XS) - 1):
    xa, xb = XS[i], XS[i + 1]
    A, B = secs[i], secs[i + 1]
    for k in range(len(A) - 1):
        for side in (1, -1):
            a0 = (xa, A[k][0] * side, A[k][1]); a1 = (xa, A[k + 1][0] * side, A[k + 1][1])
            b0 = (xb, B[k][0] * side, B[k][1]); b1 = (xb, B[k + 1][0] * side, B[k + 1][1])
            zc = (A[k][1] + A[k + 1][1]) * 0.5
            if side == 1 and door["x0"] <= xa and xb <= door["x1"] and L.HANGAR_FLOOR - 0.01 <= zc <= L.HANGAR_CEIL + 0.01 and A[k][1] >= L.HANGAR_FLOOR - 0.01:
                continue   # hangar bay door behind elevator 2
            if side == 1:
                hb.quad(a0, b0, b1, a1, M_HULL)
            else:
                hb.quad(a0, a1, b1, b0, M_HULL)
# stern transom
st = secs[0]
ring = [(XS[0], p[0], p[1]) for p in st] + [(XS[0], -p[0], p[1]) for p in reversed(st)]
hb.face([(p[0], p[1], p[2]) for p in reversed(ring)], M_HULL)
# stern overhang: close from the transom top to the deck edge
# bow tip closing strip
bt = secs[-1]
for k in range(len(bt) - 1):
    hb.quad((XS[-1], bt[k][0], bt[k][1]), (XS[-1], bt[k + 1][0], bt[k + 1][1]), (XS[-1], -bt[k + 1][0], bt[k + 1][1]), (XS[-1], -bt[k][0], bt[k][1]), M_HULL)
# hangar door jambs
hwz = lambda x, z: next((p[0] for p in section(x) if abs(p[1] - z) < 0.05), 21.0)
yi = L.HANGAR["y1"]
x0d, x1d = door["x0"], door["x1"]
zf, zc_ = L.HANGAR_FLOOR, L.HANGAR_CEIL
hb.quad((x0d, yi, zf), (x0d, hwz(x0d, zf), zf), (x1d, hwz(x1d, zf), zf), (x1d, yi, zf), M_HFLOOR)
hb.quad((x0d, yi, zc_), (x1d, yi, zc_), (x1d, hwz(x1d, zc_), zc_), (x0d, hwz(x0d, zc_), zc_), M_HANGAR)
hb.quad((x0d, yi, zf), (x0d, yi, zc_), (x0d, hwz(x0d, zc_), zc_), (x0d, hwz(x0d, zf), zf), M_HANGAR)
hb.quad((x1d, yi, zf), (x1d, hwz(x1d, zf), zf), (x1d, hwz(x1d, zc_), zc_), (x1d, yi, zc_), M_HANGAR)

# ---------------------------------------------------------------- flight deck slab (outline with elevator notches)


def deck_outline():
    """Outline with the elevator wells cut out (starboard elevators notch the starboard edge, the port one the port edge)."""
    pts = list(L.OUTLINE)
    out = []
    for i in range(len(pts)):
        out.append(pts[i])
        a, b = pts[i], pts[(i + 1) % len(pts)]
        for e in L.ELEVATORS:
            if e.get("port"):
                continue
            # starboard edge runs aft -> fwd (x increasing) in this clockwise list
            if a[1] > 0 and b[1] > 0 and a[0] < e["x0"] < b[0] and a[0] < e["x1"] <= b[0] + 1e-6:
                ya = a[1] + (b[1] - a[1]) * (e["x0"] - a[0]) / (b[0] - a[0])
                yb = a[1] + (b[1] - a[1]) * (e["x1"] - a[0]) / (b[0] - a[0])
                out += [(e["x0"], ya), (e["x0"], e["y0"]), (e["x1"], e["y0"]), (e["x1"], yb)]
    # port elevator: cut on the port edge (list runs fwd -> aft there)
    e = next(e for e in L.ELEVATORS if e.get("port"))
    res = []
    for i in range(len(out)):
        res.append(out[i])
        a, b = out[i], out[(i + 1) % len(out)]
        if a[1] < 0 and b[1] < 0 and b[0] < e["x0"] < a[0] and b[0] <= e["x1"] <= a[0] + 1e-6 and a[0] > e["x1"]:
            ya = a[1] + (b[1] - a[1]) * (e["x1"] - a[0]) / (b[0] - a[0])
            yb = a[1] + (b[1] - a[1]) * (e["x0"] - a[0]) / (b[0] - a[0])
            res += [(e["x1"], ya), (e["x1"], e["y1"]), (e["x0"], e["y1"]), (e["x0"], yb)]
    return res


OUTL = deck_outline()


def triangulate_poly(builder, poly, z, m, uvf, flip=False):
    """Ear-clipping via bmesh: add an ngon then triangulate (concave outline)."""
    bm2 = bmesh.new()
    vs = [bm2.verts.new((p[0], p[1], 0)) for p in poly]
    f = bm2.faces.new(vs)
    res = bmesh.ops.triangulate(bm2, faces=[f], quad_method="BEAUTY", ngon_method="EAR_CLIP")
    for t in res["faces"]:
        tri = [(v.co.x, v.co.y, z) for v in t.verts]
        # keep the UE-frame winding consistent (up-facing top): compute signed area in x/y
        ar = (tri[1][0] - tri[0][0]) * (tri[2][1] - tri[0][1]) - (tri[2][0] - tri[0][0]) * (tri[1][1] - tri[0][1])
        if (ar > 0) != flip:
            tri = [tri[0], tri[2], tri[1]]
        builder.face(tri, m, [uvf(p) for p in tri])
    bm2.free()


db = Builder("CarrierDeck")
triangulate_poly(db, OUTL, DZ, M_DECK, deck_uv_of)
dk = Builder("CarrierDeckSides")
n = len(OUTL)
for i in range(n):
    a, b = OUTL[i], OUTL[(i + 1) % n]
    dk.quad((a[0], a[1], DZ - L.DECK_T), (a[0], a[1], DZ), (b[0], b[1], DZ), (b[0], b[1], DZ - L.DECK_T), M_HULL)
triangulate_poly(dk, OUTL, DZ - L.DECK_T, M_HULL, box_uv, flip=True)

# ---------------------------------------------------------------- sponson skirts under the overhang + catwalks + nets
sk = Builder("CarrierSponsons")
ZS = 9.0


def in_elev(x, y):
    for e in L.ELEVATORS:
        if e["x0"] - 0.5 <= x <= e["x1"] + 0.5 and ((y > 0 and not e.get("port")) or (y < 0 and e.get("port"))):
            return True
    return False


def hull_y(x, z, side):
    s = section(x)
    for k in range(len(s) - 1):
        if s[k][1] <= z <= s[k + 1][1]:
            t = (z - s[k][1]) / max(s[k + 1][1] - s[k][1], 1e-6)
            return side * (s[k][0] + (s[k + 1][0] - s[k][0]) * t)
    return side * s[-1][0]


# resample the plain outline every ~4 m for the skirt
dense = []
for i in range(len(L.OUTLINE)):
    a, b = L.OUTLINE[i], L.OUTLINE[(i + 1) % len(L.OUTLINE)]
    ln = math.hypot(b[0] - a[0], b[1] - a[1])
    k = max(1, int(ln / 4.0))
    for j in range(k):
        t = j / k
        dense.append((a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t))
for i in range(len(dense)):
    a, b = dense[i], dense[(i + 1) % len(dense)]
    if a[0] < -160 or b[0] < -160 or a[0] > 150 or b[0] > 150:
        continue
    if in_elev(a[0], a[1]) or in_elev(b[0], b[1]):
        continue
    sa, sb = (1 if a[1] > 0 else -1), (1 if b[1] > 0 else -1)
    if sa != sb:
        continue
    ha = (a[0], hull_y(a[0], ZS, sa), ZS)
    hbp = (b[0], hull_y(b[0], ZS, sb), ZS)
    top_a = (a[0], a[1], DZ - L.DECK_T - 0.9)
    top_b = (b[0], b[1], DZ - L.DECK_T - 0.9)
    if abs(a[1]) - abs(ha[1]) < 1.0:
        continue
    if sa > 0:
        sk.quad(ha, hbp, top_b, top_a, M_HULL)
    else:
        sk.quad(ha, top_a, top_b, hbp, M_HULL)
    # vertical fascia from the skirt top to the deck plate
    za, zb2 = DZ - L.DECK_T - 0.9, DZ - L.DECK_T
    if sa > 0:
        sk.quad((a[0], a[1], za), (b[0], b[1], za), (b[0], b[1], zb2), (a[0], a[1], zb2), M_HULL)
    else:
        sk.quad((a[0], a[1], za), (a[0], a[1], zb2), (b[0], b[1], zb2), (b[0], b[1], za), M_HULL)

# catwalks (1.6 m below the deck, 1.5 m outboard) with a slanted safety net; skipped at elevators/island/ramp/bow
cw = Builder("CarrierCatwalks")
for i in range(len(dense)):
    a, b = dense[i], dense[(i + 1) % len(dense)]
    if a[0] < -158 or b[0] < -158 or a[0] > 140 or b[0] > 140:
        continue
    if in_elev(a[0], a[1]) or in_elev(b[0], b[1]):
        continue
    if L.ISLAND["x0"] - 2 < a[0] < L.ISLAND["x1"] + 2 and a[1] > 0:
        continue
    ln = math.hypot(b[0] - a[0], b[1] - a[1])
    nx, ny = (b[1] - a[1]) / ln, -(b[0] - a[0]) / ln        # outward normal for the clockwise outline
    zc = DZ - 1.6
    o = 1.5
    A0, B0 = (a[0], a[1], zc), (b[0], b[1], zc)
    A1, B1 = (a[0] + nx * o, a[1] + ny * o, zc), (b[0] + nx * o, b[1] + ny * o, zc)
    cw.quad(A0, B0, B1, A1, M_DARK) if ny < 0 or nx < 0 else cw.quad(A0, A1, B1, B0, M_DARK)
    # net: from deck edge outwards/up to 0.3 m below deck level, 2.2 m out
    N0, N1 = (a[0] + nx * 0.2, a[1] + ny * 0.2, DZ - 0.15), (b[0] + nx * 0.2, b[1] + ny * 0.2, DZ - 0.15)
    N2, N3 = (b[0] + nx * 2.4, b[1] + ny * 2.4, DZ - 0.35), (a[0] + nx * 2.4, a[1] + ny * 2.4, DZ - 0.35)
    cw.quad(N0, N1, N2, N3, M_NET)
    cw.quad(N0, N3, N2, N1, M_NET)
    # rail posts every segment
    cw.box(A1[0] - 0.05, A1[0] + 0.05, A1[1] - 0.05, A1[1] + 0.05, zc, zc + 1.1, M_DARK)

# ---------------------------------------------------------------- island (stepped superstructure, bridge + pri-fly glass, catwalks, mast, radars)
isl = Builder("CarrierIsland")
M_ISL = mat("M_Island", (0.14, 0.155, 0.165), 0.55)
I = L.ISLAND
x0, x1, y0, y1 = I["x0"], I["x1"], I["y0"], I["y1"]
# (x0, x1, y0, y1, z0, z1) tiers, relative to the island footprint; the island is narrow (10 m) and tall
tiers = [
    (x0, x1, y0, y1, 0.0, 3.1), (x0 + 0.3, x1 - 0.3, y0 + 0.2, y1 - 0.1, 3.1, 6.2), (x0 + 0.6, x1 - 0.6, y0 + 0.4, y1 - 0.2, 6.2, 9.3),
    (x0 + 1.5, x1 - 1.2, y0 + 0.6, y1 - 0.4, 9.3, 12.4), (x0 + 2.5, x1 - 2.0, y0 + 0.8, y1 - 0.6, 12.4, 15.4),
]
for (a, b, c, d, za, zb) in tiers:
    isl.box(a, b, c, d, DZ + za, DZ + zb, M_ISL)
    # deck lip + safety rail round each level (catwalk edge)
    if za > 0.0:
        isl.box(a - 0.5, b + 0.5, c - 0.55, c, DZ + za - 0.12, DZ + za, M_DARK)
        isl.box(a - 0.5, b + 0.5, c - 0.55, c - 0.5, DZ + za, DZ + za + 1.05, M_NET)
    # square windows (4 per bay) on the inboard, fore and aft faces
    zw = DZ + za + 1.55
    nx = int((b - a - 1.0) / 1.6)
    for k in range(nx):
        wx = a + 0.8 + k * 1.6
        if k % 4 == 3:
            continue
        isl.box(wx, wx + 0.55, c - 0.03, c + 0.01, zw, zw + 0.55, M_GLASS, top=False)
        isl.box(wx, wx + 0.55, d - 0.01, d + 0.03, zw, zw + 0.55, M_GLASS, top=False)
    # vertical ribs / ladders on the outboard face
    for k in range(int((b - a) / 3.2)):
        rx = a + 1.0 + k * 3.2
        isl.box(rx, rx + 0.12, d, d + 0.18, DZ + za, DZ + zb, M_DARK, top=False)
# watertight doors along the deck-level inboard face
for k in range(5):
    dx = x0 + 3.0 + k * 6.0
    isl.box(dx - 0.55, dx + 0.55, y0 - 0.06, y0 + 0.02, DZ, DZ + 1.95, M_DARK, top=False)
# navigation bridge (forward) with a raked glass band + bridge wings
bx0, bx1 = x1 - 12.0, x1 - 1.0
isl.box(bx0, bx1, y0 - 0.2, y1 + 0.2, DZ + 15.4, DZ + 18.6, M_ISL)
isl.quad((bx1, y0 - 0.2, DZ + 16.0), (bx1 + 1.0, y0 - 0.2, DZ + 18.0), (bx1 + 1.0, y1 + 0.2, DZ + 18.0), (bx1, y1 + 0.2, DZ + 16.0), M_GLASS)
isl.quad((bx0, y0 - 0.2, DZ + 16.0), (bx1, y0 - 0.2, DZ + 16.0), (bx1 + 1.0, y0 - 0.2, DZ + 18.0), (bx0, y0 - 0.2, DZ + 18.0), M_ISL)
isl.box(bx0 + 0.5, bx1 - 0.5, y0 - 0.25, y0 - 0.18, DZ + 16.2, DZ + 17.8, M_GLASS, top=False)
isl.box(bx0 + 3.0, bx1, y0 - 3.8, y0 - 0.2, DZ + 15.9, DZ + 16.25, M_ISL)
isl.box(bx0 + 3.0, bx1, y0 - 3.8, y0 - 3.7, DZ + 16.25, DZ + 17.3, M_NET)
isl.box(bx0 + 3.0, bx1, y1 + 0.2, y1 + 2.6, DZ + 15.9, DZ + 16.25, M_ISL)
# primary flight control (aft), cantilevered over the deck with glass on three sides
px0, px1 = x0 + 0.5, x0 + 12.5
isl.box(px0, px1, y0 - 2.2, y1 - 0.6, DZ + 15.4, DZ + 19.4, M_ISL)
isl.box(px0 - 0.02, px1 + 0.02, y0 - 2.23, y0 - 2.12, DZ + 16.3, DZ + 18.6, M_GLASS, top=False)
isl.box(px0 - 0.1, px0 + 0.02, y0 - 2.2, y1 - 1.0, DZ + 16.3, DZ + 18.6, M_GLASS, top=False)
isl.quad((px0, y0 - 2.2, DZ + 15.4), (px1, y0 - 2.2, DZ + 15.4), (px1, y0 + 1.0, DZ + 13.8), (px0, y0 + 1.0, DZ + 13.8), M_ISL)   # knee brace
# upper decks, stack of equipment rooms, mast
isl.box(x0 + 3.0, x1 - 3.0, y0 + 1.0, y1 - 1.0, DZ + 19.4, DZ + 22.4, M_ISL)
isl.box(x0 + 6.0, x1 - 8.0, y0 + 2.0, y1 - 1.8, DZ + 22.4, DZ + 25.2, M_ISL)
isl.box(x0 + 2.5, x1 - 2.5, y0 + 0.6, y0 + 1.0, DZ + 22.3, DZ + 22.45, M_DARK)
mx, my = (x0 + x1) * 0.5 - 1.5, (y0 + y1) * 0.5 + 0.3
for dx, dy in [(-1.6, -1.3), (1.6, -1.3), (-1.6, 1.3), (1.6, 1.3)]:
    isl.cyl(mx + dx, my + dy, DZ + 25.2, DZ + 41.0, 0.32, M_DARK, 8, 0.16)
for k, zz in enumerate([DZ + 28.5, DZ + 32.5, DZ + 36.5]):
    w = 2.6 - k * 0.5
    isl.box(mx - w, mx + w, my - w * 0.9, my + w * 0.9, zz, zz + 0.3, M_DARK)
    isl.box(mx - w, mx + w, my - w * 0.9 - 0.05, my - w * 0.9, zz + 0.3, zz + 1.1, M_NET)
    for dx in (-w * 0.8, w * 0.8):
        isl.cyl(mx + dx, my, zz + 0.3, zz + 1.2, 0.35, M_ISL, 10)
isl.cyl(mx, my, DZ + 41.0, DZ + 49.0, 0.16, M_DARK, 6, 0.05)
for k in range(4):
    zz = DZ + 33.6 + k * 2.8
    isl.box(mx - 0.08, mx + 0.08, my - 4.5 + k * 0.7, my + 4.5 - k * 0.7, zz, zz + 0.12, M_DARK)
    for sg in (-1, 1):
        isl.box(mx - 0.03, mx + 0.03, my + sg * (4.3 - k * 0.7) - 0.03, my + sg * (4.3 - k * 0.7) + 0.03, zz - 1.2, zz, M_DARK)
# 3-D air search radar (planar array, tilted back) on a pedestal, forward top
ax_, ay_ = x1 - 7.5, my
isl.cyl(ax_, ay_, DZ + 25.2, DZ + 26.6, 0.7, M_DARK, 12)
arr = [(ax_ + 0.0, ay_ - 2.7, DZ + 26.8), (ax_ + 0.0, ay_ + 2.7, DZ + 26.8), (ax_ - 0.9, ay_ + 2.7, DZ + 31.9), (ax_ - 0.9, ay_ - 2.7, DZ + 31.9)]
isl.quad(*arr, M_DARK)
isl.quad(arr[0], arr[3], arr[2], arr[1], M_DARK)
for k in range(9):
    zz = DZ + 27.2 + k * 0.52
    isl.box(ax_ - 0.12 - (zz - DZ - 26.8) * 0.176, ax_ - 0.02 - (zz - DZ - 26.8) * 0.176, ay_ - 2.65, ay_ + 2.65, zz, zz + 0.06, M_ISL, top=False)
# 2-D air search (long rectangular antenna) aft top on a mast platform
bx_ = x0 + 5.0
isl.cyl(bx_, my, DZ + 25.2, DZ + 27.4, 0.5, M_DARK, 10)
isl.box(bx_ - 0.35, bx_ + 0.35, my - 3.9, my + 3.9, DZ + 27.4, DZ + 29.8, M_DARK)
for k in range(6):
    yy = my - 3.6 + k * 1.44
    isl.box(bx_ - 0.5, bx_ + 0.5, yy, yy + 0.08, DZ + 27.5, DZ + 29.7, M_ISL, top=False)
# SATCOM radomes and small arrays
for (cx, cy, zz, r) in [(x0 + 11.5, y1 - 2.2, DZ + 22.4, 1.2), (x1 - 12.0, y0 + 2.6, DZ + 22.4, 1.0), (mx + 3.4, my + 2.4, DZ + 25.2, 0.8),
                        (mx - 3.5, my - 2.2, DZ + 25.2, 0.7)]:
    isl.cyl(cx, cy, zz, zz + r, r, M_HULL, 14, r)
    isl.cyl(cx, cy, zz + r, zz + r * 1.8, r, M_HULL, 14, 0.12 * r)
# whip antennas + light masts on the island edges
for (wx, wy, zz, h) in [(x1 - 2.0, y1 - 0.5, DZ + 18.6, 6.0), (x0 + 1.0, y1 - 0.3, DZ + 19.4, 7.5), (x0 + 16.0, y1 - 0.4, DZ + 22.4, 5.0),
                        (x1 - 4.0, y0 + 0.9, DZ + 25.2, 4.0)]:
    isl.cyl(wx, wy, zz, zz + h, 0.06, M_DARK, 6, 0.02)
# outboard equipment boxes, exhaust grilles, stair towers
for k in range(7):
    isl.box(x0 + 2 + k * 4.4, x0 + 3.6 + k * 4.4, y1, y1 + 0.55, DZ + 2.5 + (k % 3) * 2.6, DZ + 4.2 + (k % 3) * 2.6, M_DARK)
for k in range(3):
    isl.box(x0 + 8 + k * 7.5, x0 + 10 + k * 7.5, y0 - 0.02, y0 + 0.05, DZ + 10.0, DZ + 11.2, M_NET, top=False)
isl.box(x0 - 2.2, x0, y0 + 1.0, y0 + 4.0, DZ, DZ + 9.3, M_ISL)                   # aft stair tower
isl.box(x0 - 2.3, x0 - 2.2, y0 + 1.2, y0 + 3.8, DZ + 0.5, DZ + 9.0, M_NET, top=False)

# ---------------------------------------------------------------- deck-edge fittings: CIWS/RAM mounts, antennas, liferafts
fit = Builder("CarrierFittings")
for (fx, fy, side) in [(150.0, 10.0, 1), (145.0, -12.0, -1), (-150.0, 22.0, 1), (-148.0, -18.5, -1)]:
    zz = DZ - 3.2
    fit.box(fx - 3, fx + 3, fy + side * 1.0 - 3, fy + side * 1.0 + 3, zz - 0.4, zz, M_HULL)
    fit.cyl(fx, fy + side * 1.0, zz, zz + 1.6, 1.3, M_HULL, 14)
    fit.cyl(fx, fy + side * 1.0, zz + 1.6, zz + 3.0, 1.0, M_HULL, 14, 0.9)
    fit.cyl(fx, fy + side * 1.0, zz + 3.0, zz + 3.6, 0.9, M_HULL, 14, 0.2)
for k in range(10):                                                                  # liferaft canisters on the sponsons
    xx = -140 + k * 4.0
    fit.cyl(xx, -25.5 + k * 0.62, DZ - 3.2, DZ - 2.6, 0.35, M_HULL, 8)
    fit.cyl(xx + 70, 32.6, DZ - 3.0, DZ - 2.4, 0.35, M_HULL, 8)
for (ax, ay) in [(120.0, 24.0), (95.0, 29.0), (-100.0, 31.5), (-130.0, 29.0), (0.0, -46.0), (100.0, -20.5)]:
    fit.cyl(ax, ay + (1.2 if ay > 0 else -1.2), DZ - 1.0, DZ + 6.0, 0.08, M_DARK, 6, 0.03)
# stern: aft sponson + drop-light mast outline
fit.box(-164.5, -163.0, -2.5, 8.5, DZ - 8.0, DZ - 0.8, M_HULL)
# IFLOLS housing (lights are added in UE)
ix, iy, iz = L.IFLOLS
fit.box(ix - 0.8, ix + 0.8, iy - 0.8, iy + 0.8, DZ - 1.0, iz - 0.9, M_DARK)
fit.box(ix - 0.5, ix + 0.5, iy - 2.4, iy + 2.4, iz - 0.9, iz + 1.9, M_DARK)
# LSO platform (port aft) with wind screen
lx, ly = L.LSO_PLATFORM
fit.box(lx - 3.5, lx + 3.5, ly - 4.5, ly - 0.5, DZ - 0.6, DZ - 0.3, M_DARK)
fit.box(lx - 3.5, lx + 3.5, ly - 4.6, ly - 4.4, DZ - 0.3, DZ + 1.0, M_NET)

# ---------------------------------------------------------------- hangar interior (inward faces), lamps
hg = Builder("CarrierHangar")
H = L.HANGAR
hx0, hx1, hy0, hy1 = H["x0"], H["x1"], H["y0"], H["y1"]
zf, zc_ = L.HANGAR_FLOOR, L.HANGAR_CEIL
hg.quad((hx0, hy0, zf), (hx0, hy1, zf), (hx1, hy1, zf), (hx1, hy0, zf), M_HFLOOR)     # floor (up)
hg.quad((hx0, hy0, zc_), (hx1, hy0, zc_), (hx1, hy1, zc_), (hx0, hy1, zc_), M_HANGAR)   # ceiling (down)
hg.quad((hx0, hy0, zf), (hx1, hy0, zf), (hx1, hy0, zc_), (hx0, hy0, zc_), M_HANGAR)     # port wall (faces +Y)
# starboard wall with the door gap
for (a, b) in [(hx0, door["x0"]), (door["x1"], hx1)]:
    hg.quad((a, hy1, zf), (a, hy1, zc_), (b, hy1, zc_), (b, hy1, zf), M_HANGAR)
hg.quad((hx0, hy0, zf), (hx0, hy0, zc_), (hx0, hy1, zc_), (hx0, hy1, zf), M_HANGAR)
hg.quad((hx1, hy0, zf), (hx1, hy1, zf), (hx1, hy1, zc_), (hx1, hy0, zc_), M_HANGAR)
for k in range(int((hx1 - hx0) / 8.0)):                                               # ceiling beams + lamps
    bxp = hx0 + 4 + k * 8.0
    hg.box(bxp - 0.25, bxp + 0.25, hy0, hy1, zc_ - 0.7, zc_, M_HANGAR, top=False, bottom=True)
    for ly_ in (hy0 + 8, (hy0 + hy1) / 2, hy1 - 8):
        hg.box(bxp + 3.0, bxp + 4.2, ly_ - 0.25, ly_ + 0.25, zc_ - 0.15, zc_, M_LAMP, top=False, bottom=True)
# yellow safety line on the hangar floor
for (a, b) in [(hx0 + 2, hx1 - 2)]:
    hg.quad((a, hy1 - 3.0, zf + 0.01), (b, hy1 - 3.0, zf + 0.01), (b, hy1 - 2.8, zf + 0.01), (a, hy1 - 2.8, zf + 0.01), MATS.setdefault("M_Yellow", mat("M_Yellow", (0.8, 0.6, 0.1), 0.6)))

# ---------------------------------------------------------------- elevator platform (one mesh, pivot at its deck-level centre)
ev = Builder("CarrierElevator")
e2 = door
ew, el = e2["y1"] - e2["y0"], e2["x1"] - e2["x0"]
ex0, ey0 = -el / 2, -ew / 2
pts = [(ex0, ey0, 0.0), (ex0 + el, ey0, 0.0), (ex0 + el, ey0 + ew, 0.0), (ex0, ey0 + ew, 0.0)]
cx_, cy_ = (e2["x0"] + e2["x1"]) / 2, (e2["y0"] + e2["y1"]) / 2
ev.face([pts[0], pts[3], pts[2], pts[1]][::-1], M_DECK, [deck_uv_of((p[0] + cx_, p[1] + cy_)) for p in [pts[0], pts[3], pts[2], pts[1]][::-1]])
ev.prism([(p[0], p[1]) for p in pts], -0.9, 0.0, M_HULL, top=False, bottom=True)

# ---------------------------------------------------------------- JBD panel (pivot at the hinge, panel lies along +X, width along Y)
jb = Builder("CarrierJbd")
jw, jl = L.JBD_WIDTH, L.JBD_LEN
jb.box(0.0, jl, -jw / 2, jw / 2, -0.25, 0.0, M_JBD, top=True, bottom=True)
for k in range(int(jw / 1.1)):                                                        # cooling panel ribs
    yy = -jw / 2 + 0.3 + k * 1.1
    jb.box(0.2, jl - 0.2, yy, yy + 0.08, 0.0, 0.05, M_DARK)
for k in range(3):                                                                    # hydraulic rams (under)
    yy = -jw / 2 + 1.5 + k * (jw - 3.0) / 2
    jb.box(1.0, 2.6, yy - 0.15, yy + 0.15, -0.55, -0.25, M_DARK)

# ---------------------------------------------------------------- collision (UE: complex-as-simple, hidden)
co = Builder("CarrierCollision")
triangulate_poly(co, OUTL, DZ, M_DARK, box_uv)
triangulate_poly(co, OUTL, DZ - L.DECK_T, M_DARK, box_uv, flip=True)
for i in range(len(OUTL)):
    a, b = OUTL[i], OUTL[(i + 1) % len(OUTL)]
    co.quad((a[0], a[1], DZ - L.DECK_T), (a[0], a[1], DZ), (b[0], b[1], DZ), (b[0], b[1], DZ - L.DECK_T), M_DARK)
co.box(x0, x1, y0, y1, DZ - 1, DZ + 25.0, M_DARK)
# low edge coaming so walkers stop at the deck edge (0.5 m; the real deck has a small lip + nets)
for i in range(len(OUTL)):
    a, b = OUTL[i], OUTL[(i + 1) % len(OUTL)]
    if in_elev(a[0], a[1]) and in_elev(b[0], b[1]):
        continue
    co.quad((a[0], a[1], DZ), (a[0], a[1], DZ + 1.0), (b[0], b[1], DZ + 1.0), (b[0], b[1], DZ), M_DARK)
    co.quad((b[0], b[1], DZ), (b[0], b[1], DZ + 1.0), (a[0], a[1], DZ + 1.0), (a[0], a[1], DZ), M_DARK)
# hangar floor/walls/ceiling as thin boxes (door floor strip reaches the hull side)
co.box(hx0, hx1, hy0, hy1, zf - 0.5, zf, M_DARK)
co.box(door["x0"], door["x1"], hy1 - 0.5, door["y0"] + 0.4, zf - 0.5, zf, M_DARK)
co.box(hx0, hx1, hy0, hy1, zc_, zc_ + 0.5, M_DARK)
co.box(hx0, hx1, hy0 - 0.5, hy0, zf, zc_, M_DARK)
co.box(hx0 - 0.5, hx0, hy0, hy1, zf, zc_, M_DARK)
co.box(hx1, hx1 + 0.5, hy0, hy1, zf, zc_, M_DARK)
co.box(hx0, door["x0"], hy1, hy1 + 0.5, zf, zc_, M_DARK)
co.box(door["x1"], hx1, hy1, hy1 + 0.5, zf, zc_, M_DARK)

# ---------------------------------------------------------------- helmet (cranial / flight helmet), modelled facing UE +X, origin at the skull centre
hm_ = Builder("Helmet")
M_SHELL = mat("M_HelmetShell", (0.8, 0.8, 0.8), 0.45)
M_VISOR = mat("M_Visor", (0.02, 0.02, 0.025), 0.05, 0.5)
NU, NV = 20, 10
def shell_pt(u, v, r=(12.6, 11.0, 13.2)):
    # u around (0..2pi), v from the crown (0) to the rim (1); cut open at the face (front, lower half)
    th = v * math.radians(118)
    return (r[0] * math.sin(th) * math.cos(u) * 0.01, r[1] * math.sin(th) * math.sin(u) * 0.01, (r[2] * math.cos(th) + 1.5) * 0.01)
for i in range(NU):
    for j in range(NV):
        u0, u1 = 2 * math.pi * i / NU, 2 * math.pi * (i + 1) / NU
        v0, v1 = j / NV, (j + 1) / NV
        uc = (u0 + u1) / 2
        front = math.cos(uc) > 0.55 and v0 >= 0.55
        if front:
            continue
        a, b_, c, d = shell_pt(u0, v0), shell_pt(u1, v0), shell_pt(u1, v1), shell_pt(u0, v1)
        hm_.quad(a, b_, c, d, M_SHELL)
        hm_.quad(a, d, c, b_, M_SHELL)
# visor: a curved dark band across the face opening
for i in range(NU):
    u0, u1 = 2 * math.pi * i / NU, 2 * math.pi * (i + 1) / NU
    if math.cos((u0 + u1) / 2) <= 0.5:
        continue
    for j in range(5, 9):
        v0, v1 = j / NV, (j + 1) / NV
        R = (13.3, 11.7, 13.9)
        a, b_, c, d = shell_pt(u0, v0, R), shell_pt(u1, v0, R), shell_pt(u1, v1, R), shell_pt(u0, v1, R)
        hm_.quad(a, b_, c, d, M_VISOR)
        hm_.quad(a, d, c, b_, M_VISOR)
# ear cups
for sgn in (-1, 1):
    hm_.cyl(0.0, sgn * 0.118, -0.03, -0.029, 0.05, M_SHELL, 10) if False else None
    hm_.box(-0.04, 0.04, sgn * 0.112 - 0.018, sgn * 0.112 + 0.018, -0.07, 0.01, M_SHELL, top=True, bottom=True)

objs = {b.name: b.finish() for b in [hb, db, dk, sk, cw, isl, fit, hg, ev, jb, co, hm_]}
print("INFO objects", {k: len(v.data.polygons) for k, v in objs.items()})
tris = sum(sum(len(p.vertices) - 2 for p in o.data.polygons) for o in objs.values())
print("INFO tris", tris)

# fix normals consistently outward (mirrored builder winding is easy to get wrong) except for the hangar interior and double-sided bits
for name, o in objs.items():
    if name in ("CarrierHangar", "CarrierCollision", "Helmet"):
        continue
    bm = bmesh.new(); bm.from_mesh(o.data)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=0.001)
    bm.to_mesh(o.data); bm.free()


def export(names, fname):
    """One static mesh per file: copies of the parts are joined into a single object (all at the world origin)."""
    bpy.ops.object.select_all(action="DESELECT")
    copies = []
    for n in names:
        c = objs[n].copy(); c.data = objs[n].data.copy()
        bpy.context.scene.collection.objects.link(c)
        copies.append(c)
    for c in copies:
        c.select_set(True)
    bpy.context.view_layer.objects.active = copies[0]
    if len(copies) > 1:
        bpy.ops.object.join()
    j = bpy.context.view_layer.objects.active
    j.name = os.path.splitext(fname)[0]
    assert all(abs(v) < 1e-6 for v in j.location), "joined object must sit at the origin"
    bpy.ops.object.select_all(action="DESELECT")
    j.select_set(True)
    bpy.ops.export_scene.gltf(filepath=os.path.join(OUT, fname), export_format="GLB", use_selection=True, export_apply=True)
    j.hide_render = True


export(["CarrierHull", "CarrierDeckSides", "CarrierSponsons", "CarrierCatwalks", "CarrierIsland", "CarrierFittings"], "SM_CarrierHull.glb")
export(["CarrierDeck"], "SM_CarrierDeck.glb")
export(["CarrierHangar"], "SM_CarrierHangar.glb")
export(["CarrierElevator"], "SM_CarrierElevator.glb")
export(["CarrierJbd"], "SM_CarrierJbd.glb")
export(["CarrierCollision"], "SM_CarrierCollision.glb")
export(["Helmet"], "SM_Helmet.glb")
print("INFO exported to", OUT)

# ---------------------------------------------------------------- optional candidate renders
if RENDER:
    objs["CarrierCollision"].hide_render = True
    sc = bpy.context.scene
    try:
        sc.render.engine = "BLENDER_EEVEE_NEXT"
    except TypeError:
        sc.render.engine = "BLENDER_EEVEE"
    sc.render.resolution_x, sc.render.resolution_y = 1280, 720
    if DECKTEX and os.path.exists(DECKTEX):
        img = bpy.data.images.load(DECKTEX)
        nt = M_DECK.node_tree
        b = next(n for n in nt.nodes if n.type == "BSDF_PRINCIPLED")
        tx = nt.nodes.new("ShaderNodeTexImage"); tx.image = img
        nt.links.new(tx.outputs["Color"], b.inputs["Base Color"])
        M_DECK.node_tree.nodes  # keep
        # elevator top uses the same deck material
    world = bpy.data.worlds.new("W"); sc.world = world; world.use_nodes = True
    bg = world.node_tree.nodes.get("Background")
    sky = world.node_tree.nodes.new("ShaderNodeTexSky")
    try:
        sky.sky_type = "NISHITA"; sky.sun_elevation = math.radians(40); sky.sun_rotation = math.radians(200)
    except Exception:
        pass
    world.node_tree.links.new(sky.outputs["Color"], bg.inputs["Color"])
    bg.inputs["Strength"].default_value = 0.35
    sun = bpy.data.objects.new("Sun", bpy.data.lights.new("Sun", "SUN"))
    sun.data.energy = 4.0
    sun.rotation_euler = (math.radians(50), 0, math.radians(200))
    sc.collection.objects.link(sun)
    bpy.ops.mesh.primitive_plane_add(size=6000, location=(0, 0, 0))
    sea = bpy.context.active_object
    ms = mat("M_Sea", (0.01, 0.05, 0.08), 0.12)
    sea.data.materials.append(ms)
    # hull: waterline boot-top + anti-fouling below (object Z), same bands as M_CarrierHull in UE
    nt = M_HULL.node_tree
    b = next(n for n in nt.nodes if n.type == "BSDF_PRINCIPLED")
    geo = nt.nodes.new("ShaderNodeNewGeometry"); sep = nt.nodes.new("ShaderNodeSeparateXYZ"); ramp = nt.nodes.new("ShaderNodeValToRGB")
    nt.links.new(geo.outputs["Position"], sep.inputs[0]); nt.links.new(sep.outputs["Z"], ramp.inputs[0])
    ramp.color_ramp.interpolation = "CONSTANT"
    e = ramp.color_ramp.elements
    e[0].position = 0.0; e[0].color = (0.10, 0.025, 0.02, 1)
    e[1].position = 0.03; e[1].color = (0.16, 0.175, 0.185, 1)
    e.new(0.0).color = (0.10, 0.025, 0.02, 1)
    # map z in [-20, 40] -> [0,1]: boot top from -1.2 m (0.313) to +1.6 m (0.36)
    mr = nt.nodes.new("ShaderNodeMapRange"); mr.inputs[1].default_value = -20; mr.inputs[2].default_value = 40
    nt.links.new(sep.outputs["Z"], mr.inputs[0]); nt.links.new(mr.outputs[0], ramp.inputs[0])
    ramp.color_ramp.elements[0].position = 0.0
    el = ramp.color_ramp.elements
    while len(el) > 1: el.remove(el[-1])
    el[0].color = (0.10, 0.025, 0.02, 1)
    k1 = el.new(0.313); k1.color = (0.012, 0.012, 0.012, 1)
    k2 = el.new(0.36); k2.color = (0.16, 0.175, 0.185, 1)
    nt.links.new(ramp.outputs["Color"], b.inputs["Base Color"])
    views = [((-300, 230, 140), (0, 0, 10)), ((330, -250, 90), (0, 0, 10)), ((-260, -40, 38), (-100, -10, 20)), ((20, 0, 420), (0, 0, 0)), ((25, -10, 28), (-24, 28, 32)), ((-70, 5, 23), (-24, 26, 30))]
    paths = []
    for k, (loc, tgt) in enumerate(views):
        cam = bpy.data.objects.new(f"Cam{k}", bpy.data.cameras.new(f"Cam{k}"))
        sc.collection.objects.link(cam)
        cam.location = Vector(loc) * Vector((1, -1, 1))
        d = (Vector(tgt) * Vector((1, -1, 1)) - cam.location)
        cam.rotation_euler = d.to_track_quat("-Z", "Y").to_euler() if k != 3 else (0.0, 0.0, math.radians(-90))
        cam.data.lens = 24 if k == 3 else 35
        cam.data.clip_end = 20000
        sc.camera = cam
        p = os.path.join(OUT, f"render_{k}.png")
        sc.render.filepath = p
        bpy.ops.render.render(write_still=True)
        paths.append(p)
    print("INFO renders", paths)
