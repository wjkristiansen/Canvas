#!/usr/bin/env python3
"""gen_starscube.py - rotating stars cubemap generator.

Produces a 6-face RGBA cubemap (one PNG per face) carrying a procedural
star field at fixed positions on a celestial sphere.  The engine rotates
this cube once per frame via XScene::SetBackground's StarsOrientation
to drive sidereal motion -- stars sweep across the sky as a rigid
sphere.  Alpha = per-star coverage; the engine additively blends
rgb * alpha * StarsIntensity over the atmospheric skybox and clips the
lower hemisphere so stars never appear through the ground.

Cubemap convention: faces are written as sky_stars_<face>.png where
<face> is one of {posx, negx, posy, negy, posz, negz}, matching the
D3D / OpenGL cube face order used by gen_skycube.py.

Sun and moon do NOT live on this cube -- the engine renders the sun as
a procedural disc and the moon as a textured billboard, both driven by
world-space direction vectors (the same vectors the app uses for the
directional lights, so there's a single source of truth per body).

Requires: numpy, Pillow.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Tuple

import numpy as np
from PIL import Image


FACES = ('posx', 'negx', 'posy', 'negy', 'posz', 'negz')


# ----------------------------------------------------------------------------
# Per-face direction generator.  Identical convention to gen_skycube.py.
# ----------------------------------------------------------------------------
def face_directions(face: str, size: int) -> np.ndarray:
    u = np.linspace(-1.0, 1.0, size, dtype=np.float64)
    v = np.linspace(-1.0, 1.0, size, dtype=np.float64)
    uu, vv = np.meshgrid(u, v, indexing='xy')
    if face == 'posx':
        x = np.ones_like(uu); y = -vv;          z = -uu
    elif face == 'negx':
        x = -np.ones_like(uu); y = -vv;         z = uu
    elif face == 'posy':
        x = uu;                y = np.ones_like(uu); z = vv
    elif face == 'negy':
        x = uu;                y = -np.ones_like(uu); z = -vv
    elif face == 'posz':
        x = uu;                y = -vv;         z = np.ones_like(uu)
    elif face == 'negz':
        x = -uu;               y = -vv;         z = -np.ones_like(uu)
    else:
        raise ValueError(f"unknown face '{face}'")
    d = np.stack([x, y, z], axis=-1)
    n = np.linalg.norm(d, axis=-1, keepdims=True)
    return d / np.maximum(n, 1e-9)


# ----------------------------------------------------------------------------
# Star field sampling.  Points distributed isotropically on the unit sphere
# with a heavy-tailed brightness so a few stars dominate.  Subtle per-star
# colour tint biased toward neutral white.
# ----------------------------------------------------------------------------
def sample_star_field(num: int, rng: np.random.Generator) -> Tuple[np.ndarray, np.ndarray]:
    u = rng.random(num); v = rng.random(num)
    z = 2.0 * u - 1.0
    phi = 2.0 * np.pi * v
    r = np.sqrt(1.0 - z * z)
    dirs = np.stack([r * np.cos(phi), r * np.sin(phi), z], axis=-1)
    base = rng.random(num) ** 3.0
    intensities = 0.30 + base * 0.70
    return dirs, intensities


def star_colors(num: int, rng: np.random.Generator) -> np.ndarray:
    hue_bias = rng.standard_normal((num, 3)) * 0.08
    base     = np.array([1.0, 1.0, 1.0])[None, :]
    return np.clip(base + hue_bias, 0.4, 1.2)


def add_stars(rgba: np.ndarray, dirs: np.ndarray,
              star_dirs: np.ndarray, star_intensities: np.ndarray,
              star_cols: np.ndarray, angular_radius_rad: float) -> None:
    flat_dirs = dirs.reshape(-1, 3)
    flat_rgba = rgba.reshape(-1, 4)
    cos_thresh = np.cos(angular_radius_rad)
    inv_sigma2 = 1.0 / (0.4 * angular_radius_rad) ** 2

    for i in range(star_dirs.shape[0]):
        sd = star_dirs[i]
        dots = flat_dirs @ sd
        mask = dots > cos_thresh
        if not np.any(mask):
            continue
        theta2 = 2.0 * (1.0 - dots[mask])
        weight = star_intensities[i] * np.exp(-theta2 * inv_sigma2)
        contrib_rgb = star_cols[i][None, :] * weight[:, None]
        idx = np.flatnonzero(mask)
        flat_rgba[idx, 0:3] = np.minimum(1.0, flat_rgba[idx, 0:3] + contrib_rgb)
        flat_rgba[idx, 3]   = np.minimum(1.0, flat_rgba[idx, 3] + weight)


# ----------------------------------------------------------------------------
# Driver
# ----------------------------------------------------------------------------
def linear_to_srgb(c: np.ndarray) -> np.ndarray:
    return np.clip(c, 0.0, 1.0) ** (1.0 / 2.2)


def write_png_rgba8(arr01: np.ndarray, path: Path) -> None:
    u8 = (np.clip(arr01, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)
    path.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(u8, mode='RGBA').save(path, format='PNG')


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description='Procedural stars cubemap generator.')
    p.add_argument('--face-size', type=int, default=512, help='Per-face size in texels (default 512)')
    p.add_argument('--stars', type=int, default=1200, help='Star count (default 1200, 0 disables)')
    p.add_argument('--seed',  type=int, default=42,   help='Star RNG seed')
    p.add_argument('--out-dir', type=Path, required=True,
                   help='Output directory (faces written as sky_stars_<face>.png)')
    args = p.parse_args(argv)

    if args.face_size < 8:
        print('error: --face-size must be >= 8', file=sys.stderr)
        return 2

    rng = np.random.default_rng(args.seed)
    star_dirs = star_ints = star_cols = None
    star_rad_rad = 0.0
    if args.stars > 0:
        star_dirs, star_ints = sample_star_field(args.stars, rng)
        star_cols = star_colors(args.stars, rng)
        star_rad_rad = 2.0 * (np.pi / 2.0) / args.face_size

    for face in FACES:
        dirs = face_directions(face, args.face_size)
        rgba = np.zeros((args.face_size, args.face_size, 4), dtype=np.float32)
        if star_dirs is not None:
            add_stars(rgba, dirs, star_dirs, star_ints, star_cols, star_rad_rad)
        rgba[..., 0:3] = linear_to_srgb(rgba[..., 0:3])
        out_path = args.out_dir / f'sky_stars_{face}.png'
        write_png_rgba8(rgba, out_path)
        print(f'wrote {out_path}')

    print(f'  face_size={args.face_size}, stars={args.stars}, seed={args.seed}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
