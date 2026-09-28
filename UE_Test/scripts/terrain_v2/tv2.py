# Terrain v2 shared helpers: world frame constants, ctypes bindings to kernels.c, resampling, simple image IO.
# World frame = Godot frame used by the C++ game code: x east, z south, y up, metres; the map spans [-40 km, 40 km) on x and z,
# grid sample i sits at -40 km + i * cell (row = z, column = x).
import ctypes, json, os, struct, subprocess
import numpy as np
from scipy import ndimage

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))           # Operation Silent Ridge/
WORK = os.path.join(ROOT, "UE_Test", "Niepotrzebne", "terrain_v2")      # generated data (not in git)
os.makedirs(WORK, exist_ok=True)

MAP = 80000.0
X0 = -40000.0
N2 = 4096
CELL2 = MAP / N2                     # 19.53125 m global grid
CORE_CELL = CELL2 / 4.0              # 4.8828125 m refined core
# refined core box (aligned to 5 km tiles and to the global grid)
CORE_X = (-25000.0, 15000.0)
CORE_Z = (-15000.0, 15000.0)
CORE_W = int(round((CORE_X[1] - CORE_X[0]) / CORE_CELL))   # 8192
CORE_H = int(round((CORE_Z[1] - CORE_Z[0]) / CORE_CELL))   # 6144
# fixed sun (same as stage 1/2: elevation 40 deg, azimuth 200 deg; Godot TO_SUN)
TO_SUN = np.array([-0.26200, 0.64279, 0.71985])

FP = ctypes.POINTER(ctypes.c_float)
U8P = ctypes.POINTER(ctypes.c_uint8)
IP = ctypes.POINTER(ctypes.c_int)


def _lib():
    src = os.path.join(HERE, "kernels.c")
    lib = os.path.join(WORK, "libtv2.so")
    if not os.path.exists(lib) or os.path.getmtime(lib) < os.path.getmtime(src):
        subprocess.check_call(["cc", "-O3", "-ffast-math", "-shared", "-fPIC", "-o", lib, src, "-lpthread", "-lm"])
    k = ctypes.CDLL(lib)
    c_i, c_f, c_d, c_l, c_u = ctypes.c_int, ctypes.c_float, ctypes.c_double, ctypes.c_long, ctypes.c_uint32
    k.fbm.argtypes = [FP, c_i, c_i, c_d, c_d, c_d, c_d, c_i, c_f, c_f, c_u, c_i, c_f, c_d]
    k.stream_power2.argtypes = [FP, FP, FP, U8P, FP, FP, c_i, c_i, c_f, c_i, c_f, c_f, c_f, c_i, c_f, FP]
    k.thermal.argtypes = [FP, FP, c_i, c_i, c_f, c_i, c_f, FP]
    k.mfd_area.argtypes = [FP, U8P, c_i, c_i, c_f, c_f, FP]
    k.drainage_area.argtypes = [FP, U8P, c_i, c_i, c_f, FP]
    k.receivers.argtypes = [FP, U8P, c_i, c_i, IP, IP, FP]
    k.bake_ao.argtypes = [FP, c_i, c_i, c_f, c_i, c_f, FP]
    k.bake_shadow.argtypes = [FP, c_i, c_i, c_f, c_f, c_f, c_f, c_f, c_f, FP]
    k.fbm_points.argtypes = [FP, FP, FP, c_l, c_d, c_i, c_f, c_f, c_u, c_i]
    k.ice_flux.argtypes = [IP, IP, FP, c_l, c_f, FP]
    k.monotone_bed.argtypes = [IP, IP, U8P, FP, c_l, c_f]
    k.dist_downstream.argtypes = [IP, IP, c_l, c_i, c_f, FP]
    k.accum.argtypes = [IP, IP, FP, FP, c_l, FP]
    k.set_amax.argtypes = [c_f]
    k.set_flow.argtypes = [c_f, c_i]
    k.render.argtypes = [FP, c_i, c_i, c_f, c_f, c_f, FP, FP, c_i, c_i, c_f, c_f, c_f, FP,
                         FP, FP, FP, FP, c_f, c_i, c_i, FP, c_f, FP, FP, FP, FP, FP]
    return k


K = _lib()


def fp(a):
    assert a.dtype == np.float32 and a.flags["C_CONTIGUOUS"], (a.dtype, a.flags)
    return a.ctypes.data_as(FP)


def u8p(a):
    assert a.dtype == np.uint8 and a.flags["C_CONTIGUOUS"]
    return a.ctypes.data_as(U8P)


def f32(a):
    return np.ascontiguousarray(a, dtype=np.float32)


def noise(w, h, x0, z0, dx, scale, octaves=6, lac=2.0, gain=0.5, seed=1, mode=0, warp=0.0, warp_scale=1.0):
    """fBm (mode 0, [-1,1]), ridged (1, [0,1]) or billow (2, [0,1]) sampled on a grid whose sample (0,0) is at world (x0, z0)."""
    out = np.empty((h, w), np.float32)
    K.fbm(fp(out), w, h, x0, z0, dx, scale, octaves, lac, gain, seed, mode, warp, warp_scale)
    return out


def grid_noise(shape, x0, z0, cell, scale, **kw):
    return noise(shape[1], shape[0], x0, z0, cell, scale, **kw)


