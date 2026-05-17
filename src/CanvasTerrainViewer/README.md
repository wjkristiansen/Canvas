# CanvasTerrainViewer

> **Status:** design phase - no code yet. This document is the shared
> reference for collaborators and is expected to evolve as
> implementation lands.

A Canvas application that loads a height-field bitmap and renders a
tiled, adaptively-tessellated terrain using the existing `CanvasGfx12`
backend. Modeled after `CanvasModelViewer`: same windowing, FPS-style
camera, deferred renderer, and SDF text HUD; new asset type, new
renderable, new shader pipeline.

---

## Goals

- Render large heightfield-driven terrain at interactive frame rates.
- GPU hardware tessellation (HS/DS) with per-edge LOD that scales with
  both **screen-space distance** and **terrain curvature**.
- Composable heightfields: multiple stacked layers with feathered
  blend masks, so biomes, detail noise, erosion, and runtime overlays
  all share one path.
- Crack-free seams between adjacent tiles by construction (no skirts,
  no stitch strips).
- A 5-minute day/night cycle with a warm sun and dim cool moon as
  polar-opposite directional lights.

## Non-goals (for now)

- Mesh-shader / amplification-shader rendering path.
- Geo-clipmap streaming over wide worlds (the descriptor leaves room
  for it; the renderer does not implement it yet).
- Vegetation, decals, weather.
- Physics / collision against the terrain.

---

## Plan in stages

| Stage | Scope                                                                                                  |
|-------|--------------------------------------------------------------------------------------------------------|
| v1    | CPU-built static grid mesh per heightfield. Validates asset, transform, lighting, HUD.                 |
| v2    | GPU hardware tessellation (HS/DS) with distance + curvature LOD. Same asset.                           |
| v3+   | Multi-tile streaming + heightfield composition (stacked layers + blend masks). Animated water surface. |

The v1 path and the v2 path share the same `XHeightField` asset and
descriptor - only the renderable and the PSO differ, so v1 work is
not throwaway.

---

## Design

### Density - 1 m / texel (configurable)

- World spacing `dxy` is a per-heightfield property, default **1.0 m**.
- A tile is a power-of-two grid: default **1024 x 1024 texels ->
  1024 x 1024 m**.
- World-space tile origin is stored on the tile so multiple tiles can
  sit on a global integer grid.
- The base patch grid (control mesh fed to the tessellator) is
  **coarser** than the texel grid - e.g. 64x64 quads per tile (16 m
  patches at default density). Tessellation refines patches up to
  per-texel detail.

### Height-map storage format

| Format               | Bits | Precision @ 256 m height scale | Notes                                                    |
|----------------------|-----:|--------------------------------|----------------------------------------------------------|
| `R8_UNORM`           | 8    | ~1.0 m  (256 steps)            | Visible terracing; thumbnails only.                      |
| `R16_UNORM`          | 16   | ~4 mm   (65 536 steps)         | Native sampling, simplest shader path. **Default.**      |
| `R16_FLOAT`          | 16   | exponent-dependent             | Wastes bits for terrain; not recommended.                |
| `R32_FLOAT`          | 32   | exact                          | Authoring / source format only.                          |
| `R10G10B10A2_UNORM`  | 10   | ~25 cm                         | Worse than R16 for a single-channel signal; rejected.    |

**Decision:** runtime format is **`R16_UNORM`**, decoded as
`height_m = sample * heightScale + heightBias`.

### Height scale & bias

- `heightScale` (meters) and `heightBias` (meters) are per-heightfield
  constants in the per-tile cbuffer.
- Defaults: `heightScale = 64 m`, `heightBias = 0 m` (~ 1 mm
  precision with R16). Sandbox heightmaps tend to feel right at this
  scale; raise to 128 m+ for high-relief terrain.
- Height is bilinear-sampled in the DS. Normals come from
  central-differences against the height texture, computed in DS in
  v1/v2. A precomputed `R8G8_SNORM` normal-XY texture is an option
  if/when DS sampling cost becomes a problem.

