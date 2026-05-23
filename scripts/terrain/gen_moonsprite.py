#!/usr/bin/env python3
"""gen_moonsprite.py - soft-edged moon billboard texture.

Writes a single RGBA PNG with a circular moon disc + soft outer glow.
The engine samples this texture in the composite shader inside the
angular disc defined by MoonDirection / MoonAngularRadius (see
GfxBackgroundDesc in CanvasGfx.h), so its texture-space UV (top-left
origin) is what shows up on screen with the visual top of the moon at
the disc's apex.

Requires: numpy, Pillow.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np
from PIL import Image


def make_moon_disc(size: int) -> np.ndarray:
    """Soft-edged moon disc.  Neutral white core, tight Gaussian glow at
    the rim.  The texture is colourless so the engine-side MoonColor
    tint is the single source of moon appearance colour."""
    y = np.linspace(-1.0, 1.0, size)
    x = np.linspace(-1.0, 1.0, size)
    xx, yy = np.meshgrid(x, y, indexing='xy')
    r = np.sqrt(xx * xx + yy * yy)

    core_color = np.array([1.0, 1.0, 1.0])
    disc       = (r < 0.50).astype(np.float32)
    glow       = np.exp(-((r - 0.50) * 6.0) ** 2) * (r >= 0.50)
    alpha      = np.clip(disc + 0.3 * glow, 0.0, 1.0)

    rgba = np.zeros((size, size, 4), dtype=np.float32)
    rgba[..., 0:3] = core_color[None, None, :]
    rgba[..., 3]   = alpha
    return rgba


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description='Procedural moon billboard sprite.')
    p.add_argument('--size', type=int, default=256, help='Output texel size (default 256)')
    p.add_argument('--out',  type=Path, required=True, help='Output PNG path')
    args = p.parse_args(argv)

    if args.size < 8:
        print('error: --size must be >= 8', file=sys.stderr)
        return 2

    rgba = make_moon_disc(args.size)
    u8 = (np.clip(rgba, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(u8, mode='RGBA').save(args.out, format='PNG')
    print(f'wrote {args.out}')
    print(f'  size={args.size}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
