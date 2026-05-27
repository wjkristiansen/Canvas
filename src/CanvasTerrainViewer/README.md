# CanvasTerrainViewer

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

| Stage | Status       | Scope                                                                                                  |
|-------|--------------|--------------------------------------------------------------------------------------------------------|
| v1    | **Done**     | CPU-built static grid mesh per heightfield. Validates asset, transform, lighting, HUD.                 |
| v2    | **Done**     | GPU hardware tessellation (HS/DS) with distance + curvature LOD. Same asset. Primary render path.      |
| v3+   | Not started  | Multi-tile streaming + heightfield composition (stacked layers + blend masks). Animated water surface. |

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

Both directional lights (sun and moon) cast shadows on the displaced
terrain.  Each gets its own orthographic depth pass that renders the
terrain from the light's point of view into a tile of a shared shadow
atlas; the deferred composite samples the appropriate tile via
hardware PCF and uses the result as a per-light visibility multiplier
on top of the `NdotL` term.

Shipped (v1):

- Sun and moon both cast: each `XLight` is created with
  `LightFlags::Enabled | LightFlags::CastsShadows` and configured
  with `SetShadowResolution(2048)`,
  `SetDirectionalShadowExtent(256 m, 1024 m)`, and the default bias
  triple (`SetShadowDepthBias(1e-4, 2.0, 0.5 texels)`).
- The backend's shadow atlas is a single `D32_Float` surface (DSV +
  SRV) divided into a fixed 2×2 grid of 2048² tiles -- room for up
  to four directional shadow casters per frame.  See
  `src/CanvasGfx12/README.md` "Light Submission" + "Composition" for
  the engine-side flow (atlas allocation, depth-only displaced PSO,
  per-light `GpuTask` insertion, automatic `DSV -> SRV` barrier,
  composite PCF).
- Self-shadowing: caster-side rasterizer bias is baked into the
  shadow PSO (`DepthBias = 1000`, `SlopeScaledDepthBias = 2.0`);
  receiver-side constant bias + world-space normal-offset apply at
  composite sample time and are tunable per light via
  `XLight::SetShadowDepthBias`.
- 2×2 hardware PCF via a `SamplerComparisonState` (s3) configured
  `GREATER_EQUAL` (reverse-Z) and `OPAQUE_WHITE` border so receivers
  outside the shadow frustum read as fully lit rather than fully
  shadowed.

Future work:

- Cascaded shadow maps for the sun, to preserve fidelity across the
  full visible terrain range.  v1's single 256 m half-width slab
  captures most of what the camera sees but misses distant features
  near dawn/dusk.
- Skip the moon's shadow pass when its intensity gate is near zero,
  saving the cost during full daylight.  The frame cost is already
  small (one 2048² depth fill of the terrain) but the saving is
  free with one branch.
- Per-light caster-side rasterizer bias (currently baked into the
  shared shadow PSO).  Either via `RSSetDepthBias` or one PSO
  variant per light.

### Sky

The terrain needs something behind it.

#### Implemented sky system

The sky is integrated into `PSComposite` (not a separate sky shader) via the engine's `GfxBackgroundDesc` / `XScene::SetBackground` API. The viewer configures:

- **Up to two cubemaps** (`pSkyboxCubemapA`, `pSkyboxCubemapB`) with a CPU-driven `BlendFactor` for crossfading between presets (e.g. day → dusk → night). Both cubemaps can be bound simultaneously; the composite lerps between them in the shader so transitions are smooth without per-frame texture authoring.
- **Stars cubemap** (`pStarsCubemap`): additively blended over the sky, lower hemisphere clipped, driven by `StarsOrientation` for sidereal-motion animation.
- **Procedural sun disc** (`SunDirection`, `SunColor`): a soft-edged disc rendered analytically in the composite shader, driven by the same unit vector used for the sun's directional light.
- **Moon billboard** (`pMoonTexture`, `MoonDirection`): a 2D RGBA texture blended as a billboard at the angular position of the moon's directional light.