### Tessellation - CPU vs GPU

| Aspect                | CPU tessellation                              | GPU tessellation (HS/DS)                              |
|-----------------------|-----------------------------------------------|-------------------------------------------------------|
| Vertex bandwidth      | High - full mesh re-uploaded on LOD change    | Low - only patch corners + height texture             |
| LOD granularity       | Per chunk (geomipmap / CDLOD / quadtree)      | Per edge, continuous                                  |
| Crack handling        | Skirts or stitch strips                       | Edge tess factors agree across patches                |
| Authoring complexity  | More CPU code, simpler shaders                | Tess pipeline state, HS/DS authoring                  |
| Compatibility         | Universal                                     | D3D12 baseline (yes)                                  |

**Both, in stages** (see Plan in stages above). v1 reuses the existing
`CreateMeshData` path; v2 introduces the dedicated **terrain PSO
family** (kept separate from the deferred uber-PSO so HS/DS variants
do not pollute the opaque PSO permutation matrix). Both paths write
the same G-buffer layout, so deferred lighting and composition are
unchanged.

### Tessellation level - distance x curvature

Per-edge tess factor is the **product** of a distance term and a
curvature term. Either alone is wrong: distance alone wastes
triangles on distant flat ground, curvature alone wastes triangles
on a sharp ridge a kilometer away.

#### Distance term - screen-space target

The right metric is **edge length in pixels**, not raw world distance.
Per edge, in HS:

```
edgeMidWorld   = midpoint of the two control-point world positions
edgeLenWorld   = |cp_b - cp_a|
distEye        = length(edgeMidWorld - eyePos)
pixelsPerMeter = (viewportHeight * 0.5) / (distEye * tan(fovY * 0.5))
edgePixels     = edgeLenWorld * pixelsPerMeter
tessDist       = clamp(edgePixels / targetEdgePixels, 1, maxTess)
```

- `targetEdgePixels` is a tunable, default ~ **8 px**.
- This automatically yields `1 / distance` falloff with correct FOV
  response (zooming in gets more triangles, as it should).

#### Curvature term - 2nd derivative

```
d2        ~ |h(p+d) - 2*h(p) + h(p-d)| / d^2
curvBoost = 1 + curvatureGain * sqrt(d2_at_edge_mid)
```

Sampled from a **mip-matched** height texture so the estimate stays
stable across LODs.

#### Composition

```
edgeFactor   = clamp(tessDist * curvBoost, 1, maxTess)   // maxTess = 64
insideFactor = average of the four edge factors
```

| Distance | Curvature | Result                                                   |
|----------|-----------|----------------------------------------------------------|
| Near     | Flat      | Low (curvBoost ~ 1, tessDist modest)                     |
| Near     | Curved    | **High** - both terms contribute                         |
| Far      | Flat      | Low                                                      |
| Far      | Curved    | Low - subpixel detail isn't worth subdividing            |

A global `lodBias` constant is HUD-tweakable for visual tuning.

#### Other distance-driven simplifications

- **Patch frustum culling**: HS sets all factors to 0 for off-screen
  patches, killing them before any DS work.
- **Material atlas mips**: PS uses `Texture.SampleGrad` with
  world-derivative-driven gradients so mip selection is stable across
  the tessellation cliff.
- **Layer-count LOD** (future, with composition): tiles past a
  distance threshold collapse to base layer only.

### Seams and content blending

Two distinct concerns; conflating them produces either visible cliffs
or visible cracks.

#### Crack-free sampling - shared-edge tiles

About the **DS sampling identical values on a shared edge** so the
two tiles produce the same world-space vertex position. Not a content
problem; both sides already agree on the data.

- Tiles are sized `(N+1) x (N+1)` texels to cover `N x N` cells of
  world space. The last column / row of each tile is shared with the
  first column / row of the neighbor: same world position, same height
  value. Water-tightness is a structural invariant on tile dimensions.
- For bilinear DS sampling, having the boundary texels match is
  sufficient to eliminate cracks.
