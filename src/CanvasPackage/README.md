# CanvasPackage

A compiled binary asset package for Canvas — the canonical runtime format for loading scenes,
models, materials, and animations without depending on FBX or any DCC tool at runtime.

FBX files are the *source* format (authored in Blender, Maya, etc.). A "bake" step converts
them into `.cpkg` files that the runtime can load directly — faster, smaller, and free of
ufbx/FBX SDK dependency.

---

## Binary File Layout

```
[Header]          fixed 24 bytes
[Chunk Table]     ChunkCount * 40 bytes
[Chunk Data]      variable-length blobs at offsets given by the table
```

### Header (24 bytes)

| Offset | Size | Field        | Notes                                                    |
|--------|------|--------------|----------------------------------------------------------|
| 0      | 4    | Magic        | ASCII `CPKG` (0x43504B47)                                |
| 4      | 2    | VersionMajor | Breaking format changes increment this                   |
| 6      | 2    | VersionMinor | Backwards-compatible additions increment this            |
| 8      | 4    | ChunkCount   | Number of entries in the Chunk Table                     |
| 12     | 4    | Flags        | bit 0 = must be 1 (little-endian); all other bits 0      |
| 16     | 4    | Reserved     | 0                                                        |
| 20     | 4    | HeaderCRC32  | CRC32 of bytes [0..19]                                   |

Current version: `1.0`. All v1 files are little-endian; `Flags` bit 0 is a validation
assertion, not a branch. Loaders return an error if it is clear.

### Chunk Table Entry (40 bytes each, immediately after header)

| Offset | Size | Field         | Notes                                                  |
|--------|------|---------------|--------------------------------------------------------|
| 0      | 4    | FourCC        | Chunk type identifier (see table below)                |
| 4      | 2    | Version       | Chunk format version (independent of header version)   |
| 6      | 2    | Flags         | bit 0 = zlib-compressed (reserved, always 0 in v1)     |
| 8      | 8    | Offset        | Byte offset from start of file to chunk data           |
| 16     | 4    | SizeCompressed| Byte count on disk (== SizeRaw when uncompressed)      |
| 20     | 4    | SizeRaw       | Uncompressed byte count                                |
| 24     | 16   | ChunkGUID     | 128-bit identifier (zeroed in v1; reserved for dedup,  |
|        |      |               | hot-reload, and cross-package references in v2+)       |

Readers that do not recognize a FourCC must skip the chunk using its Offset + SizeCompressed.
Only `NODE` and `MESH` are required for a minimal renderable scene; all others are optional.

### Chunk Alignment

- Each chunk's `Offset` is a multiple of 4. Writers pad the preceding chunk's data with
  zero bytes to the next 4-byte boundary before starting the next chunk.
- Within a `MESH` chunk, each vertex stream array (Positions, Normals, UV0, Tangents,
  SkinVertices) begins on a 16-byte boundary relative to the start of the file. Writers
  insert zero-padding between the per-part header fields and the first stream, and between
  streams, to enforce this. Readers must skip the same padding.

---

## Chunk Types

### `NODE` — Scene Graph (required)

Flat array of nodes. Each node references its parent by index. Depth-first order is not
required; loaders build the hierarchy by walking parent indices after reading all nodes.
Nodes reference mesh, light, and camera payloads by index into their respective chunk arrays.

```
NodeCount             : uint32
ActiveCameraNodeIndex : int32     (-1 = none)
Per node:
  NameLen     : uint32
  Name        : char[NameLen]     (UTF-8, null terminator included in length)
  ParentIndex : int32             (-1 = root)
  Translation : float[4]          (W = 0, Canvas row-vector Z-up space)
  Rotation    : float[4]          (xyzw unit quaternion)
  Scale       : float[4]          (W = 0)
  MeshIndex   : int32             (-1 = none)
  LightIndex  : int32             (-1 = none)
  CameraIndex : int32             (-1 = none)
```

### `MESH` — Geometry (required for renderable scenes)

All meshes in one chunk. Each mesh has one or more material partitions (parts), matching the
`PackageMesh / PackageMeshPart` layout. Data is stored in Canvas convention (Z-up,
row-vector matrices, V-flipped UVs for D3D).

