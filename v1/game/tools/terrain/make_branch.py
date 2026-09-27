#!/usr/bin/env python3
# Composes a conifer branch-spray card texture (albedo+alpha, normal) from the Poly Haven fir_tree_01 twig atlas (CC0), seeded.
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFilter
from scipy import ndimage

HERE = os.path.dirname(os.path.abspath(__file__))
GAME = os.path.normpath(os.path.join(HERE, "..", ".."))
SRC = os.path.join(GAME, "assets", "world", "src", "polyhaven", "fir_tree_01")
OUT = os.path.join(GAME, "assets", "world", "src", "tree")


def sprites():
    diff = Image.open(os.path.join(SRC, "fir_tree_01_twig_diff_2k.jpg")).convert("RGB")
    nor = Image.open(os.path.join(SRC, "fir_tree_01_twig_nor_gl_2k.jpg")).convert("RGB")
    alpha = np.asarray(Image.open(os.path.join(SRC, "fir_tree_01_twig_alpha_2k.png")).convert("L"))
    a = alpha.copy()
    a[1500:, :] = 0      # bottom strip holds bark/stem pieces
    a[:, :350] = 0
    lab, n = ndimage.label(ndimage.binary_dilation(a > 128, iterations=6))
    out = []
    for sl in ndimage.find_objects(lab):
        h, w = sl[0].stop - sl[0].start, sl[1].stop - sl[1].start
        if h < 150 or w < 80:
            continue
        box = (sl[1].start, sl[0].start, sl[1].stop, sl[0].stop)
        rgba = diff.crop(box).copy()
        rgba.putalpha(Image.fromarray(alpha[sl]))
        out.append((rgba, nor.crop(box)))
    return out


def main():
    os.makedirs(OUT, exist_ok=True)
    rng = np.random.default_rng(21)
    sp = sprites()
    print("twig sprites:", len(sp))
    W, H = 1024, 512
    alb = Image.new("RGBA", (W, H), (40, 52, 30, 0))
    nrm = Image.new("RGBA", (W, H), (128, 128, 255, 0))
    # branch spine from the trunk (left) to the tip (right), slightly curved
    spine = [(int(20 + t * (W - 60)), int(H / 2 + 18 * np.sin(t * 2.5))) for t in np.linspace(0, 1, 40)]
    d = ImageDraw.Draw(alb)
    d.line(spine, fill=(70, 52, 36, 255), width=7)
    items = []
    for k in range(95):
        t = rng.uniform(0.05, 1.0) ** 0.8
        px = 20 + t * (W - 60)
        py = H / 2 + 18 * np.sin(t * 2.5)
        side = 1 if k % 2 else -1
        ang = side * rng.uniform(30, 70)          # twig direction relative to the spine (x axis)
        scale = 0.82 * (0.22 + 0.34 * np.sin(np.pi * min(t * 1.3, 1.0)) ** 0.7 + rng.uniform(-0.04, 0.06)) * (H / 900)
        items.append((t, px, py, ang, scale, rng.integers(len(sp))))
    items.append((1.0, W - 70, H / 2, 0.0, 0.5 * H / 900, 0))
    items.sort(key=lambda it: -it[0])
    for t, px, py, ang, scale, si in items:
        img, nor = sp[si]
        w, h = img.size
        img = img.resize((max(1, int(w * scale * 2.2)), max(1, int(h * scale * 2.2))), Image.LANCZOS)
        nor = nor.resize(img.size, Image.LANCZOS)
        # sprites point up (stem at the bottom): rotate so they point along +x, then by ang
        rot = -90 + ang
        ri = img.rotate(rot, resample=Image.BICUBIC, expand=True)
        rn = nor.convert("RGBA").rotate(rot, resample=Image.BICUBIC, expand=True)
        rn.putalpha(ri.getchannel("A"))
        # the stem (bottom centre of the upright sprite) sits on the spine
        th = np.radians(rot)
        hh = img.size[1] / 2
        pos = (int(px - ri.size[0] / 2 - hh * np.sin(th)), int(py - ri.size[1] / 2 - hh * np.cos(th)))
        shade = 0.75 + 0.35 * t + rng.uniform(-0.08, 0.08)
        arr = np.asarray(ri, np.float32)
        arr[..., :3] *= shade
        ri = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
        alb.alpha_composite(ri, pos)
        nrm.alpha_composite(rn, pos)
    a = np.asarray(alb, np.float32)
    # dilate colour into transparent texels so mip levels do not bleed a halo
    rgb = a[..., :3]
    mask = a[..., 3] > 10
    idx = ndimage.distance_transform_edt(~mask, return_distances=False, return_indices=True)
    rgb = rgb[idx[0], idx[1]]
    out = np.dstack([rgb, a[..., 3]]).astype(np.uint8)
    Image.fromarray(out, "RGBA").save(os.path.join(OUT, "branch_albedo.png"))
    n = np.asarray(nrm, np.float32)
    nrgb = n[..., :3][idx[0], idx[1]]
    Image.fromarray(nrgb.astype(np.uint8), "RGB").save(os.path.join(OUT, "branch_normal.png"))
    print("branch card written")


if __name__ == "__main__":
    main()