- Always on, no perceptible cost.

#### Content blending - heightfield composition

The actual "mountain tile next to plain tile" problem. Shared-edge
matching does the wrong thing here (visible cliff). The clean
formulation is **layered sampling**:

```
H_world(x, y) = Sum_i w_i(x, y) * H_i(x, y)
```

normalized by `max(Sum w_i, eps)` so layers don't have to sum to 1.

- Each `H_i` is a `XHeightField` layer with its own `dxy` /
  `heightScale` / `heightBias`.
- Influence masks `w_i(x, y)` in [0, 1] come in three flavors:
  - **Feathered rect** - inner rect at full weight, outer falloff over
    `blendWidth` (texels or meters). Curve enum:
    `linear` / `smoothstep` (cubic) / `smootherstep` (quintic).
  - **Mask texture** - R8 weight map sampled bilinearly. Hand-painted
    or generator-produced.
  - **Procedural** - SDF / radial / noise-modulated callback.
- Per-layer dxy lets a coarse continental layer (e.g. 64 m/texel) sit
  under a fine detail layer (1 m/texel) - both sampled in DS and
  combined.

#### Renderer impact

The DS does the layer composition: bind up to **K = 4** active layers
as a small texture array + per-layer cbuffer. Layer count per tile is
data-driven (usually 1-2; only blend zones touch all 4). Each layer's
height and weight are sampled, summed, normalized; the resulting
position + analytic gradient feed the curvature LOD logic.

A single-layer fast path keeps v1/v2 cost identical to today.

#### Use cases the composition path enables

- **Detail noise** added on top of a base heightfield (small `H_i`,
  high frequency, weight 1).
- **Erosion / hydraulic passes** baked as deltas over a base.
- **Crater / impact overlays** dropped at runtime as small layers
  with feathered radial masks.
- **LOD hand-off** between a low-res streamed tile set and a
  high-res insert - both layers exist in the overlap, weights
  crossfade.
- **Authoring sandbox** - drop a "mountain" heightfield over a
  "plain" base with a feathered mask, get a smooth transition with
  no manual edge editing.

#### Asset split

- `XHeightField` - a single shared-edge grid + grid-cell index `(ix, iy)`.
- `XHeightFieldComposition` - list of layers + masks. The renderer
  queries this per region; its result is what's bound to the DS.

This keeps v1/v2 simple (single layer, weight 1) while the API shape
is already correct for the eventual N-layer feathered-blend world.

### Material - altitude + slope blend

Pixel shader writes albedo / normal / roughness into the existing
G-buffer layout.

- Material atlas: **2x2 of 1024x1024** packed into 2048x2048,
  slot order `[grass, rock, sand, snow]`.
- Two textures: `terrain_atlas_albedo` (8-bit sRGB) and
  `terrain_atlas_orm` (8-bit linear, R = AO, G = roughness,
  B = metallic ~ 0; matches glTF ORM packing).
- Blend logic per pixel:
  ```
  grassiness = saturate(slopeBlend(normal.z) * altitudeBlend(height))
  ```
- World-XY-derived UVs so adjacent tiles seam naturally.
- Sampled with `SampleGrad` for stable mip selection.

### Day / night cycle

- **5-minute** real-time cycle, configurable `cycleSeconds`
  (default 300).
- Two **polar-opposite directional `XLight`s** driven by phase
  `theta = 2pi * t / cycleSeconds`:
  - **Sun:** direction `(cos theta, 0, sin theta)`; warm yellow-white
    `~(1.00, 0.95, 0.80)`; intensity gated by `smoothstep` on
    `sin theta` so it falls to ~0 below the horizon.
  - **Moon:** direction = `-sun`; cool blue
    `~(0.55, 0.65, 0.95)`; peak intensity ~ **1/40** of the sun;
    gated by `smoothstep` on `-sin theta`.
  - Both lights can be non-zero simultaneously near dawn/dusk for a
    smooth crossfade.
