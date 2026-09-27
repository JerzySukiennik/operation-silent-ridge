#!/usr/bin/env python3
# Packs the Blender impostor renders into 2048x1024 atlases (4 side views 384x1024 + top view 512x512) and the branch card textures.
import os

import numpy as np
from PIL import Image
from scipy import ndimage

HERE = os.path.dirname(os.path.abspath(__file__))
GAME = os.path.normpath(os.path.join(HERE, "..", ".."))
SRC = os.path.join(GAME, "assets", "world", "src", "tree")
OUT = os.path.join(GAME, "assets", "world", "trees")


def dilate(rgba):
    a = np.asarray(rgba).astype(np.float32)
    mask = a[..., 3] > 8
    idx = ndimage.distance_transform_edt(~mask, return_distances=False, return_indices=True)
    rgb = a[..., :3][idx[0], idx[1]]
    return Image.fromarray(np.dstack([rgb, a[..., 3]]).astype(np.uint8), "RGBA")


def atlas(mode):
    out = Image.new("RGBA", (2048, 1024), (0, 0, 0, 0))
    for k in range(4):
        out.paste(Image.open(os.path.join(SRC, f"imp_{mode}_side{k}.png")).convert("RGBA"), (k * 384, 0))
    out.paste(Image.open(os.path.join(SRC, f"imp_{mode}_top.png")).convert("RGBA"), (1536, 0))
    return dilate(out)


def main():
    os.makedirs(OUT, exist_ok=True)
    atlas("albedo").save(os.path.join(OUT, "fir_billboard_albedo.png"))
    n = atlas("normal")
    # alpha is not needed in the normal atlas
    n.convert("RGB").save(os.path.join(OUT, "fir_billboard_normal.png"))
    Image.open(os.path.join(SRC, "branch_albedo.png")).save(os.path.join(OUT, "fir_twig_albedo.png"))
    Image.open(os.path.join(SRC, "branch_normal.png")).convert("RGB").save(os.path.join(OUT, "fir_twig_normal.png"))
    bark = Image.open(os.path.join(GAME, "assets", "world", "src", "polyhaven", "fir_tree_01", "fir_tree_01_bark_diff_2k.jpg"))
    bark.resize((512, 512), Image.LANCZOS).save(os.path.join(OUT, "fir_bark_albedo.png"))
    print("tree atlases written")


if __name__ == "__main__":
    main()
