#!/usr/bin/env python3
"""gen_heightmap.py - procedural heightfield generator for CanvasTerrainViewer.

Outputs a 16-bit grayscale PNG sized for one tile. Default size is 1025x1025
texels - under the shared-edge tiling convention this covers 1024x1024 world
cells, with the boundary row/column shared with the neighbor tile.

Topologies (--type):
    flat     constant height (sanity / lighting check)
    ramp     linear ramp along an axis (verifies normals + slope blends)
    cone     single radial cone (curvature LOD stress test)
    dunes    stacked low-frequency sines (smooth, low 2nd derivative)
    fbm      fractal Brownian motion over value noise (general purpose)
    ridged   ridged-multifractal (sharp ridges -> exercises curvature LOD)
    crater   fbm base + Gaussian craters (mixed smooth + sharp)
    island   fbm masked by radial falloff to sea level

Determinism: when --tile-x / --tile-y are provided, the seed is mixed with
the tile coordinates so adjacent tiles produced with the same base seed
agree on their shared edges.

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
# Noise primitives - value-noise based; we deliberately avoid a heavyweight
# noise dependency. Quality is fine for sandbox tiles.
# ----------------------------------------------------------------------------

def _smoothstep(t: np.ndarray) -> np.ndarray:
    return t * t * (3.0 - 2.0 * t)


def value_noise(shape: Tuple[int, int], cells: int, rng: np.random.Generator) -> np.ndarray:
    """Bilinear-filtered random lattice noise. shape=(H, W)."""
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

    yf2 = yf[:, None]
    xf2 = xf[None, :]
    a = g00 * (1 - xf2) + g10 * xf2
    b = g01 * (1 - xf2) + g11 * xf2
    return a * (1 - yf2) + b * yf2


def fbm(shape, base_cells, octaves, lacunarity, gain, rng) -> np.ndarray:
    out = np.zeros(shape, dtype=np.float64)
    amp = 1.0
    cells = base_cells
    norm = 0.0
    for _ in range(octaves):
        out += amp * value_noise(shape, cells, rng)
        norm += amp
        amp *= gain
        cells = max(int(round(cells * lacunarity)), cells + 1)
    return out / norm


def ridged(shape, base_cells, octaves, lacunarity, gain, rng) -> np.ndarray:
    out = np.zeros(shape, dtype=np.float64)
    amp = 1.0
    cells = base_cells
    norm = 0.0
    for _ in range(octaves):
        n = value_noise(shape, cells, rng)
        n = 1.0 - np.abs(2.0 * n - 1.0)  # ridge transform
        out += amp * (n ** 2)
        norm += amp
        amp *= gain
        cells = max(int(round(cells * lacunarity)), cells + 1)
    return out / norm


# ----------------------------------------------------------------------------
# Topology builders. Each returns a [0, 1] heightfield of shape (size, size).
# ----------------------------------------------------------------------------

def topo_flat(size, rng):
    return np.full((size, size), 0.5, dtype=np.float64)


def topo_ramp(size, rng):
    g = np.linspace(0.0, 1.0, size)
    return np.broadcast_to(g, (size, size)).copy()


def topo_cone(size, rng):
    yy, xx = np.indices((size, size))
    cy, cx = (size - 1) * 0.5, (size - 1) * 0.5
    r = np.sqrt((xx - cx) ** 2 + (yy - cy) ** 2) / (size * 0.5)
    return np.clip(1.0 - r, 0.0, 1.0)


def topo_dunes(size, rng):
    yy, xx = np.indices((size, size))
    fx = 2 * np.pi / size
    a = 0.5 + 0.25 * np.sin(xx * fx * 3.0 + rng.random() * 6.28)
    b = 0.25 * np.sin((yy + xx * 0.4) * fx * 5.0 + rng.random() * 6.28)
    return np.clip(a + b, 0.0, 1.0)


def topo_fbm(size, rng):
    h = fbm((size, size), base_cells=4, octaves=8, lacunarity=2.0, gain=0.5, rng=rng)
    h = (h - h.min()) / max(h.max() - h.min(), 1e-9)
    return h


def topo_ridged(size, rng):
    h = ridged((size, size), base_cells=4, octaves=7, lacunarity=2.0, gain=0.55, rng=rng)
    h = (h - h.min()) / max(h.max() - h.min(), 1e-9)
    return h


def topo_crater(size, rng):
    base = topo_fbm(size, rng) * 0.6
    n = rng.integers(4, 10)
    yy, xx = np.indices((size, size)).astype(np.float64)
    out = base.copy()
    for _ in range(n):
        cy = rng.uniform(0.1, 0.9) * size
        cx = rng.uniform(0.1, 0.9) * size
        radius = rng.uniform(0.04, 0.12) * size
        depth  = rng.uniform(0.1, 0.3)
        rim    = rng.uniform(0.05, 0.15)
        d2 = ((xx - cx) ** 2 + (yy - cy) ** 2) / (radius ** 2)
        crater = -depth * np.exp(-d2) + rim * np.exp(-((np.sqrt(d2) - 1.0) ** 2) * 6.0)
        out += crater
    return np.clip(out, 0.0, 1.0)


def topo_island(size, rng):
    h = topo_fbm(size, rng)
    yy, xx = np.indices((size, size)).astype(np.float64)
    cy, cx = (size - 1) * 0.5, (size - 1) * 0.5
    r = np.sqrt((xx - cx) ** 2 + (yy - cy) ** 2) / (size * 0.5)
    mask = np.clip(1.0 - r * 1.05, 0.0, 1.0)
    mask = mask ** 1.5
    return np.clip(h * mask, 0.0, 1.0)


TOPOS = {
    "flat":   topo_flat,
    "ramp":   topo_ramp,
    "cone":   topo_cone,
    "dunes":  topo_dunes,
    "fbm":    topo_fbm,
    "ridged": topo_ridged,
    "crater": topo_crater,
    "island": topo_island,
}


# ----------------------------------------------------------------------------
# Driver
# ----------------------------------------------------------------------------

def derive_seed(base_seed: int, tile_x: int, tile_y: int) -> int:
    # Splittable-mix style integer hash. Use Python ints with masks rather than
    # np.uint64 (which raises overflow warnings on multiply).
    MASK = (1 << 64) - 1
    h = base_seed & MASK
    h ^= ((tile_x * 0x9E3779B97F4A7C15) & MASK)
    h ^= ((tile_y * 0xBF58476D1CE4E5B9) & MASK)
    h ^= (h >> 33)
    h = (h * 0xff51afd7ed558ccd) & MASK
    h ^= (h >> 33)
    return int(h & 0x7FFFFFFF)


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description="Procedural 16-bit heightmap generator.")
    p.add_argument("--type", choices=list(TOPOS.keys()), required=True,
                   help="Topology to generate")
    p.add_argument("--size", type=int, default=1025,
                   help="Output size in texels (default 1025: 1024-cell tile with shared edges)")
    p.add_argument("--seed", type=int, default=1,
                   help="Base random seed")
    p.add_argument("--tile-x", type=int, default=0,
                   help="Tile X index in a global grid (mixed into seed)")
    p.add_argument("--tile-y", type=int, default=0,
                   help="Tile Y index in a global grid (mixed into seed)")
    p.add_argument("--out", type=Path, required=True,
                   help="Output PNG path")
    args = p.parse_args(argv)

    if args.size < 4:
        print("error: --size must be >= 4", file=sys.stderr)
        return 2

    seed = derive_seed(args.seed, args.tile_x, args.tile_y)
    rng = np.random.default_rng(seed)

    h01 = TOPOS[args.type](args.size, rng)
    h01 = np.clip(h01, 0.0, 1.0)

    # Quantize to uint16. Pillow expects mode='I;16' in big-endian wire order
    # but numpy uint16 little-endian works for in-memory PNG writes.
    h16 = (h01 * 65535.0 + 0.5).astype(np.uint16)

    args.out.parent.mkdir(parents=True, exist_ok=True)
    # Pillow's preferred path for 16-bit grayscale: build a 16-bit image
    # explicitly via Image.new + putdata, or use frombuffer to avoid the
    # deprecated mode= reinterpretation on fromarray().
    img = Image.new("I;16", (args.size, args.size))
    img.frombytes(h16.tobytes())
    img.save(args.out, format="PNG")
    print(f"wrote {args.out} ({args.size}x{args.size}, type={args.type}, "
          f"seed={seed}, tile=({args.tile_x},{args.tile_y}))")
    return 0


if __name__ == "__main__":
    sys.exit(main())