- Ambient `XLight` modulated for warm horizon light and dim blue
  night. The ambient floor is deliberately non-trivial (not just
  pitch-black at night, not just "absence of direct light" during the
  day) so unlit / back-facing surfaces stay readable. Magnitude is
  tuned against the deferred composite's exposure so a slope facing
  away from the sun reads as "in shadow" without becoming a black
  silhouette. Future improvement: drive ambient from a sample of the
  low-mip sky cubemap.
- HUD shows time-of-day (HH:MM on a virtual 24 h clock).
- Hotkeys: `[` / `]` scrub +/-15 min; `\` toggles pause.
- Implemented as a small `DayNightCycle` helper in the viewer app -
  no engine-level changes; uses the existing directional + ambient
  `XLight` types.
- Drives the sky tint, sun/moon sprite positions, and the shadow
  pass orientation (see *Shadow casting* below).

### Shadow casting

Each directional light (sun, moon) gets its own orthographic depth
pass that renders the terrain (and, later, water and any opaque
non-terrain geometry) from the light's point of view. The deferred
composite samples both maps and uses the result as a per-light
visibility multiplier on top of the NdotL term.

Staging:

- **v1** - render only the sun's shadow map; moon contribution
  computed without shadows (very low intensity; negligible visual
  cost to skip).
- **v2** - add the moon shadow map. Same pipeline as the sun, just
  with the polar-opposite light direction.
- **v3 (future)** - cascaded shadow maps (CSM) for the sun, to
  preserve fidelity across the full visible terrain range without a
  single huge texture.

Design notes:

- **Projection:** orthographic, because directional lights are at
  infinity. Bounds are computed each frame from the camera's view
  frustum projected onto the light-space plane and snapped to the
  shadow map texel grid to avoid edge-shimmer when the camera moves.
- **Texture:** depth-only target, `D32_Float` (preferred for range
  + precision) or `D16_UNorm` (smaller; revisit if memory tight).
  Resolution `2048 x 2048` as the v1 default, tunable from the
  HUD / command line.
- **Render pass:** a new shadow-only PSO that re-uses the terrain
  vertex stage (and the v2 HS/DS stages once they exist) but binds
  no pixel shader. Two passes per frame in v2 (sun + moon).
- **Filtering:** 3x3 PCF in the composite shader. Avoids hard
  aliased shadow edges without large blur-kernel cost.
- **Bias:** small constant depth bias plus a slope-scaled term
  driven by `dot(N, L)` to suppress acne on lit slopes without
  introducing peter-panning on flat ground.
- **Culling:** the shadow pass uses front-face culling (instead of
  the usual back-face) to keep depth comparisons stable on the lit
  side. Doubles as a cheap mitigation for shadow acne.
- **Quality vs perf knob:** `--shadowmap-size <N>` and
  `--shadow-bias <f>` on the command line; HUD tweakable later.

Day/night coupling:

- Shadow contribution fades out with the light's own intensity gate
  (`sunGate` / `moonGate` from the day/night cycle). Below-horizon
  lights skip their shadow pass entirely.
- Near the horizon, very long shadow ortho bounds make small far
  features dominate. The shadow projection snaps to a maximum
  reasonable extent so dawn/dusk doesn't blow the cascade budget.

### Sky

The terrain needs something behind it. The sky is staged so v1 can be
trivial while the eventual implementation has room to grow.

#### v1 - single cubemap, time-of-day tint

- One sRGB cubemap (e.g. **`R8G8B8A8_UNORM_SRGB`**, per-face 512^2),
  authored or generator-produced.
- Drawn as a fullscreen pass after the G-buffer composite, using a
  `VSFullscreen` + `PSSky` pair. Depth test = `GREATER_EQUAL` against
  the cleared far-plane depth so the sky fills only un-shaded pixels;
  no actual cube geometry needed.
- Color modulated by the day/night phase:
  - Day tint ~ neutral (1, 1, 1).
  - Night tint ~ deep blue `~(0.05, 0.07, 0.15)`.
  - Horizon dawn/dusk tint applied as a smoothstep band around
    `sin theta ~ 0`.
- This single cubemap also feeds the ambient `XLight` color -
  sampling its low mip per frame gives a cheap, plausible
  environment ambient that "tracks" the sky tint.

#### v2 - orbiting sun and moon sprites

- Sun and moon rendered as **screen-aligned sprites**, *not* baked
  into the cubemap.
- Position: project the day/night sun direction `(cos theta, 0, sin theta)`
  (and `-sun` for moon) into clip space, drop a quad of fixed
  angular size (e.g. ~0.5 deg for the moon, ~0.55 deg for the sun, both
  HUD-tunable).
- Textured with a small RGBA sprite (soft-edged disc + optional
  corona). Additive blend; tinted by the same sun/moon colors used
  for the directional lights.
- Drawn after the sky cube, before opaque terrain, with depth write
  off and depth test = far-plane equal so terrain occludes them.
- Cheap, decoupled from the sky shader, and trivially extends to
  multiple celestial bodies later.

#### v3 - layered sky shader (future)

- Multiple cubemaps blended in shader: e.g.
  `day`, `dusk`, `night`, optionally `overcast`.
- Per-pixel weights driven by `sin theta` (day/night) and a separate
  weather scalar.
- Procedural horizon term: angular falloff toward the horizon line
  with a separately tinted band, computed in PS from the view
  direction's `z` component - sells sunrise/sunset without needing
  per-frame texture authoring.
- Optional simple analytic atmospheric scattering
  (Preetham / Hosek-Wilkie style) replaces the day cubemap when we
  want a fully procedural sky. Out of scope until the cubemap
  approach hits its limits.

#### Authoring

- A dev-time generator `gen_skycube.py` produces a placeholder cube
  per "preset" (`day`, `night`, `dusk`) by:
  - Filling each face with a vertical gradient (zenith -> horizon) in
    a per-preset palette.
  - Optional star field overlay for `night` (white pixels with
    Poisson-disk distribution, intensity-jittered).
  - Saving as a 6-face DDS (BC1 sRGB) or 6 PNG faces.
- Output to `src/CanvasTerrainViewer/assets/sky/`, regenerated by
  `regen_all.ps1 / .sh`.



---

## Project layout

```
src/CanvasTerrainViewer/
    CMakeLists.txt
    CanvasTerrainViewer.cpp        (entry point + app shell)
    HeightField.cpp / .h           (asset + CPU mesh builder)
    QLogAdapter.h                  (XLogger adapter over QLog)
    pch.h                          (precompiled header)
    CanvasTerrainViewer.rc / .ico  (icons + version info)
    README.md                      (this file)

