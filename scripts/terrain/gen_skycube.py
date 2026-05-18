#!/usr/bin/env python3
"""gen_skycube.py - procedural sky cubemap + celestial sprite generator.

Produces a 6-face cubemap (one PNG per face) for a sky "preset" and, optionally,
soft-edged sun and moon disc sprites used by the sun/moon billboard pass.

Cubemap convention: faces are written as <prefix>_<face>.png where <face> is
one of {posx, negx, posy, negy, posz, negz}, matching D3D / OpenGL cube face
order. The +Z face looks straight up (toward the zenith), -Z faces straight
down (toward the nadir). The four side faces wrap horizontally.

Per-preset content:
    day    - bright blue zenith fading to warm horizon, faint solar haze
    dusk   - oranges/purples banded at horizon, dim upper sky
    night  - deep blue zenith fading to almost-black horizon, Poisson-disk
             star field overlay on all faces

Requires: numpy, Pillow.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Tuple

import numpy as np
from PIL import Image


# ----------------------------------------------------------------------------
# Per-face direction generator.
#
# For a face of size N x N, returns an (N, N, 3) array of unit-length world
# direction vectors. Each face is centered on its axis with the other two
# axes spanning [-1, 1].
# ----------------------------------------------------------------------------

def face_directions(face: str, size: int) -> np.ndarray:
    """Returns (N, N, 3) of unit-vector directions for the named cube face."""
    # Build u, v in [-1, 1] where (u, v) = (0, 0) is the face center.
    u = np.linspace(-1.0, 1.0, size, dtype=np.float64)
    v = np.linspace(-1.0, 1.0, size, dtype=np.float64)
    uu, vv = np.meshgrid(u, v, indexing='xy')

    # D3D cube face conventions (looking outward from the cube center):
    #   posx: +X face, u -> -Z, v -> -Y
    #   negx: -X face, u -> +Z, v -> -Y
    #   posy: +Y face, u -> +X, v -> +Z
    #   negy: -Y face, u -> +X, v -> -Z
    #   posz: +Z face, u -> +X, v -> -Y
    #   negz: -Z face, u -> -X, v -> -Y
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

    dir = np.stack([x, y, z], axis=-1)
    norm = np.linalg.norm(dir, axis=-1, keepdims=True)
    return dir / np.maximum(norm, 1e-9)


# ----------------------------------------------------------------------------
# Sky gradient evaluation.
#
# Each preset is a function from (direction, elevation) to linear RGB. The
# elevation is dir.z = sin(altitude angle). +1 = zenith, 0 = horizon,
# -1 = nadir.
# ----------------------------------------------------------------------------

def smoothstep(a: float, b: float, t: np.ndarray) -> np.ndarray:
    x = np.clip((t - a) / (b - a), 0.0, 1.0)
    return x * x * (3.0 - 2.0 * x)


def sky_day(dirs: np.ndarray) -> np.ndarray:
    """Bright blue zenith fading to warm horizon."""
    z = dirs[..., 2]
    above = np.clip(z, 0.0, 1.0)
    below = np.clip(-z, 0.0, 1.0)

    zenith   = np.array([0.20, 0.40, 0.80])
    horizon  = np.array([0.65, 0.78, 0.90])
    ground   = np.array([0.10, 0.10, 0.12])

    t_up = smoothstep(0.0, 0.6, above)
    upper = horizon[None, None, :] * (1.0 - t_up[..., None]) + zenith[None, None, :] * t_up[..., None]

    t_dn = smoothstep(0.0, 0.4, below)
    lower = horizon[None, None, :] * (1.0 - t_dn[..., None]) + ground[None, None, :] * t_dn[..., None]

    return np.where(z[..., None] >= 0.0, upper, lower)


def sky_dusk(dirs: np.ndarray) -> np.ndarray:
    """Banded oranges/purples at horizon, dim violet upper sky."""
    z = dirs[..., 2]
    above = np.clip(z, 0.0, 1.0)
    below = np.clip(-z, 0.0, 1.0)

    zenith   = np.array([0.10, 0.08, 0.22])
    horizon_warm = np.array([0.95, 0.45, 0.20])
    horizon_cool = np.array([0.40, 0.18, 0.38])
    ground   = np.array([0.04, 0.03, 0.06])

    # Two horizon bands: a warm strip just above the horizon, a cooler one
    # higher up.
    warm = smoothstep(0.0, 0.08, above) * (1.0 - smoothstep(0.0, 0.20, above))
    cool = smoothstep(0.15, 0.45, above)

    upper = (zenith[None, None, :] * (1.0 - cool[..., None])
             + horizon_warm[None, None, :] * warm[..., None] * 0.7
             + horizon_cool[None, None, :] * cool[..., None] * 0.6)

    t_dn = smoothstep(0.0, 0.4, below)
    lower = horizon_cool[None, None, :] * (1.0 - t_dn[..., None]) + ground[None, None, :] * t_dn[..., None]

    return np.where(z[..., None] >= 0.0, upper, lower)


def sky_night(dirs: np.ndarray) -> np.ndarray:
    """Deep blue zenith fading to near-black horizon."""
    z = dirs[..., 2]
    above = np.clip(z, 0.0, 1.0)
    below = np.clip(-z, 0.0, 1.0)

    zenith   = np.array([0.02, 0.04, 0.10])
    horizon  = np.array([0.06, 0.08, 0.16])
    ground   = np.array([0.01, 0.01, 0.02])

    t_up = smoothstep(0.0, 0.6, above)
    upper = horizon[None, None, :] * (1.0 - t_up[..., None]) + zenith[None, None, :] * t_up[..., None]

    t_dn = smoothstep(0.0, 0.4, below)
    lower = horizon[None, None, :] * (1.0 - t_dn[..., None]) + ground[None, None, :] * t_dn[..., None]

    return np.where(z[..., None] >= 0.0, upper, lower)


# ----------------------------------------------------------------------------
# Star overlay (night preset only).
#
# Distributes ~density stars per square steradian on the unit sphere using
# rejection sampling, then for each star finds all face pixels within an
# angular radius and adds a brightness contribution.
# ----------------------------------------------------------------------------

def add_stars(rgb: np.ndarray, face_dirs: np.ndarray, star_dirs: np.ndarray,
              star_intensity: np.ndarray, angular_radius_rad: float) -> np.ndarray:
    """Add Gaussian-falloff star spots within angular_radius of each star
    direction that lands inside this face."""
    H, W, _ = rgb.shape
    flat_dirs = face_dirs.reshape(-1, 3)  # (H*W, 3)
    flat_rgb  = rgb.reshape(-1, 3).copy()

    # For each star, dot product with every face pixel direction.
    # Stars are not visible if dot < cos(angular_radius).
    cos_thresh = np.cos(angular_radius_rad)
    inv_sigma2 = 1.0 / (0.4 * angular_radius_rad) ** 2

    for i in range(star_dirs.shape[0]):
        sd = star_dirs[i]
        dots = flat_dirs @ sd  # (H*W,)
        mask = dots > cos_thresh
        if not np.any(mask):
            continue
        # Angular distance approximation: 1 - cos = 1/2 theta^2 for small theta.
        theta2 = 2.0 * (1.0 - dots[mask])
        weight = star_intensity[i] * np.exp(-theta2 * inv_sigma2)
        flat_rgb[mask] += weight[:, None]

    return flat_rgb.reshape(H, W, 3)


def sample_star_field(num_stars: int, rng: np.random.Generator) -> Tuple[np.ndarray, np.ndarray]:
    """Returns (directions, intensities) sampled isotropically on the unit
    sphere. Intensities follow a heavy-tailed distribution so a few stars are
    much brighter than the rest."""
    # Cosine-weighted sphere via inverse-CDF.
    u = rng.random(num_stars)
    v = rng.random(num_stars)
    z = 2.0 * u - 1.0
    phi = 2.0 * np.pi * v
    r = np.sqrt(1.0 - z * z)
    dirs = np.stack([r * np.cos(phi), r * np.sin(phi), z], axis=-1)
    # Brightness: log-uniform between 0.05 and 0.9, with a power weighting
    # to favor the dim end.
    base = rng.random(num_stars) ** 3.0
    intensities = 0.05 + base * 0.85
    return dirs, intensities


# ----------------------------------------------------------------------------
# Driver
# ----------------------------------------------------------------------------

FACES = ('posx', 'negx', 'posy', 'negy', 'posz', 'negz')

PRESETS = {
    'day':   sky_day,
    'dusk':  sky_dusk,
    'night': sky_night,
}


def linear_to_srgb(c: np.ndarray) -> np.ndarray:
    return np.clip(c, 0.0, 1.0) ** (1.0 / 2.2)


def write_png_rgb8(arr01: np.ndarray, path: Path) -> None:
    u8 = (np.clip(arr01, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)
    path.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(u8, mode='RGB').save(path, format='PNG')


def gen_celestial_sprite(size: int, body: str, out: Path) -> None:
    """Soft-edged disc sprite for sun or moon. Alpha falls off with radius."""
    y = np.linspace(-1.0, 1.0, size)
    x = np.linspace(-1.0, 1.0, size)
    xx, yy = np.meshgrid(x, y, indexing='xy')
    r = np.sqrt(xx * xx + yy * yy)

    if body == 'sun':
        core = np.array([1.00, 0.96, 0.85])
        # Bright core then soft falloff.
        disc  = (r < 0.45).astype(np.float32)
        glow  = np.exp(-((r - 0.45) * 4.0) ** 2) * (r >= 0.45)
        alpha = np.clip(disc + 0.6 * glow, 0.0, 1.0)
    elif body == 'moon':
        core = np.array([0.85, 0.88, 0.95])
        disc  = (r < 0.50).astype(np.float32)
        glow  = np.exp(-((r - 0.50) * 6.0) ** 2) * (r >= 0.50)
        alpha = np.clip(disc + 0.3 * glow, 0.0, 1.0)
    else:
        raise ValueError(f"unknown celestial body '{body}'")

    rgba = np.zeros((size, size, 4), dtype=np.float32)
    rgba[..., 0:3] = core[None, None, :]
    rgba[..., 3]   = alpha
    u8 = (np.clip(rgba, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)
    out.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(u8, mode='RGBA').save(out, format='PNG')


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description='Procedural sky cubemap + celestial sprite generator.')
    p.add_argument('--preset', choices=sorted(PRESETS.keys()), required=True,
                   help='Which sky preset to generate')
    p.add_argument('--face-size', type=int, default=512, help='Per-face size in texels (default 512)')
    p.add_argument('--seed', type=int, default=1, help='Random seed (used for night star field)')
    p.add_argument('--stars', type=int, default=1500,
                   help='Star count for night preset (default 1500)')
    p.add_argument('--out-dir', type=Path, required=True,
                   help='Output directory (cube faces written as sky_<preset>_<face>.png)')
    p.add_argument('--sprites', action='store_true',
                   help='Also write sun.png and moon.png to --out-dir')
    args = p.parse_args(argv)

    if args.face_size < 8:
        print('error: --face-size must be >= 8', file=sys.stderr)
        return 2

    rng = np.random.default_rng(args.seed)
    sky_fn = PRESETS[args.preset]

    # Pre-sample the star field once so all 6 faces see the same starscape.
    star_dirs = star_intensities = None
    star_radius_rad = 0.0
    if args.preset == 'night' and args.stars > 0:
        star_dirs, star_intensities = sample_star_field(args.stars, rng)
        # Angular radius of a single star, in radians. Smaller for higher
        # resolution faces. Roughly 2 texels worth.
        star_radius_rad = 2.0 * (np.pi / 2.0) / args.face_size

    for face in FACES:
        dirs = face_directions(face, args.face_size)
        rgb  = sky_fn(dirs)
        if star_dirs is not None:
            rgb = add_stars(rgb, dirs, star_dirs, star_intensities, star_radius_rad)
        rgb_srgb = linear_to_srgb(rgb)
        out_path = args.out_dir / f'sky_{args.preset}_{face}.png'
        write_png_rgb8(rgb_srgb, out_path)
        print(f'wrote {out_path}')

    if args.sprites:
        gen_celestial_sprite(256, 'sun',  args.out_dir / 'sun.png')
        gen_celestial_sprite(256, 'moon', args.out_dir / 'moon.png')
        print(f'wrote {args.out_dir / "sun.png"}')
        print(f'wrote {args.out_dir / "moon.png"}')

    print(f'  preset={args.preset}, face_size={args.face_size}, seed={args.seed}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