Vertex streams within each part are 16-byte aligned (see Chunk Alignment above).

```
MeshCount : uint32
Per mesh:
  NameLen   : uint32
  Name      : char[NameLen]
  PartCount : uint32
  BoundsMin : float[3]
  BoundsMax : float[3]
  Per part:
    MaterialIndex : int32
    VertexCount   : uint32
    StreamFlags   : uint8   (bit 0 = has UV0, bit 1 = has Tangents, bit 2 = has Skin)
    _pad          : uint8[3]
    -- 16-byte alignment pad to file boundary --
    Positions[VertexCount]    : float[4]   (W = 1)
    -- 16-byte alignment pad --
    Normals[VertexCount]      : float[4]   (W = 0, unit length)
    -- 16-byte alignment pad (if UV0 present) --
    UV0[VertexCount]          : float[2]   (only if StreamFlags.UV0)
    -- 16-byte alignment pad (if Tangents present) --
    Tangents[VertexCount]     : float[4]   (xyz=T, w=bitangent sign; only if StreamFlags.Tangents)
    -- 16-byte alignment pad (if Skin present) --
    SkinVertices[VertexCount] : { uint32[4] BoneIndices, float[4] Weights }  (only if StreamFlags.Skin)
  Skin (always written per mesh):
    HasSkin              : uint8
    _pad                 : uint8[3]
    BoneCount            : uint32           (0 when HasSkin == 0)
    BoneNodeIndices[N]   : int32            (N = BoneCount)
    InvBindPoses[N]      : float[16]        (row-major 4x4, Canvas row-vector space)
```

### `MATL` — Material Library

PBR metallic-roughness materials. Texture indices reference entries in the `TXTR` chunk;
-1 means the slot is unbound (the Factor value acts as a solid constant).

```
MaterialCount : uint32
Per material:
  NameLen                      : uint32
  Name                         : char[NameLen]
  BaseColorFactor              : float[4]   (linear RGBA)
  EmissiveFactor               : float[4]   (linear RGB, A unused)
  RoughMetalAOFactor           : float[4]   (R=roughness, G=metallic, B=AO, A=spare)
  AlbedoTextureIndex           : int32
  NormalTextureIndex           : int32
  EmissiveTextureIndex         : int32
  RoughnessTextureIndex        : int32
  MetallicTextureIndex         : int32
  AmbientOcclusionTextureIndex : int32
```

MATL is expected to grow in future chunk versions (clearcoat, transmission, IOR, alpha mode,
double-sided flag, etc.). Chunk versioning handles this without breaking old loaders.

### `TXTR` — Texture Table

One entry per referenced texture. A texture is a full GPU resource of any dimension (1D, 2D,
3D, or cube), not just a 2D image: `Dimension`, `Width`, `Height`, `Depth`, `ArraySize`, and
`MipCount` mirror `Canvas::GfxSurfaceDesc` so the entry round-trips straight into
`XGfxDevice::CreateSurface`.

`Name` is the runtime lookup key (e.g. `"sky_px"`). It is non-empty only for textures
declared explicitly in the build manifest. FBX-sourced textures that have no manifest
entry store an empty string for `Name` and are found by their material binding index.

The payload is **stored per subresource**, never as one opaque blob. Each subresource is one
mip / array / depth slice; a `SubresTable` of `(Offset, Size, RowPitch)` triples maps logical
subresource index (D3D order: `mip + arraySlice * MipCount`) to a byte range within the
texture's payload. This lets a loader pre-allocate the full mip chain from the metadata and
then upload — or **stream** — individual subresources on demand. Because the table carries
explicit offsets, the writer is free to order the payload coarsest-mip-first so a streaming
loader can read a contiguous prefix to bring up low-detail mips before the rest arrives, and
to pad each `Offset` for GPU / DirectStorage placement alignment.

`Format` is a `Canvas::GfxFormat` value (see `CanvasTypes.h`); `GfxFormat::Unknown` (0) means
"encoded source image; decode at runtime to determine GPU format." sRGB-ness is carried by
the format itself (the `_UNorm_SRGB` variants), so there is no separate color-space field.

