"""Synthetic inputs for fg_bench (no game data): quarter-size luma pairs with a pan, a moving patch and flat sky, a vector
field, and an RGBA8 frame. Writes into the folder given (default: ./data)."""
import struct
import sys
from pathlib import Path

import numpy as np

out = Path(sys.argv[1] if len(sys.argv) > 1 else "data")
out.mkdir(parents=True, exist_ok=True)
rng = np.random.default_rng(1)
for name, (w, h) in {"1080p": (480, 270), "1440p": (640, 360), "2160p": (960, 540)}.items():
    y, x = np.mgrid[0:h, 0:w].astype(np.float32)
    a = 0.5 + 0.2 * np.sin(x / 13) * np.cos(y / 17) + 0.1 * np.sin((x + 2 * y) / 5)
    a += rng.normal(0, 0.05, (h, w)).astype(np.float32)
    b = np.roll(a, (3, -5), (0, 1)).copy()
    b[100:160, 200:300] = np.roll(a, (-6, 7), (0, 1))[100:160, 200:300]
    a[:40], b[:40] = 0.8, 0.8
    np.clip(a, 0, 1).astype(np.float16).tofile(out / f"la_{name}.bin")
    np.clip(b, 0, 1).astype(np.float16).tofile(out / f"lb_{name}.bin")
f = np.zeros((90, 160, 4), np.float32)
f[..., :2] = rng.integers(-16, 17, (90, 160, 2)) * 4
f[..., 2], f[..., 3] = rng.random((90, 160)), 1
f.astype(np.float16).tofile(out / "flow.bin")
h, w = 720, 1280
y, x = np.mgrid[0:h, 0:w]
img = np.stack([128 + 100 * np.sin(x / 7 + c) * np.cos(y / 9) + rng.normal(0, 20, (h, w)) for c in range(3)]
               + [np.full((h, w), 255.0)], -1)
np.clip(img, 0, 255).astype(np.uint8).tofile(out / "frame720.bin")
for mode in (1, 2):
    (out / f"pc{mode}.hex").write_text(struct.pack("<if", mode, 0.5).hex())