src/HLSL/
    VSTerrain.hlsl               (v1 + v2 vertex stage)
    HSTerrain.hlsl               (v2)
    DSTerrain.hlsl               (v2)
    PSTerrain.hlsl               (writes existing G-buffer layout)
    Terrain.hlsli                (shared constants, sampling helpers)
    PSSky.hlsl                   (fullscreen sky with time-of-day tint)
    VSCelestialSprite.hlsl       (sun/moon sprite vertex stage, v2)
    PSCelestialSprite.hlsl       (sun/moon sprite pixel stage, v2)

scripts/terrain/                 (sandbox asset generators, dev-time)
    gen_heightmap.py
    gen_tileset.py
    gen_material_atlas.py
    gen_mask.py
    gen_skycube.py
    regen_all.ps1 / regen_all.sh

src/CanvasTerrainViewer/assets/  (sample assets shipped with the viewer)
    default_heightmap.png        (generated; checked in)
    ... (atlas + sky added in later milestones)
```

`CanvasGfx12` gains a dedicated **terrain PSO family** (HS/DS variants
isolated from the deferred uber-PSO permutation matrix). `XGfxDevice`
exposes `CreateTerrainPipeline()` / `CreateHeightFieldTexture()` as
needed.

### Source asset ingest

- **Windows:** anything **WIC** can decode (PNG, TIFF, BMP,
  JPEG-XR, ...); 16-bit sources pass through, 8-bit promoted to R16.
  WIC decode lives in `CanvasHeightField` behind a platform `#ifdef`.
