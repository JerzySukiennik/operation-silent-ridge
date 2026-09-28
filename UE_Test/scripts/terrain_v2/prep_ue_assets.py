# Prepares the non-terrain v2 inputs for UE: the camera-following ocean grid (GLB, sinh-spaced: ~3 m quads at the centre, ~3 km at
# the 100 km rim) and packed CC0 textures (albedo RGB + height A, normals) for the new terrain layers.
import os, shutil
import numpy as np
from PIL import Image
import tv2
from tv2 import glb

Image.MAX_IMAGE_PIXELS = None
TEX = os.path.join(tv2.WORK, "tex"); os.makedirs(TEX, exist_ok=True)
AS = os.path.join(tv2.ROOT, "UE_Test", "Niepotrzebne", "assets")

# ------------------------------------------------------------------ ocean grid
N = 512; R = 100000.0; a = 7.6
u = np.linspace(-1, 1, N + 1)
x = R * np.sinh(a * u) / np.sinh(a)
X, Z = np.meshgrid(x, x)
pos = np.stack([X, np.zeros_like(X), Z], -1).reshape(-1, 3)
nrm = np.tile([0.0, 1.0, 0.0], (len(pos), 1))
a0 = (np.arange(N)[None, :] + np.arange(N)[:, None] * (N + 1)).ravel()
idx = np.stack([a0, a0 + N + 1, a0 + 1, a0 + 1, a0 + N + 1, a0 + N + 2], 1).ravel()
glb(os.path.join(tv2.WORK, "SM_OceanGrid.glb"), pos, nrm, idx)
print("ocean grid: centre quad %.2f m, rim quad %.0f m" % (x[N // 2 + 1] - x[N // 2], x[-1] - x[-2]))


# ------------------------------------------------------------------ textures
def pack(src_dir, aid, out):
    d = Image.open(os.path.join(AS, src_dir, "%s_diff_2k.png" % aid)).convert("RGB")
    h = Image.open(os.path.join(AS, src_dir, "%s_disp_2k.png" % aid)).convert("L").resize(d.size)
    Image.merge("RGBA", (*d.split(), h)).save(os.path.join(TEX, out + "_A.png"))
    shutil.copy(os.path.join(AS, src_dir, "%s_nor_gl_2k.png" % aid), os.path.join(TEX, out + "_N.png"))
    print("packed", out)


pack("rock_face_03", "rock_face_03", "T_rockface")
pack("rocks_ground_06", "rocks_ground_06", "T_grav")
pack("coast_sand_05", "coast_sand_05", "T_sand")