The viewer's `CDayNightCycle` helper updates `SunDirection`, `MoonDirection`, and the cubemap `BlendFactor` each frame to animate the sky in step with the light positions.

#### v3 - layered sky shader (future)

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

- `gen_skycube.py` (in `scripts/terrain/`) generates per-preset cubemaps (`day`, `dusk`, `night`) as 6 PNG faces and an optional moon sprite.
- `gen_starscube.py` generates a separate star-field RGBA cubemap.
- Output goes to `src/CanvasTerrainViewer/assets/sky/`.



---

## Project layout

```
src/CanvasTerrainViewer/
    CMakeLists.txt
    CanvasTerrainViewer.cpp        (entry point, app shell, day/night cycle)
    HeightField.cpp / .h           (asset loading, CPU mesh builder, mip chain)
    SceneConfig.cpp / .h           (JSON scene file parsing)
    SkyCubeLoader.cpp / .h         (cubemap loading for sky / stars)
    TerrainMaterial.cpp / .h       (terrain material + displacement setup)
    QLogAdapter.h                  (XLogger adapter over QLog)
    pch.h                          (precompiled header)
    CanvasTerrainViewer.rc / .ico  (icons + version info)
    README.md                      (this file)
    assets/
        default_heightmap.png      (sample fbm heightfield)
        scene.json                 (default scene configuration)
        terrain_atlas_albedo.png   (2048×2048, four 1024×1024 slots: grass/rock/sand/snow)
        terrain_atlas_orm.png      (matching ORM atlas)
        sky/                       (per-preset PNG face images and moon sprite)

src/HLSL/
    VSDisplaced.hlsl             (terrain vertex stage: control-point UV gen from SV_VertexID)
    HSDisplaced.hlsl             (hull shader: distance + curvature tessellation LOD)
    DSDisplaced.hlsl             (domain shader: heightmap sample, world position, normals)
    DSDisplacedShadow.hlsl       (depth-only domain shader for shadow atlas passes)
    PSDisplaced.hlsl             (pixel shader: atlas sample, G-buffer write)
    Displaced.hlsli              (shared constants and sampling helpers for displaced shaders)
    PSComposite.hlsl             (composition: deferred lighting + skybox + stars + sun + moon)

scripts/terrain/                 (sandbox asset generators, dev-time)
    gen_heightmap.py
    gen_material_atlas.py
    gen_skycube.py               (sky cubemap generator; also produces moon sprite via --moon flag)
    gen_starscube.py             (separate star-field cubemap generator)
```

The displaced PSO family (`VSDisplaced` + `HSDisplaced` + `DSDisplaced` + `PSDisplaced`, and the depth-only shadow variant) lives entirely inside `CanvasGfx12`. A material with `GfxDisplacementDesc` attached triggers this path automatically; `XGfxDevice` requires no new factory methods. Sky, stars, sun, and moon rendering are integrated into `PSComposite` and configured via `GfxBackgroundDesc` on `XScene`.

### Source asset ingest

- **Windows:** anything **WIC** can decode (PNG, TIFF, BMP,
  JPEG-XR, ...); 16-bit sources pass through, 8-bit promoted to R16.
  WIC decode lives in `CanvasHeightField` behind a platform `#ifdef`.
- **Non-Windows (future):** minimal PNG / RAW path.

---

## Sandbox asset generators

Procedural Python scripts (rely only on `numpy` + `Pillow`) live under
`scripts/terrain/`. They are dev-time tools, not part of the runtime
build, but emit assets the viewer loads directly.

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
fills each face with a per-preset vertical gradient (zenith → horizon)
and saves as 6 PNG faces. Pass `--moon` to also emit a soft-edged
disc moon sprite (`moon.png`).

### `gen_starscube.py`

Separate star-field cubemap generator. Produces a Poisson-disk
distributed star field in RGBA PNG faces (rgb = per-star colour, a =
coverage). The composite shader additively blends this over the skybox,
driven by `StarsOrientation` for sidereal-motion animation.

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