- **Non-Windows (future):** minimal PNG / RAW path.

---

## Sandbox asset generators

Procedural Python scripts (Windows + cross-platform; rely only on
`numpy` + `Pillow`) live under `scripts/terrain/`. They are dev-time
tools, not part of the runtime build, but emit assets the viewer
loads directly.

### `gen_heightmap.py`

Outputs a 16-bit grayscale PNG sized for one tile (default 1025x1025,
i.e. 1024 world cells per side under the shared-edge convention).

| `--type`  | Description                                                                 |
|-----------|-----------------------------------------------------------------------------|
| `flat`    | Constant height - sanity / lighting check.                                  |
| `ramp`    | Linear ramp along an axis - verifies normals + slope-blend material.        |
| `cone`    | Single radial cone - curvature LOD stress test.                             |
| `dunes`   | Stacked low-frequency sines - smooth, low 2nd derivative.                   |
| `fbm`     | Fractal Brownian motion over Perlin/simplex noise - general purpose.        |
| `ridged`  | Ridged-multifractal - sharp ridges, exercises curvature LOD.                |
| `crater`  | fbm base + Gaussian craters - mixed smooth + sharp features.                |
| `island`  | fbm masked by radial falloff to sea level - water-surface test (future).    |

Common flags: `--size 1025`, `--seed N`, `--height-scale 256`,
`--out path.png`, `--tile-x I --tile-y J` (deterministic seed
`hash(seed, I, J)` so adjacent tiles agree on their shared edges).

### `gen_tileset.py`

Drives `gen_heightmap.py` for an NxN grid and writes each tile such
that adjacent tiles' shared edges match exactly - gives a real
multi-tile dataset for the seam work.

### `gen_mask.py`

Produces R8 PNG masks with feathered edges (linear / smoothstep /
smootherstep), radial / SDF, and noise-modulated variants. Used to
author blend zones between sandbox biome tiles (e.g. mountain over
plain).

### `gen_material_atlas.py`

Procedural material atlas. Default layout: 2x2 of 1024x1024 in a
single 2048x2048 image, slot order `[grass, rock, sand, snow]`. Per
material:

- `*_albedo.png` - 8-bit sRGB, value-noise + warped octaves tinted to
  a per-material palette.
- `*_orm.png` - 8-bit linear, R = AO, G = roughness, B = metallic
  (glTF ORM packing).

Combined outputs `terrain_atlas_albedo.png` + `terrain_atlas_orm.png`
let the PS sample with one atlas-tile-index lookup.

Flags: `--seed`, `--tile-size 1024`, `--materials grass,rock,sand,snow`,
`--out-dir`. Deterministic.

### `gen_skycube.py`

Placeholder sky cubemaps. Per `--preset` (`day`, `night`, `dusk`):
fills each face with a per-preset vertical gradient (zenith -> horizon),
optional Poisson-disk star field for `night`, saves as a 6-face DDS
(BC1 sRGB) or 6 PNG faces. Companion `gen_celestial_sprite.py` (or
flag in this script) produces simple soft-edged disc sprites for sun
and moon.

### `regen_all.ps1` / `regen_all.sh`

Convenience driver that regenerates the default tile set + atlas the
viewer launches with by default. Output goes to
`src/CanvasTerrainViewer/assets/`, overwriting the committed defaults.
Re-run after changing the generator scripts or the desired sample
content; commit the regenerated PNGs if you want them to ship.

---

## Open questions / known gaps

- Authoring vs runtime: do we want to bake mip chains and normal maps
  offline (faster startup) or generate at load time (simpler tooling)?
  Plan currently assumes load-time generation.
- Streaming: the descriptor leaves room for paged tile loading, but
  this README intentionally does not specify an LRU / prefetch
  policy - to be designed when the viewer can render multiple tiles.
- Water surface (animated, with reflections) is captured as a future
  todo and will likely warrant its own README.
