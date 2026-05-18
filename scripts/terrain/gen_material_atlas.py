#!/usr/bin/env python3
"""gen_material_atlas.py - procedural terrain material atlas generator.

Produces a packed material atlas for the CanvasTerrainViewer terrain shader.
Default layout is a 2x2 grid of 1024x1024 tiles in a single 2048x2048 image,
slot order [grass, rock, sand, snow]. Two outputs per atlas:

    terrain_atlas_albedo.png   - 8-bit sRGB albedo
    terrain_atlas_orm.png      - 8-bit linear ORM: R=AO, G=Roughness, B=Metallic

Per-material content is value-noise + warped octaves tinted to a per-palette
target. Outputs are deterministic for a given --seed so the same atlas
re-generates byte-identically.

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
# Noise primitives (kept independent from gen_heightmap.py so each script
# stands alone for distribution).
# ----------------------------------------------------------------------------

def _smoothstep(t: np.ndarray) -> np.ndarray:
    return t * t * (3.0 - 2.0 * t)


def value_noise(shape: Tuple[int, int], cells: int, rng: np.random.Generator) -> np.ndarray:
    h, w = shape
    grid = rng.random((cells + 1, cells + 1), dtype=np.float64)
    ys = np.linspace(0.0, cells, h, endpoint=False)
    xs = np.linspace(0.0, cells, w, endpoint=False)
    yi = np.floor(ys).astype(np.int32)
    xi = np.floor(xs).astype(np.int32)
    yf = _smoothstep(ys - yi)
    xf = _smoothstep(xs - xi)
    g00 = grid[np.ix_(yi,     xi)]
    g10 = grid[np.ix_(yi,     xi + 1)]
    g01 = grid[np.ix_(yi + 1, xi)]
    g11 = grid[np.ix_(yi + 1, xi + 1)]
    yf2 = yf[:, None]; xf2 = xf[None, :]
    a = g00 * (1 - xf2) + g10 * xf2
    b = g01 * (1 - xf2) + g11 * xf2
    return a * (1 - yf2) + b * yf2


def fbm(shape, base_cells, octaves, lacunarity, gain, rng) -> np.ndarray:
    out = np.zeros(shape, dtype=np.float64)
    amp, cells, norm = 1.0, base_cells, 0.0
    for _ in range(octaves):
        out += amp * value_noise(shape, cells, rng)
        norm += amp
        amp *= gain
        cells = max(int(round(cells * lacunarity)), cells + 1)
    return out / norm


def warped_fbm(shape, base_cells, octaves, lacunarity, gain, warp_amp, rng) -> np.ndarray:
    """fbm sampled at coords perturbed by another fbm. Adds organic-looking
    flow that pure value noise lacks."""
    wx = fbm(shape, base_cells, 3, lacunarity, gain, rng) - 0.5
    wy = fbm(shape, base_cells, 3, lacunarity, gain, rng) - 0.5
    h, w = shape
    base = fbm(shape, base_cells, octaves, lacunarity, gain, rng)
    # Cheap warp: offset sample coords by (wx, wy) and resample base by a small
    # shift via index rolling. Not a true bilinear warp but good enough for an
    # atlas and avoids a per-pixel sampler.
    ox = (wx * warp_amp).astype(np.int32)
    oy = (wy * warp_amp).astype(np.int32)
    yy, xx = np.indices((h, w))
    yy2 = np.clip(yy + oy, 0, h - 1)
    xx2 = np.clip(xx + ox, 0, w - 1)
    return base[yy2, xx2]


# ----------------------------------------------------------------------------
# Per-material palettes and ORM parameters.
#
# Each entry: { 'albedo': [(weight, rgb_srgb), ...], 'orm': (ao, rough, metal) }
# Weights are blended by a (clipped) noise field, so higher weights produce
# larger regions of that color.
# ----------------------------------------------------------------------------

PALETTES = {
    'grass': {
        'albedo_low':  np.array([0.18, 0.32, 0.10]),  # deep shadowed
        'albedo_mid':  np.array([0.34, 0.50, 0.18]),  # bulk
        'albedo_high': np.array([0.55, 0.62, 0.28]),  # sun-bleached tips
        'orm': (0.95, 0.85, 0.00),
        'noise_cells': 16,
        'octaves': 6,
    },
    'rock': {
        'albedo_low':  np.array([0.18, 0.16, 0.14]),
        'albedo_mid':  np.array([0.42, 0.39, 0.35]),
        'albedo_high': np.array([0.62, 0.58, 0.52]),
        'orm': (0.85, 0.72, 0.00),
        'noise_cells': 8,
        'octaves': 7,
    },
    'sand': {
        'albedo_low':  np.array([0.62, 0.50, 0.30]),
        'albedo_mid':  np.array([0.80, 0.68, 0.42]),
        'albedo_high': np.array([0.92, 0.83, 0.58]),
        'orm': (0.92, 0.55, 0.00),
        'noise_cells': 24,
        'octaves': 5,
    },
    'snow': {
        'albedo_low':  np.array([0.78, 0.82, 0.90]),
        'albedo_mid':  np.array([0.92, 0.94, 0.98]),
        'albedo_high': np.array([1.00, 1.00, 1.00]),
        'orm': (0.95, 0.35, 0.00),
        'noise_cells': 12,
        'octaves': 5,
    },
}


def build_material_albedo(size: int, palette: dict, rng: np.random.Generator) -> np.ndarray:
    """Returns (size, size, 3) float64 in [0,1]. Linear-ish; converted to
    sRGB at write time."""
    cells = palette['noise_cells']
    octaves = palette['octaves']
    # Two noise channels: one for low/mid blend, one for mid/high highlights.
    base = warped_fbm((size, size), cells, octaves, 2.0, 0.5, warp_amp=6.0, rng=rng)
    base = (base - base.min()) / max(base.max() - base.min(), 1e-9)
    detail = fbm((size, size), cells * 2, max(octaves - 2, 3), 2.0, 0.55, rng)
    detail = (detail - detail.min()) / max(detail.max() - detail.min(), 1e-9)

    low  = palette['albedo_low']
    mid  = palette['albedo_mid']
    high = palette['albedo_high']

    t_lm = _smoothstep(np.clip((base - 0.20) / 0.50, 0.0, 1.0))
    rgb  = low[None, None, :] * (1.0 - t_lm[..., None]) + mid[None, None, :] * t_lm[..., None]

    t_mh = _smoothstep(np.clip((detail - 0.65) / 0.25, 0.0, 1.0))
    rgb  = rgb * (1.0 - t_mh[..., None]) + high[None, None, :] * t_mh[..., None]

    return np.clip(rgb, 0.0, 1.0)


def build_material_orm(size: int, palette: dict, rng: np.random.Generator) -> np.ndarray:
    """Returns (size, size, 3) float64 in [0,1] - R=AO, G=Roughness, B=Metallic."""
    ao_base, rough_base, metal_base = palette['orm']
    # Modulate AO and roughness by a low-frequency noise so the material isn't
    # flat-shaded uniformly. Metallic stays constant (always 0 for these).
    n = fbm((size, size), max(palette['noise_cells'] // 2, 2), 4, 2.0, 0.55, rng)
    n = (n - n.min()) / max(n.max() - n.min(), 1e-9)
    ao    = np.clip(ao_base    + (n - 0.5) * 0.10, 0.0, 1.0)
    rough = np.clip(rough_base + (n - 0.5) * 0.20, 0.0, 1.0)
    metal = np.full_like(ao, metal_base)
    return np.stack([ao, rough, metal], axis=-1)


def linear_to_srgb(c: np.ndarray) -> np.ndarray:
    """Approximate linear -> sRGB encoding (gamma 2.2)."""
    return np.clip(c, 0.0, 1.0) ** (1.0 / 2.2)


# ----------------------------------------------------------------------------
# Atlas assembly + write
# ----------------------------------------------------------------------------

def assemble_atlas(material_imgs, tile_size: int) -> np.ndarray:
    """material_imgs: list of (H, W, 3) arrays in tile order [TL, TR, BL, BR]."""
    assert len(material_imgs) == 4
    h = w = tile_size
    atlas = np.zeros((2 * h, 2 * w, 3), dtype=material_imgs[0].dtype)
    atlas[0:h,   0:w]   = material_imgs[0]
    atlas[0:h,   w:2*w] = material_imgs[1]
    atlas[h:2*h, 0:w]   = material_imgs[2]
    atlas[h:2*h, w:2*w] = material_imgs[3]
    return atlas


def write_png_rgb8(arr01: np.ndarray, path: Path) -> None:
    u8 = (np.clip(arr01, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)
    path.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(u8, mode='RGB').save(path, format='PNG')


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description='Procedural terrain material atlas generator.')
    p.add_argument('--tile-size', type=int, default=1024,
                   help='Per-material tile size in texels (default 1024)')
    p.add_argument('--seed', type=int, default=1, help='Base random seed')
    p.add_argument('--materials', default='grass,rock,sand,snow',
                   help='Comma-separated 4-material order: TL,TR,BL,BR (default grass,rock,sand,snow)')
    p.add_argument('--out-dir', type=Path, required=True,
                   help='Output directory (atlas PNGs written here)')
    args = p.parse_args(argv)

    materials = [m.strip() for m in args.materials.split(',')]
    if len(materials) != 4:
        print('error: --materials must list exactly 4 names (TL,TR,BL,BR)', file=sys.stderr)
        return 2
    for m in materials:
        if m not in PALETTES:
            print(f"error: unknown material '{m}'. Known: {sorted(PALETTES.keys())}", file=sys.stderr)
            return 2

    rng_master = np.random.default_rng(args.seed)
    albedos = []
    orms = []
    for slot, name in enumerate(materials):
        # Per-material RNG derived from master seed + slot index for determinism.
        sub_seed = int(rng_master.integers(0, 2**31 - 1)) ^ (slot * 0x9E3779B1)
        rng = np.random.default_rng(sub_seed & 0x7FFFFFFF)
        palette = PALETTES[name]
        albedos.append(linear_to_srgb(build_material_albedo(args.tile_size, palette, rng)))
        orms.append(build_material_orm(args.tile_size, palette, rng))

    atlas_albedo = assemble_atlas(albedos, args.tile_size)
    atlas_orm    = assemble_atlas(orms,    args.tile_size)

    out_albedo = args.out_dir / 'terrain_atlas_albedo.png'
    out_orm    = args.out_dir / 'terrain_atlas_orm.png'
    write_png_rgb8(atlas_albedo, out_albedo)
    write_png_rgb8(atlas_orm,    out_orm)
    print(f'wrote {out_albedo} ({atlas_albedo.shape[1]}x{atlas_albedo.shape[0]})')
    print(f'wrote {out_orm}    ({atlas_orm.shape[1]}x{atlas_orm.shape[0]})')
    print(f'  layout: [TL={materials[0]} TR={materials[1]} BL={materials[2]} BR={materials[3]}], '
          f'tile={args.tile_size}, seed={args.seed}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