Three payload cases:
- **Embedded GPU-format texture** — `PayloadSize > 0`, `SubresCount == MipCount * ArraySize`
  (cube/array) or `== MipCount` (3D), one entry per slice.
- **Embedded encoded-source image** (PNG/JPEG/HDR) — `Format = Unknown`, `SubresCount = 1`
  covering the whole encoded blob, `RowPitch = 0`.
- **External file** — `PayloadSize = 0`, `SubresCount = 0`; `Path` (always `.cpkg`-relative,
  never absolute) points at a container (e.g. DDS) that carries its own subresource layout.

```
TextureCount : uint32
Per texture:
  NameLen      : uint32
  Name         : char[NameLen]    (UTF-8, null terminated; empty for unnamed FBX-sourced textures)
  PathLen      : uint32
  Path         : char[PathLen]    (UTF-8, null terminated; relative to .cpkg directory; empty when embedded)
  Format       : uint32           (Canvas::GfxFormat value; 0 = Unknown / encoded source image)
  Dimension    : uint32           (Canvas::GfxSurfaceDimension; 0=1D, 1=2D, 2=3D, 3=Cube)
  Width        : uint32
  Height       : uint32           (1 for 1D)
  Depth        : uint32           (1 unless 3D)
  ArraySize    : uint32           (1; 6 * cubeCount for cube maps)
  MipCount     : uint32           (1 = base level only)
  SubresCount  : uint32           (0 for external; 1 for encoded source)
  SubresTable[SubresCount]:
    Offset     : uint64           (byte offset from this texture's payload base)
    Size       : uint32
    RowPitch   : uint32           (bytes per row for GPU upload; 0 for encoded source)
  PayloadSize  : uint64           (0 = external file)
  Payload      : uint8[PayloadSize]   (subresource bytes; recommended coarsest-mip-first order)
```

`Format` stores the `Canvas::GfxFormat` enumerant directly; no separate package-specific
format enum is used. Common bake outputs are the encoded-source sentinel (`GfxFormat::Unknown`)
for PNG/JPEG/HDR images, and the BCn variants (`BC1_UNorm`, `BC1_UNorm_SRGB`, `BC3_UNorm`,
`BC5_UNorm`, `BC7_UNorm`, `BC7_UNorm_SRGB`) for pre-compressed, mip-mapped textures.

### `LITE` — Light Definitions

```
LightCount : uint32
Per light:
  NameLen           : uint32
  Name              : char[NameLen]
  Type              : uint32    (Canvas::LightType enum value)
  Color             : float[4]  (linear RGBA)
  Intensity         : float
  Range             : float
  AttenuationConst  : float
  AttenuationLinear : float
  AttenuationQuad   : float
  SpotInnerAngle    : float     (radians)
  SpotOuterAngle    : float     (radians)
```

### `CAMR` — Camera Definitions

```
CameraCount : uint32
Per camera:
  NameLen     : uint32
  Name        : char[NameLen]
  NearClip    : float
  FarClip     : float
  FovAngle    : float    (radians, vertical FOV)
  AspectRatio : float
```

### `ANIM` — Animation Clips

One clip per FBX AnimationStack / Blender Action. Keyframes are TRS-sampled and stored
in Canvas row-vector Z-up space (same convention as CanvasFbx output). NodeIndex values
reference entries in the `NODE` chunk.

Future chunk versions may add quaternion compression and keyframe deduplication without
breaking existing loaders.

```
ClipCount : uint32
Per clip:
  NameLen         : uint32
  Name            : char[NameLen]
  DurationSeconds : float
  TrackCount      : uint32
  Per track:
    NodeIndex     : int32
    KeyframeCount : uint32
    Per keyframe:
      Time        : float
      Translation : float[4]    (W = 0)
      Rotation    : float[4]    (xyzw unit quaternion)
      Scale       : float[4]    (W = 0)
```

---

## Build Manifest (JSON)

`.cpkg` files are produced by a bake step driven by a JSON manifest. The tool that performs
this step is called `CanvasBake`. Manifests are plain JSON files, conventionally named
`<scene>.cpkg.json`.

