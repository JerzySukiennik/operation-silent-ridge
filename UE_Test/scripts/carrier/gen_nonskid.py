"""Tileable nonskid deck normal map (512^2): fine grit + trowel ripples. Own work. Usage: python3 gen_nonskid.py <out.png>"""
import sys, numpy as np
from PIL import Image
N = 512
rng = np.random.default_rng(5)
def tile_noise(freq):
    g = rng.normal(0, 1, (freq, freq))
    F = np.fft.fft2(np.pad(g, ((0, N - freq), (0, N - freq))))
    return np.real(np.fft.ifft2(F))
h = np.zeros((N, N))
k = np.fft.fftfreq(N)[:, None] ** 2 + np.fft.fftfreq(N)[None, :] ** 2
for f0, a in [(0.25, 1.0), (0.08, 0.6), (0.02, 0.25)]:
    spec = np.fft.fft2(rng.normal(0, 1, (N, N))) * np.exp(-k / (2 * f0 ** 2))
    h += np.real(np.fft.ifft2(spec)) * a / np.std(np.real(np.fft.ifft2(spec)))
gx = (np.roll(h, -1, 1) - np.roll(h, 1, 1)) * 0.5
gy = (np.roll(h, -1, 0) - np.roll(h, 1, 0)) * 0.5
s = 0.9
n = np.stack([-gx * s, -gy * s, np.ones_like(h)], -1)
n /= np.linalg.norm(n, axis=-1, keepdims=True)
Image.fromarray(((n * 0.5 + 0.5) * 255).astype(np.uint8)).save(sys.argv[1])
print("wrote", sys.argv[1])