def stream_power(h, uplift, kmul, outlet, talus, ext, cell, iters, dt, Kf, m, therm_iters=1, therm_rate=0.5):
    A = np.empty_like(h)
    K.stream_power2(fp(h), fp(uplift) if uplift is not None else None, fp(kmul) if kmul is not None else None, u8p(outlet),
                    fp(talus) if talus is not None else None, fp(ext) if ext is not None else None,
                    h.shape[1], h.shape[0], cell, iters, dt, Kf, m, therm_iters, therm_rate, fp(A))
    return A


def thermal(h, talus, cell, iters, rate=0.5, dep=None):
    K.thermal(fp(h), fp(talus), h.shape[1], h.shape[0], cell, iters, rate, fp(dep) if dep is not None else None)


def mfd(h, outlet, cell, p=1.1):
    A = np.empty_like(h)
    K.mfd_area(fp(h), u8p(outlet), h.shape[1], h.shape[0], cell, p, fp(A))
    return A


def receivers(h, outlet):
    n = h.size
    rec = np.empty(n, np.int32); order = np.empty(n, np.int32); filled = np.empty_like(h)
    K.receivers(fp(h), u8p(outlet), h.shape[1], h.shape[0], rec.ctypes.data_as(IP), order.ctypes.data_as(IP), fp(filled))
    return rec, order, filled


def ao(h, cell, dirs=16, maxdist=3000.0):
    out = np.empty_like(h)
    K.bake_ao(fp(h), h.shape[1], h.shape[0], cell, dirs, maxdist, fp(out))
    return out


def sun_vis(h, cell, pen_deg=2.5, maxdist=12000.0):
    s = TO_SUN
    hor = np.hypot(s[0], s[2])
    out = np.empty_like(h)
    K.bake_shadow(fp(h), h.shape[1], h.shape[0], cell, float(s[0] / hor), float(s[2] / hor), float(np.arcsin(s[1])),
                  float(np.radians(pen_deg)), maxdist, fp(out))
    return out


def resample(a, shape, order=3):
    """Node-aligned resample: sample i of the output sits at i * (in/out) of the input (same origin)."""
    zy = np.arange(shape[0]) * (a.shape[0] / shape[0])
    zx = np.arange(shape[1]) * (a.shape[1] / shape[1])
    yy, xx = np.meshgrid(zy, zx, indexing="ij")
    return ndimage.map_coordinates(a, [yy, xx], order=order, mode="nearest").astype(np.float32)


def sample_world(a, cell, x, z, x0=X0, z0=X0, order=1):
    """Sample a grid (origin x0/z0, spacing cell) at world coordinates."""
    u = (np.asarray(x, np.float64) - x0) / cell
    v = (np.asarray(z, np.float64) - z0) / cell
    return ndimage.map_coordinates(a, [np.atleast_1d(v), np.atleast_1d(u)], order=order, mode="nearest")


def normals(h, cell):
    gz, gx = np.gradient(h, cell)
    n = np.stack([-gx, np.ones_like(h), -gz], -1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    return n.astype(np.float32)


def smoothstep(a, b, x):
    t = np.clip((x - a) / (b - a), 0.0, 1.0)
    return t * t * (3 - 2 * t)


def save(name, **arrs):
    np.savez(os.path.join(WORK, name + ".npz"), **arrs)


def load(name):
    return dict(np.load(os.path.join(WORK, name + ".npz")))


def world_axes(n, cell, x0=X0):
    return x0 + np.arange(n) * cell


def accum(h, outlet, src, carry):
    rec, order, _ = receivers(h, outlet)
    out = np.empty(h.size, np.float32)
    K.accum(order.ctypes.data_as(IP), rec.ctypes.data_as(IP), fp(f32(src.ravel())), fp(f32(carry.ravel())), h.size, fp(out))
    return out.reshape(h.shape)


def glb(path, pos, nrm, idx):
    pos = pos.astype("<f4"); nrm = nrm.astype("<f4"); idx = idx.astype("<u4")
    bin_ = pos.tobytes() + nrm.tobytes() + idx.tobytes(); bin_ += b"\0" * ((4 - len(bin_) % 4) % 4)
    nm = os.path.basename(path)[:-4]
    js = {"asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0, "name": nm}],
          "meshes": [{"name": nm, "primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1}, "indices": 2, "material": 0}]}],
          "materials": [{"name": "M_TerrainSlot"}], "buffers": [{"byteLength": len(bin_)}],
          "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": pos.nbytes, "target": 34962},
                          {"buffer": 0, "byteOffset": pos.nbytes, "byteLength": nrm.nbytes, "target": 34962},
                          {"buffer": 0, "byteOffset": pos.nbytes + nrm.nbytes, "byteLength": idx.nbytes, "target": 34963}],
          "accessors": [{"bufferView": 0, "componentType": 5126, "count": len(pos), "type": "VEC3", "min": pos.min(0).tolist(), "max": pos.max(0).tolist()},
                        {"bufferView": 1, "componentType": 5126, "count": len(pos), "type": "VEC3"},
                        {"bufferView": 2, "componentType": 5125, "count": len(idx), "type": "SCALAR"}]}
    j = json.dumps(js).encode(); j += b" " * ((4 - len(j) % 4) % 4)
    with open(path, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(j) + 8 + len(bin_)))
        f.write(struct.pack("<II", len(j), 0x4E4F534A)); f.write(j)
        f.write(struct.pack("<II", len(bin_), 0x004E4942)); f.write(bin_)