### Path resolution rule

Paths in the manifest may be relative or absolute:

- **Relative paths** are resolved relative to the directory containing the manifest file.
  There is no required project layout or `assets/` root convention. Paths may traverse
  upward (`../../shared/textures`) freely.
- **Absolute paths** are used as-is.

`CanvasBake` resolves all paths to absolute paths before processing. Paths written into
the TXTR chunk are then re-expressed relative to the output `.cpkg` file's directory, so
the `.cpkg` and its external textures can be relocated together without editing the package.

### Example

```json
{
  "name": "ForestScene",
  "output": "../build/ForestScene.cpkg",
  "sources": [
    {
      "type": "fbx",
      "path": "scene/ForestScene.fbx",
      "textureSearchPaths": [
        "textures/materials",
        "../../shared/textures"
      ]
    }
  ],
  "textures": [
    { "name": "sky_px", "path": "textures/sky/px.hdr", "embed": true },
    { "name": "sky_nx", "path": "textures/sky/nx.hdr", "embed": true },
    { "name": "sky_py", "path": "textures/sky/py.hdr", "embed": true },
    { "name": "sky_ny", "path": "textures/sky/ny.hdr", "embed": true },
    { "name": "sky_pz", "path": "textures/sky/pz.hdr", "embed": true },
    { "name": "sky_nz", "path": "textures/sky/nz.hdr", "embed": true }
  ],
  "options": {
    "defaultEmbed": false
  }
}
```

### Fields

**Top level**
- `name` — display name for the package (stored in header reserved space in a future version)
- `output` — path of the `.cpkg` to write; relative to the manifest directory
- `options.defaultEmbed` — default embed behaviour for any texture that omits the `embed`
  field; `false` means store an external path reference in TXTR

**`sources[]`**
- `type` — `"fbx"` is the only supported source type in v1; `"cpkg"` is reserved for v2
- `path` — path to the source file; relative to the manifest directory
- `textureSearchPaths` — ordered list of directories (each relative to the manifest) to
  search when an FBX-embedded texture path cannot be resolved on the current machine;
  searched in order, first match wins; filename component only is matched

**`textures[]`** — explicitly declared image assets; processed before FBX-sourced textures;
deduplicated against FBX-sourced entries by resolved absolute path
- `name` — runtime lookup key stored in the TXTR `Name` field; must be unique within the manifest
- `path` — path to the image file; relative to the manifest directory
- `embed` — `true`: decoded image data is stored in the texture's `Bytes` payload (as
  per-subresource slices); `false` (or omitted): only the `.cpkg`-relative path is stored;
  falls back to `options.defaultEmbed`

**Options**
- `options.compress` — reserved; always `false` in v1 (chunk `Flags` bit 0 is never set)

### Bake-time texture resolution order

For each texture reference encountered during bake (manifest-explicit or FBX-sourced):

1. Resolve the path to an absolute path (manifest-relative for manifest textures;
   FBX-embedded absolute path for FBX textures, falling back through `textureSearchPaths`)
2. Check the deduplicated texture table — if an entry with the same resolved absolute path
   already exists, reuse its TXTR index
3. Otherwise add a new TXTR entry; store `Name` from the manifest declaration (empty for
   unnamed FBX textures); store `Path` as a path relative to the output `.cpkg` directory

---

## Package intermediate — `CanvasPackageData.h`

`CanvasPackage` defines the CPU-side data structure for a packaged scene:
`Canvas::PackageData`. It is declared in `src/CanvasPackage/Inc/CanvasPackageData.h` — the
library's single public header — a pure-data header with no dependency on `CanvasCore.h` or
any GPU type, just `Gem.hpp`, `CanvasMath.hpp`, `CanvasTypes.h`, and standard library
headers. (`CanvasFbx` currently produces its own `Fbx::ImportedScene`; folding that into
`PackageData` so both load paths share one builder is planned work — see Session 10.)

