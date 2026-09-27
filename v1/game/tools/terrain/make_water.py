#!/usr/bin/env python3
# Builds tileable ocean detail normal maps from a Phillips-spectrum FFT heightfield (seeded, CC0 by construction).
import os

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, "..", "..", "assets", "world", "textures"))


def phillips_height(n, size_m, wind, wind_dir, seed, t=0.0):
    rng = np.random.default_rng(seed)
    k1 = np.fft.fftfreq(n, d=size_m / n) * 2 * np.pi
    kx, kz = np.meshgrid(k1, k1)
    k = np.hypot(kx, kz)
    k[0, 0] = 1e-6
    g = 9.81
    L = wind * wind / g
    wd = np.array(wind_dir) / np.linalg.norm(wind_dir)
    kd = (kx * wd[0] + kz * wd[1]) / k
    ph = np.exp(-1.0 / (k * L) ** 2) / k ** 4 * kd ** 2
    ph *= np.exp(-(k * 0.05) ** 2)  # damp capillary scale
    ph[kd < 0] *= 0.15               # waves mostly travel downwind
    ph[0, 0] = 0
    h0 = (rng.normal(size=(n, n)) + 1j * rng.normal(size=(n, n))) * np.sqrt(ph / 2)
    w = np.sqrt(g * k)
    hk = h0 * np.exp(1j * w * t)
    h = np.real(np.fft.ifft2(hk)) * n * n
    return h


def normal_map(h, size_m, strength):
    n = h.shape[0]
    d = size_m / n
    gx = (np.roll(h, -1, 1) - np.roll(h, 1, 1)) / (2 * d)
    gz = (np.roll(h, -1, 0) - np.roll(h, 1, 0)) / (2 * d)
    nx, ny, nz = -gx * strength, -gz * strength, np.ones_like(h)
    ln = np.sqrt(nx * nx + ny * ny + nz * nz)
    # tangent-space: x = +u (world x), y = +v up in the image (OpenGL convention: v points to -z in world)
    rgb = np.stack([nx / ln, -ny / ln, nz / ln], -1) * 0.5 + 0.5
    return (np.clip(rgb, 0, 1) * 255).astype(np.uint8)


def main():
    os.makedirs(OUT, exist_ok=True)
    for name, size_m, wind, seed, st in (("water_normal_a", 64.0, 9.0, 3, 1.0), ("water_normal_b", 12.0, 5.0, 4, 1.0)):
        h = phillips_height(512, size_m, wind, (1.0, 0.35), seed)
        h = h / np.std(h)
        # scale to a plausible rms slope
        g = np.hypot(*np.gradient(h, size_m / 512))
        h *= 0.16 / np.sqrt(np.mean(g * g))
        Image.fromarray(normal_map(h, size_m, st), "RGB").save(os.path.join(OUT, name + ".png"))
    print("water normals written")


if __name__ == "__main__":
    main()
