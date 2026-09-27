#!/usr/bin/env python3
# Downloads the CC0 Poly Haven terrain/tree textures and packs them for the terrain shader (albedo RGB + height A, normal RGB).
"""
    python3 game/tools/terrain/fetch_textures.py
Sources go to game/assets/world/src/polyhaven/ (not exported); packed PNGs to game/assets/world/textures/.
"""
import json
import os
import urllib.request

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
GAME = os.path.normpath(os.path.join(HERE, "..", ".."))
SRC = os.path.join(GAME, "assets", "world", "src", "polyhaven")
OUT = os.path.join(GAME, "assets", "world", "textures")

# name in game -> (poly haven id, resolution, output size)
TERRAIN = {
    "rock_cliff": ("marble_cliff_05", "2k", 2048),
    "rock_macro": ("aerial_rocks_02", "2k", 2048),
    "scree": ("gray_rocks", "2k", 1024),
    "scree_macro": ("aerial_rocks_01", "2k", 2048),
    "snow": ("snow_02", "2k", 1024),
    "snow_macro": ("snow_field_aerial", "2k", 2048),
    "ground": ("aerial_grass_rock", "2k", 2048),
    "forest_floor": ("forest_ground_04", "2k", 1024),
    "shore": ("coast_sand_rocks_02", "2k", 1024),
}
TREE = ["twig_diff", "twig_alpha", "twig_nor_gl", "bark_diff", "bark_nor_gl"]


def fetch(url, path):
    if os.path.exists(path):
        return
    os.makedirs(os.path.dirname(path), exist_ok=True)
    print("get", url)
    req = urllib.request.Request(url, headers={"User-Agent": "OperationSilentRidge-terrain-tool"})
    with urllib.request.urlopen(req) as r, open(path + ".part", "wb") as f:
        f.write(r.read())
    os.replace(path + ".part", path)


def files(asset):
    req = urllib.request.Request(f"https://api.polyhaven.com/files/{asset}", headers={"User-Agent": "osr"})
    return json.loads(urllib.request.urlopen(req).read())


def main():
    os.makedirs(OUT, exist_ok=True)
    credits = []
    for name, (asset, res, size) in TERRAIN.items():
        f = files(asset)
        maps = {}
        for key in ("diff", "nor_gl", "disp"):
            k = {"diff": "Diffuse", "nor_gl": "nor_gl", "disp": "Displacement"}[key]
            url = f[k][res]["jpg" if "jpg" in f[k][res] else "png"]["url"]
            p = os.path.join(SRC, asset, os.path.basename(url))
            fetch(url, p)
            maps[key] = p
        alb = Image.open(maps["diff"]).convert("RGB").resize((size, size), Image.LANCZOS)
        disp = Image.open(maps["disp"]).convert("L").resize((size, size), Image.LANCZOS)
        d = np.asarray(disp, np.float32)
        d = (d - d.min()) / max(1.0, d.max() - d.min()) * 255.0
        rgba = np.dstack([np.asarray(alb), d.astype(np.uint8)])
        Image.fromarray(rgba, "RGBA").save(os.path.join(OUT, f"{name}_albedo_h.png"))
        nor = Image.open(maps["nor_gl"]).convert("RGB").resize((size, size), Image.LANCZOS)
        nor.save(os.path.join(OUT, f"{name}_normal.png"))
        credits.append(f"assets/world/textures/{name}_*.png -> Poly Haven \"{asset}\", https://polyhaven.com/a/{asset}, CC0")
    f = files("fir_tree_01")
    for key in TREE:
        url = f[key]["2k"]["png" if key == "twig_alpha" else "jpg"]["url"]
        fetch(url, os.path.join(SRC, "fir_tree_01", os.path.basename(url)))
    credits.append("assets/world/trees/* (twig/bark textures) -> Poly Haven \"fir_tree_01\", https://polyhaven.com/a/fir_tree_01, CC0")
    make_noise(os.path.join(OUT, "noise.png"))
    print("\n".join(credits))


def make_noise(path, n=512, seed=7):
    """tileable RGBA value-noise texture (four independent fBm channels) used for macro variation."""
    rng = np.random.default_rng(seed)
    chans = []
    for c in range(4):
        acc = np.zeros((n, n), np.float32)
        amp, tot = 1.0, 0.0
        for o in range(6):
            k = 4 * 2 ** o
            g = rng.random((k, k)).astype(np.float32)
            # periodic cubic upsample via FFT-free bicubic on a tiled grid
            big = np.tile(g, (3, 3))
            im = Image.fromarray(big).resize((n * 3, n * 3), Image.BICUBIC)
            a = np.asarray(im, np.float32)[n:2 * n, n:2 * n]
            acc += a * amp
            tot += amp
            amp *= 0.55
        acc /= tot
        acc = (acc - acc.min()) / (acc.max() - acc.min())
        chans.append((acc * 255).astype(np.uint8))
    Image.fromarray(np.dstack(chans), "RGBA").save(path)


if __name__ == "__main__":
    main()