```cpp
// src/CanvasPackage/Inc/CanvasPackageData.h  (abbreviated)
namespace Canvas {

// Severity passed to the optional package I/O logging callback.
enum class PackageLogLevel : uint8_t { Info, Warning, Error };
// Receives a printf-style format string + va_list (not a finished string) so the sink defers
// composition until after its own level filter -- forward straight to QLog::Logger::Log.
using PackageLogFn = std::function<void(PackageLogLevel level, const char* format, va_list args)>;

struct PackageSubresource { uint64_t Offset; uint32_t Size, RowPitch; };
struct PackageTexture  { std::string Name; std::string Path; GfxFormat Format;
                         GfxSurfaceDimension Dimension;
                         uint32_t Width, Height, Depth, ArraySize, MipCount;
                         std::vector<PackageSubresource> Subresources;
                         std::vector<uint8_t> Bytes; };
struct PackageMaterial { /* PBR factors + 6 texture indices */ };
struct PackageMeshPart { int32_t MaterialIndex; std::vector<Math::FloatVector4> Positions,
                         Normals, Tangents; std::vector<Math::FloatVector2> UV0;
                         std::vector<PackageSkinVertex> SkinVertices; };
struct PackageMesh     { std::string Name; std::vector<PackageMeshPart> Parts;
                         Math::AABB Bounds; PackageSkin Skin; };
struct PackageLight    { /* type, color, intensity, range, attenuation, spot angles */ };
struct PackageCamera   { /* near, far, fov, aspect */ };
struct PackageNode     { std::string Name; int32_t ParentIndex;
                         Math::FloatVector4 Translation, Scale;
                         Math::FloatQuaternion Rotation;
                         int32_t MeshIndex, LightIndex, CameraIndex; };
struct PackageAnimClip { std::string Name; float DurationSeconds;
                         std::vector<PackageAnimTrack> Tracks; };

struct PackageData {
    std::vector<PackageMesh>     Meshes;
    std::vector<PackageLight>    Lights;
    std::vector<PackageCamera>   Cameras;
    std::vector<PackageMaterial> Materials;
    std::vector<PackageTexture>  Textures;
    std::vector<PackageNode>     Nodes;
    std::vector<PackageAnimClip> AnimClips;
    Math::AABB                   Bounds;
    int32_t                      ActiveCameraNodeIndex = -1;

    // .cpkg is PackageData's native on-disk form; read/write are members.
    Gem::Result ReadPackage(const wchar_t* pFilePath,    const PackageLogFn& logFn = {});
    Gem::Result WritePackage(const wchar_t* pOutputPath, const PackageLogFn& logFn = {}) const;
};

} // namespace Canvas
```

A `PackageData` is written to a `.cpkg` file via its own `WritePackage` member, which returns
`Gem::Result`. There are no free I/O functions and no separate `CanvasPackage.h`. Per-chunk
warnings are delivered to the optional `PackageLogFn` callback rather than stored on the struct
— the caller forwards them to QLog / `Canvas::XLogger` as it sees fit, which is what keeps the
header free of any `CanvasCore.h` dependency.

There are **two read paths**, because a `.cpkg` may be multiple gigabytes and must not be loaded
whole into CPU memory:

- `PackageData::ReadPackage` is the *convenience full-load* — it materializes the entire scene
  (including bulk geometry and texture bytes) into `PackageData`. Use it for small / medium
  scenes; it is also what tooling and the bake path want.
- The *streaming path* loads only metadata and bulk **descriptors** (byte ranges) through a
  seekable `CCpkgSource`, then streams vertex streams and texture subresources straight to the
  GPU on demand. This is the path for large packages. See the implementation plan's "Runtime
  loading model" for the `CCpkgSource` / `CpkgDocument` / `BuildModel` design.

## `Canvas::BuildModel` (`CanvasCore.h`)

Turning a `PackageData` into a GPU-resident `XModel` is the job of a common
`Canvas::BuildModel` in `CanvasCore`, shared between the FBX and `.cpkg` load paths. This is
planned work (Session 10); the intended shape is:

```cpp
// Callback for platform-specific image decode and GPU surface upload.
using PackageTextureLoadFn = std::function<bool(
    XGfxDevice*                   pDevice,
    const Canvas::PackageTexture& tex,
    XGfxSurface**                 ppSurface)>;

struct BuildModelOptions {
    PackageTextureLoadFn  TextureLoader;          // optional; null -> textures not loaded
    const char*           pModelName  = nullptr;
    QLog::Logger*         pLogger     = nullptr;  // no XLogger dependency
};

// Convert a PackageData into a fully populated XModel (GPU resources allocated).
// Called identically whether the PackageData came from FBX or .cpkg.
CANVAS_API Gem::Result BuildModel(
    XCanvas*                   pCanvas,
    XGfxDevice*                pDevice,
    const Canvas::PackageData& scene,
    const BuildModelOptions&   options,
    XModel**                   ppModel);
```

---

## Implementation Roadmap

See `IMPLEMENTATION_PLAN.md` for the session-by-session breakdown with per-session
"done when" checklists. High-level phases:

1. **`CanvasPackageData.h` + library skeleton** — `Canvas::PackageData` in
   `src/CanvasPackage/Inc/`; `CanvasPackage` project files + single public header (no
   `CanvasCore.h` dep)
2. **Binary I/O primitives** — `CCpkgReader` (cursor over a byte block), `CCpkgSink` (buffered
   streaming file writer), `CCpkgSource` (seekable streaming file reader); typed read/write +
   alignment
3. **Header and chunk-table I/O** — CRC32, streaming write with chunk-table back-patch, validation
   on read
4. **NODE + MESH chunk serializers** — with 16-byte vertex stream alignment
5. **MATL + TXTR + LITE + CAMR chunk serializers** — including TXTR format metadata
6. **ANIM chunk serializer**
7. **Bake infrastructure** — `SerializeScene` (FBX data -> `PackageData`) + `WritePackage`
8. **`CanvasBake` executable** — CLI bake tool
9. **`ReadPackage` runtime loader**
10. **`Canvas::BuildModel` in `CanvasCore`** — common scene builder; refactor `CanvasFbx`
    to produce `PackageData` and remove `Fbx::BuildModel`
11. **`CanvasModelViewer --pkg` integration**

---

## Deferred / Out of Scope (v1)

Items explicitly deferred and the reason:

| Item | Reason deferred |
|------|-----------------|
| Compression (chunk Flags bit 0) | Field is reserved; all chunks stored raw in v1 |
| ChunkGUID population | Field is reserved at 16 zeroed bytes; full GUID generation is v2+ |
| `DEPS` chunk | See schema sketch below; forward-compatibility slot defined |
| `FBX Source` chunk | Embedding the raw FBX is unnecessary for runtime loading |
| `Script` / `Script Trigger` | No scripting system |
| `Sound` / `Music` | No audio system |
| `NavMesh` | No navigation system |
| `Open World Cell` | Streaming is v2+ |
| `Static Lightmap` | No lightmap baking pipeline |
| BCn / mip generation at bake time | TXTR *format* already carries GPU-format mip subresources; the v1 bake tool just doesn't compress or generate mips yet (it stores encoded source images) |
| Progressive / async mip streaming (runtime) | The v1 runtime already reads through a seekable `CCpkgSource` and streams bulk vertex / texture-subresource byte ranges on demand (no whole-file load), so the format and loader are range-streamable. What is deferred to v2 is *asynchronous, partial-residency* streaming — background prefetch, coarsest-mip-first progressive upload, and eviction — which the per-subresource offset table is designed to support |
| Animation keyframe compression | ANIM stores raw float32 TRS; compression is v2+ |

### `DEPS` — Package Dependency List (deferred, FourCC reserved)

Define the FourCC now so v2 is not constrained. A `DEPS` chunk lists other `.cpkg` files
this package depends on, enabling shared skeleton libraries, shared material libraries, and
open-world cell streaming. v1 loaders skip it via the unknown-FourCC rule.

```
DependencyCount : uint32
Per dependency:
  PathLen      : uint32
  Path         : char[PathLen]   (relative to this .cpkg file)
  ChunkRefCount: uint32          (0 = depend on whole package)
  Per chunk ref:
    ChunkGUID  : uint8[16]       (GUID of required chunk in the dependency)
```
