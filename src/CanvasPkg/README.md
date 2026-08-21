# CanvasPkg

A CanvasPkg (Cpkg) is a package file containing world data - including models, geometry, materials, textures, lights, cameras, animations, (and eventually scripts, sounds, and other world data). The file is organized into bundles which contain related assets. Bundle assets can interrelate by asset index within a bundle. For example, a bundle might contain model data with object/bone hierarchy, meshes, materials, textures, animation clips. Other bundles might contain sky-box texture, scripts, music or sound effects.

API Success/failure codes use type `Gem::Result`.

`CCpkgReader` is used by Canvas itself to build world instances and set up scripts at load time. `CCpkgWriter` is used by `cpkgbuilder`, a separate command-line tool that cooks a `.cpkg` file from a JSON manifest (see [cpkgbuilder](../cpkgbuilder/README.md)).

## Dependencies

`CanvasPkg.h` includes `CanvasCore.h` and `CanvasGfx.h`. The dependency is header-only and CanvasPkg links no Canvas library, so `cpkgbuilder` can include it without linking the engine.

Engine enumerations are used by name in the record declarations below. Public Canvas enumerations declare an explicit base type; an enumeration field is stored as its base type, holding the enumerator's numeric value, and occupies that many bytes on disk.

Engine vector, quaternion, matrix, and bounds types are laid out for AVX operations, not for storage. The `Cpkg*` records declare raw `float` arrays instead, sized to the component count each quantity has (see [Geometric quantities](#geometric-quantities)).

## On-Disk Encoding

This policy governs every structure written to a `.cpkg` file - the file and bundle headers, the bundle and asset tables, the packed names, the [variable-length blobs](#variable-length-fields), and every asset payload.

All multi-byte fields are little-endian. Every on-disk field is one of the primitives below. Fields are stored back-to-back with no padding, so a fixed-size record's on-disk stride is the sum of its fields' widths, and each field's offset is the sum of the widths preceding it.

### On-disk primitives

| Notation | Size | Encoding |
|---|---|---|
| `f32` | 4 | IEEE-754 binary32, little-endian |
| `i32` | 4 | two's-complement signed, little-endian |
| `u32` | 4 | unsigned, little-endian |
| `u64` | 8 | unsigned, little-endian |
| `T[n]` | `n * sizeof(T)` | `n` elements of `T`, back-to-back, no padding |
| `CpkgBlob` | `8 + length` | `u64` byte length, then that many payload bytes, stored inline (see [Variable-Length Fields](#variable-length-fields)) |
| packed name | `4 + length` | `u32` byte length, then that many UTF-8 bytes, **not** null-terminated |

The packed-name form is used only by bundle and asset *records* (see [Assets](#assets)). A name inside an asset payload is a `CpkgBlob`, like any other variable-length field.

### Geometric quantities

This table fixes the component count and order for each kind of geometric quantity, which in turn fixes the stride of any stream of them:

| Quantity | On-disk | Component order |
|---|---|---|
| Position | `f32[3]` | `x`, `y`, `z` |
| Normal, translation, scale | `f32[3]` | `x`, `y`, `z` |
| Texture coordinate | `f32[2]` | `u`, `v` |
| Tangent | `f32[4]` | `x`, `y`, `z` = tangent, `w` = bitangent sign |
| Rotation | `f32[4]` | `x`, `y`, `z`, `w` - unit quaternion, Canvas space |
| Color | `f32[4]` | `r`, `g`, `b`, `a` |
| PBR factor | `f32[4]` | as named by the member (`RoughMetalAOFactor` is roughness, metallic, ambient occlusion, spare) |
| Bounding box (`CpkgAABB`) | `f32[6]` | `Min[3]` then `Max[3]` |
| Keyframe (`CpkgKeyframe`) | `f32[11]` | `Time`, `Translation[3]`, `Rotation[4]`, `Scale[3]` |
| Matrix | `f32[16]` | row-major (Canvas row-vector convention: `v' = v * M`, translation in row 3) |
| Bone influence indices | `u32[4]` | |
| Bone influence weights | `f32[4]` | |

## Assets

Assets are the primary data block for CanvasPkg files. Each asset has a name, an `AssetType`, and a payload of bytes whose internal layout is defined by that `AssetType` (see [Package Data Types](#package-data-types)). An asset is stored on disk as a *record*: its packed name first (a `uint32_t` byte length, followed by that many UTF-8 name bytes - no imposed maximum length), immediately followed by the payload bytes. A record is self-describing when read forwards from its own start. The bundle's asset table (see [Bundles](#bundles)) is a flat, fixed-stride array, indexed by asset index, holding the type, record offset, and compression info of every asset in the bundle. Names are not in the table; obtaining one costs a read at the record's packed name.

Asset indices are per-bundle and span all asset types: the first asset appended to a bundle is index 0, the second is index 1, and so on, in append order, regardless of `AssetType`. `CCpkgReader::ReadAsset` and the `Cpkg*Data` cross-reference fields described below (`MaterialIndex`, `TextureIndex`, `MeshDataIndex`, `ParentIndex`, ...) all use this index.

**An asset may only reference assets appended before it.** Every intra-bundle cross-reference index (`MeshDataIndex`, `MaterialIndex`, `ParentIndex`, `TextureIndex`, `MeshNodeIndex`, `BoneNodeIndices`, ...) must be strictly less than the index of the asset holding it. Forward references are not part of the format, so a loader resolves every reference in a single forward pass over the asset table, with no deferred fixups and no second pass.

The reference graph this schema defines is acyclic, so an append order satisfying the rule always exists:

```
Image <- Material <- MeshData <- SceneNode <- MeshSkin
                                  ^     ^
                           Camera-+     +-Light
```

`AnimationClip` binds tracks by node name and holds no indices, so it may be appended anywhere. The canonical order is: `Image`, `Material`, `MeshData`, `Camera`, `Light`, `SceneNode`, `MeshSkin`, `AnimationClip`. Within `SceneNode`, appending in pre-order satisfies the rule for `ParentIndex` and guarantees parent-before-child; a loader may rely on a node's parent already being resolved when the node itself is read.

`AppendAsset` takes an opaque payload and does not interpret indices out of it, so the writer cannot enforce this rule. It is a contract on whoever produces the payloads - `cpkgbuilder` orders its appends to satisfy it - and a package that violates it is malformed. A reader may treat an out-of-order reference as a corrupt package (`Gem::Result::CorruptedData`).

Intra-bundle asset dependencies reference assets by this index. Cross-bundle dependencies reference bundle name + asset name. Cross-package dependencies use package name + bundle name + asset name. Resolving cross-bundle and cross-package references is a loader-level concern, built on top of `CCpkgReader`'s opt-in [name directories](#name-directories).

`CCpkgWriter::AppendAsset` requires an open bundle; call `BeginBundle` first, and `EndBundle` when the bundle is complete.

Asset names must be unique within a bundle. Bundle names must be unique within a package.

## Bundles

Bundles represent a related collection of assets such as mesh data, materials and textures. Bundles in a package must be uniquely named.

A package file starts with a fixed-size file header, and every bundle starts with a fixed-size bundle header.

```cpp
// First bytes of a .cpkg file.
struct CpkgFileHeader
{
    char     Magic[4];           // "CPKG"; CCpkgReader::Open fails if this doesn't match
    uint32_t Version;            // format version; CCpkgReader::Open fails if outside [kMinSupportedVersion, kMaxSupportedVersion]
    uint64_t BundleTableOffset;  // file offset of the file-level bundle table
    uint32_t BundleCount;        // number of entries at BundleTableOffset
};

// A bundle is stored as a record: its packed name first, immediately followed
// by this header. CpkgBundleTableEntry::RecordOffset points at the record's
// start - i.e. at the packed name - so the name is read forwards before the header.
struct CpkgBundleHeader
{
    uint64_t AssetTableOffset;   // file offset of this bundle's asset table
    uint32_t AssetCount;         // number of entries at AssetTableOffset
};
```

Both tables are flat, fixed-stride arrays containing no names, so an index resolves to an offset by multiplication. Their entry counts come from the file and bundle header fields above:

```cpp
// One entry per bundle in the file-level bundle table.
struct CpkgBundleTableEntry
{
    uint64_t RecordOffset;   // file offset of this bundle's record (its packed name, followed by CpkgBundleHeader)
};

// One entry per asset in a bundle's asset table.
struct CpkgAssetTableEntry
{
    uint64_t               RecordOffset;  // file offset of this asset's record (its packed name, followed by the payload)
    AssetType              Type;          // enum class : uint32_t
    CpkgCompressionScheme  Compression;   // see below; all fixed-width fields
};
```

- **File-level bundle table**: `CpkgFileHeader::BundleCount` entries of `CpkgBundleTableEntry`, starting at `CpkgFileHeader::BundleTableOffset`.
- **Per-bundle asset table**: `CpkgBundleHeader::AssetCount` entries of `CpkgAssetTableEntry`, starting at `CpkgBundleHeader::AssetTableOffset`.

A bundle's `RecordOffset` and an asset's `RecordOffset` both point at the start of a record, which begins with the packed name (UTF-8, length-prefixed the same way for both - see [Assets](#assets)): a bundle's name is followed by its `CpkgBundleHeader`, an asset's name by its payload. What follows the name begins at `RecordOffset + 4 + nameLength`. Fetching a name by index, or searching by name, means seeking to `RecordOffset` and reading the name forwards.

### File Layout and Fixups

Both tables are variable-length and grow as the writer appends, so neither has a fixed, pre-reserved location. The writer accumulates each table in memory and serializes it once, when the thing it describes is sealed.

`Open` writes `CpkgFileHeader::Magic`/`Version` immediately, leaving `BundleTableOffset`/`BundleCount` as placeholders. `EndBundle` seals the open bundle: it writes that bundle's accumulated asset table at the current end of file, then seeks back and overwrites `AssetTableOffset` and `AssetCount` in the bundle's header to point at it. `Close` does the same for the file-level bundle table and the file header.

Every write is therefore either an append (an asset payload, a sealed bundle's asset table, the file-level bundle table) or a fixup of an already-allocated header field. No previously written asset or table is moved or rewritten.


### Staged Writes

`CCpkgWriter::Open`'s `append` parameter selects what happens when `fileName` already exists:

| `fileName` | `append` | Behavior |
|---|---|---|
| does not exist | either | Creates a new package. `append` is ignored. |
| exists | `true` | Amends the existing package, as described below. `Gem::Result::CorruptedData` if the file is not a readable package. |
| exists | `false` | `Gem::Result::AlreadyExists`. The existing file is left untouched, and the caller decides whether to delete it, rename it, or pick another path. |

**A writer never writes to `fileName`.** Every write - to a new package or an amend - goes to a temporary scratch file, and `fileName` is touched only once, by an atomic rename at the end of a successful `Close()`. Until that step, `fileName` is exactly what it was before `Open`: absent for a new package, byte-for-byte the original for an amend. A crash, a power loss, or a [writer failure](#writer-failure-closes-the-writer) at any point therefore never leaves a partial or corrupt package under `fileName`. This is the standard write-new-then-rename discipline; for an amend it costs one full copy of the package, which is acceptable for a build-time cooking tool.

The sequence is:

1. **`Open`** creates the temporary file in the *same directory* as `fileName` - the rename in step 3 is only atomic within a volume - with a name derived from `fileName` plus a unique suffix (for example `world.cpkg.a1b2c3.tmp`).
   - **New package**: writes `CpkgFileHeader` with placeholder `BundleTableOffset`/`BundleCount` (see [File Layout and Fixups](#file-layout-and-fixups)).
   - **Amend**: copies the existing package's bytes into the temporary file, reads `CpkgFileHeader` to find `BundleTableOffset`, loads the existing `CpkgBundleTableEntry` array and every existing bundle's name into memory (seeding the in-memory bundle list and duplicate-name check), and treats `BundleTableOffset` as the resume point for writing in the temporary file.
2. **`BeginBundle`/`AppendAsset`/`EndBundle`** write bundle headers, payloads, and asset tables into the temporary file. On an amend, writing starts at the resume offset, overwriting the copy's file-level bundle table; that table was the tail of a closed file, so nothing is orphaned, and the live copy of it is the one held in memory.
3. **`Close`** writes the complete bundle table (for an amend, existing + new) at the end of the temporary file, fixes up its header, flushes the file's contents to stable storage (`FlushFileBuffers`, `fsync`), closes it, and only then atomically renames it to `fileName` (`ReplaceFileW` or `MoveFileExW` with `MOVEFILE_REPLACE_EXISTING`; `rename(2)` elsewhere). Flushing before the rename is required: without it a crash just after the rename can publish a file name pointing at unwritten data.

If any step fails, the writer closes and deletes the temporary file (see [Writer failure closes the writer](#writer-failure-closes-the-writer)). The same cleanup runs if the `CCpkgWriter` is destroyed while still open. A temporary file can still be left behind by a hard crash; it is inert (it holds the `.tmp` suffix, not the package's name) and callers may sweep stale ones.

Amending only ever adds whole new bundles. An existing bundle is immutable once `EndBundle` has sealed it; there is no operation to reopen one and append more assets to it.

## Package Data Types

`CCpkgWriter`/`CCpkgReader` move opaque, named, typed byte blobs. This section defines what those bytes mean for each `AssetType`.

**The structures in this section are the on-disk records.** Each is declared in serialized order using only [on-disk primitives](#on-disk-primitives), so the declaration is the byte layout: members are stored back-to-back with no padding, a record of fixed-width members occupies the sum of their widths, and each member's offset is the sum of the widths above it. A variable-length array appears as a `CpkgBlob` member (see [Variable-Length Fields](#variable-length-fields)), a byte length followed inline by the bytes themselves.

Relationships between assets - a node's parent, the mesh a node instances, the material a mesh group draws with - are stored as the intra-bundle asset index of the referenced asset (see [Assets](#assets)). Records correspond to the descriptors in `CanvasCore.h`/`CanvasGfx.h`: `MeshDataDesc`, `AnimationClipDesc`, `MeshSkinDesc`, and so on.

### AssetType

```cpp
enum class AssetType : uint32_t
{
    Unknown       = 0,   // sentinel; never assigned to a real asset
    SceneNode     = 1,
    MeshData      = 2,
    Material      = 3,
    Image         = 4,
    AnimationClip = 5,
    MeshSkin      = 6,
    Camera        = 7,
    Light         = 8,
};
```

This covers everything `XModel` round-trips except its model-level active-camera and bounds designation (see [Future Directions](#future-directions)).

### Compression

Compression is applied by the API, never by the caller. `AppendAsset` compresses a payload per the requested codec before writing it to disk, and `ReadAsset` decompresses before handing bytes back, so a caller's buffer holds raw, uncompressed bytes in both directions.

```cpp
enum class CpkgCodec : uint32_t
{
    None = 0,
    // Byte compressors are added here once a codec dependency is chosen.
    // v1 implements only None: every asset is stored uncompressed and
    // StoredSize == RawSize.
};

// Stored in the on-disk CpkgAssetTableEntry (see Bundles).
struct CpkgCompressionScheme
{
    CpkgCodec Codec      = CpkgCodec::None;  // codec the payload is stored with
    uint64_t  StoredSize = 0;   // on-disk byte count
    uint64_t  RawSize    = 0;   // uncompressed byte count
};
```

A reader obtains an asset's scheme from `GetAssetCompression`, whose `RawSize` is the buffer size `ReadAsset` requires.

`CpkgCompressionScheme` is an on-disk record and a reader-side return value only. `AppendAsset` takes the codec and the raw byte count as parameters and computes `StoredSize` itself; there is no caller-populated descriptor struct on either the write or the read path.

### Variable-Length Fields

A variable-length array is stored inline as a **blob**: a `u64` byte length, immediately followed by that many bytes of tightly packed elements. The record structures below spell a blob member as `CpkgBlob`:

```cpp
// Notation only. A CpkgBlob member is a uint64_t byte length followed by
// that many bytes, stored inline at this position in the record.
//
// A member is written as CpkgBlob<T> when the bytes are an array of
// fixed-stride T, and as CpkgBlob<> when they are a sequence of
// variable-size records or opaque bytes.
struct CpkgBlob;
```

The length prefix lets a reader skip a blob without interpreting its contents. A record's total size is the sum of its fixed-width members plus `8 + length` for each blob.

A blob carries no element count; the count is recovered from the length:

- For `CpkgBlob<T>`, the element count is `length / sizeof(T)`, where `sizeof(T)` is the on-disk stride of `T` per [On-Disk Encoding](#on-disk-encoding).
- For a `CpkgBlob<>` holding variable-size records (`Groups`, `Tracks`), a reader parses records one at a time - each is self-describing via its own nested blobs - until it has consumed exactly the outer blob's declared length, counting them as it goes.

Every blob member is always present in the byte stream, so a record always contains exactly the blobs its declaration lists, in that order. **An absent optional blob is written with a length of `0` and no bytes.** Zero length means absent for every optional stream in this format.

A blob's length must be an exact multiple of its element stride, and parallel streams (the per-vertex streams against `Positions`, `InvBindPoses` against `BoneNodeIndices`) must imply the same element count. A reader may treat a violation of either rule as a corrupt package (`Gem::Result::CorruptedData`).

### CpkgSceneNodeData (AssetType::SceneNode)

One node in a model's node/bone hierarchy. A bundle's `SceneNode` assets together describe the whole hierarchy; `ParentIndex` links a node to its parent by asset index.

Fixed size, 56 bytes.

```cpp
struct CpkgSceneNodeData
{
    int32_t   ParentIndex        = -1;  // asset index of the parent SceneNode; -1 = root
    float     LocalTranslation[3];
    float     LocalRotation[4];         // unit quaternion, XYZW, Canvas space
    float     LocalScale[3];
    int32_t   MeshDataIndex      = -1;  // asset index of a MeshData this node instances; -1 = none
    int32_t   CameraIndex        = -1;  // asset index of a Camera this node instances; -1 = none
    int32_t   LightIndex         = -1;  // asset index of a Light this node instances; -1 = none
};
```

The node's name comes from the `name` it was appended with, matched against cloned node names the same way `AnimationTrackDesc::NodeName` is today.

### CpkgMeshData (AssetType::MeshData)

Corresponds to `MeshDataDesc`/`MeshDataGroupDesc`. Each vertex stream is its own blob holding one element per vertex; the element types below fix each stream's component count and stride.

Variable size. The payload is one `CpkgMeshData`, whose `Groups` blob holds `CpkgMeshDataGroup` records back-to-back:

```cpp
struct CpkgVertexPosition { float XYZ[3];  };  // 12 bytes
struct CpkgVertexNormal   { float XYZ[3];  };  // 12 bytes
struct CpkgVertexUV       { float UV[2];   };  //  8 bytes
struct CpkgVertexTangent  { float XYZW[4]; };  // 16 bytes, xyz = T, w = bitangent sign
struct CpkgVertexBoneIndices { uint32_t Index[4]; };  // 16 bytes
struct CpkgVertexBoneWeights { float    Weight[4]; };  // 16 bytes

struct CpkgAABB
{
    float Min[3];
    float Max[3];
};

// One material partition of a mesh. The vertex count is the Positions
// blob length divided by sizeof(CpkgVertexPosition).
struct CpkgMeshDataGroup
{
    int32_t                         MaterialIndex = -1;  // asset index of a Material; -1 = none
    CpkgBlob<CpkgVertexPosition>    Positions;           // required by every topology
    CpkgBlob<CpkgVertexNormal>      Normals;             // required by every topology
    CpkgBlob<CpkgVertexUV>          UV0;                 // required by PatchList4CP, optional otherwise
    CpkgBlob<CpkgVertexTangent>     Tangents;            // optional; zero-length = absent
    CpkgBlob<CpkgVertexBoneIndices> BoneIndices;         // optional; palette-relative, see below
    CpkgBlob<CpkgVertexBoneWeights> BoneWeights;         // optional; zero-length = absent
};

struct CpkgMeshData
{
    GfxPrimitiveTopology  Topology;      // CanvasGfx.h
    CpkgAABB              LocalBounds;   // Min > Max (empty) = auto-compute from positions, as in MeshDataDesc
    CpkgBlob<>            Groups;        // CpkgMeshDataGroup records, parsed until the blob length is consumed
};
```

Every present stream's length divided by its element size must yield the same vertex count as `Positions`.

**Which streams are required depends on `Topology`**, matching what the backend enforces in `CreateMeshData`: `TriangleList` requires `Positions` and `Normals`; `PatchList4CP` requires `Positions`, `Normals`, and `UV0` (per-control-point map UVs, and the base direction along which displacement extends each control point). `Tangents`, `BoneIndices`, and `BoneWeights` are optional under either topology.

A skinned mesh, its `MeshSkin` asset, and every bone it references must all live in the same bundle. `BoneIndices` is the one cross-reference field in this schema that is not a bundle asset index: it is a palette-relative index into the mesh's `CpkgMeshSkinData::BoneNodeIndices` array, and by the ordering correspondence into `InvBindPoses`. This matches the runtime skinning convention. `CpkgMeshSkinData` is the single place bones are referenced by asset index. An unused influence is encoded as weight 0 in `BoneWeights`, with its `BoneIndices` slot ignored; there is no reserved "none" bone-index sentinel.

### CpkgMaterialData (AssetType::Material)

Covers the material properties `XGfxMaterial` exposes today: texture-role bindings, the three PBR factors, and the optional displacement extension.

Fixed size, 104 bytes.

```cpp
struct CpkgDisplacementData
{
    int32_t DisplacementMapTextureIndex = -1;  // asset index of an Image; required if present
    float   MapScale          = 1.0f;
    float   MapBias           = 0.0f;
    float   MinTessFactor     = 2.0f;
    float   MaxTessFactor     = 32.0f;
    float   DistanceLodScale  = 10.0f;
    float   CurvatureLodScale = 0.5f;
};

struct CpkgMaterialData
{
    float  BaseColorFactor[4];
    float  EmissiveFactor[4];
    float  RoughMetalAOFactor[4];   // R=Roughness, G=Metallic, B=AmbientOcclusion, A=spare

    int32_t AlbedoTextureIndex           = -1;  // asset index of an Image; -1 = none, one per MaterialLayerRole
    int32_t NormalTextureIndex           = -1;
    int32_t RoughnessTextureIndex        = -1;
    int32_t MetallicTextureIndex         = -1;
    int32_t AmbientOcclusionTextureIndex = -1;
    int32_t EmissiveTextureIndex         = -1;

    uint32_t               HasDisplacement = 0;  // 0 or 1
    CpkgDisplacementData   Displacement;         // meaningful only when HasDisplacement is 1
};
```

The `Displacement` block is always written, even when `HasDisplacement` is `0`, so every `CpkgMaterialData` record is the same size. A reader ignores its contents when `HasDisplacement` is `0`.

### CpkgImageData (AssetType::Image)

An image asset holds encoded image bytes verbatim, exactly as read from the source. `Encoding` identifies how they are encoded. CanvasPkg neither decodes nor re-encodes the bytes; decoding happens downstream, at load time.

```cpp
enum class CpkgImageEncoding : uint32_t
{
    Unknown = 0,
    Jpeg    = 1,
    Png     = 2,
    Tiff    = 3,
    // Additional encodings (block-compressed formats such as DXT/BCn,
    // container formats such as DDS/KTX) are added here.
};

struct CpkgImageData
{
    CpkgImageEncoding  Encoding;
    CpkgBlob<>         ImageData;  // encoded bytes, exactly as read from the source
};
```

The image's byte count is the `ImageData` blob's length.

`Encoding` and `Compression.Codec` describe separate layers. `Encoding` describes the payload itself and is opaque to CanvasPkg, which stores and returns those bytes untouched. `Compression.Codec` is transport compression applied over the payload, and `ReadAsset` always reverses it before returning (see [Compression](#compression)). An encoding that already compresses its bytes gains little from a second pass, so such an asset should be appended with `CpkgCodec::None`.

Tiled/decoded GPU-ready texture support (mip chains, array slices, GPU tile layout) is a future design pass - see [Future Directions](#future-directions).

### CpkgAnimClipData (AssetType::AnimationClip)

Corresponds to `AnimationClipDesc`/`AnimationTrackDesc`. Tracks target nodes **by name**, as `AnimationTrackDesc` does, so a clip binds to any model or instance whose node names match, independent of that hierarchy's index layout.

Variable size. The payload is one `CpkgAnimClipData`, whose `Tracks` blob holds `CpkgAnimTrack` records back-to-back:

```cpp
struct CpkgKeyframe                // 44 bytes
{
    float Time;                    // seconds from clip start
    float Translation[3];
    float Rotation[4];             // unit quaternion, XYZW
    float Scale[3];
};

struct CpkgAnimTrack
{
    CpkgBlob<>              NodeName;    // UTF-8, not null-terminated; matched by name at bind time
    CpkgBlob<CpkgKeyframe>  Keyframes;   // sorted ascending by Time
};

struct CpkgAnimClipData
{
    float       Duration;   // seconds
    CpkgBlob<>  Tracks;     // CpkgAnimTrack records, parsed until the blob length is consumed
};
```

### CpkgMeshSkinData (AssetType::MeshSkin)

Corresponds to `MeshSkinDesc`. A skin binding is tied to one model's node topology, so bones are referenced by asset index.

Variable size:

```cpp
// Row-major, Canvas row-vector convention: v' = v * M, translation in row 3.
struct CpkgInvBindPose { float M[16]; };  // 64 bytes

struct CpkgMeshSkinData
{
    int32_t                     MeshNodeIndex = -1;  // asset index of the SceneNode carrying the skinned mesh
    CpkgBlob<int32_t>           BoneNodeIndices;     // asset indices of SceneNode bones; one per bone
    CpkgBlob<CpkgInvBindPose>   InvBindPoses;        // one per bone, same order as BoneNodeIndices
};
```

The bone count is the `BoneNodeIndices` blob length divided by 4; `InvBindPoses` must imply the same count.

### CpkgCameraData (AssetType::Camera)

Corresponds to `XCamera`'s settable state (`Camera.h`). Fixed size, 20 bytes.

```cpp
struct CpkgCameraData
{
    float NearClip      = 0.1f;
    float FarClip       = 1000.0f;
    float FovAngle      = 0.785398f;  // radians
    float AspectRatio   = 1.7778f;
    float ExposureStops = 0.0f;
};
```

### CpkgLightData (AssetType::Light)

Corresponds to `XLight`'s settable state (`Light.h`): light type, color/intensity, point/spot attenuation, spot cone angles, and shadow parameters. `Flags` is a `LightFlags` bitmask (`CastsShadows`, `Enabled`); the shadow fields take effect only when `Flags & LightFlags::CastsShadows` is set. Fixed size, 68 bytes.

```cpp
struct CpkgLightData
{
    LightType Type                 = LightType::Point;
    float     Color[4]             = { 1.0f, 1.0f, 1.0f, 1.0f };
    float     Intensity            = 1.0f;
    uint32_t  Flags                = LightFlags::Enabled;

    // Attenuation (Point and Spot lights)
    float     AttenuationConstant  = 1.0f;
    float     AttenuationLinear    = 0.0f;
    float     AttenuationQuadratic = 0.0f;
    float     Range                = 100.0f;

    // Spot light cone angles, in radians (only valid for Spot lights)
    float     SpotInnerAngle       = 0.785398f;
    float     SpotOuterAngle       = 1.047198f;

    // Shadow parameters (consumed by the backend when Flags & LightFlags::CastsShadows is set)
    uint32_t  ShadowResolution     = 0;      // 0 = backend default (2048)
    float     ShadowConstantBias   = 1e-4f;
    float     ShadowSlopeScaleBias = 2.0f;
    float     ShadowNormalOffset   = 0.5f;   // in shadow-map texels
};
```

The shadow fields are always written, even when `Flags & LightFlags::CastsShadows` is unset, so every record is a fixed size. The backend ignores them in that case.

## Error handling and result reporting

Every call that can fail returns `Gem::Result`. A call that produces an index writes it through an optional out parameter, which may be null and is written only on success:

```cpp
uint32_t assetIndex = 0;
Gem::Result r = writer.AppendAsset(AssetType::Material, "Brass", CpkgCodec::None, pData, dataSize, &assetIndex);
```

The codes this API reports:

| Code | Meaning |
|---|---|
| `Gem::Result::Success` | |
| `Gem::Result::InvalidArg` | Caller error: an out-of-range index, a duplicate bundle or asset name, a null required argument, a `ReadAsset` destination buffer smaller than the asset's `RawSize`, or a call with no bundle open |
| `Gem::Result::NotFound` | The named file does not exist |
| `Gem::Result::IoError` | A read, write, or seek on the package file failed, or the writer's temporary file could not be created, copied, flushed, or renamed to `fileName` |
| `Gem::Result::CorruptedData` | The file is not a package (`Magic` mismatch, or too short to hold a `CpkgFileHeader`), or its structure is damaged: an offset or length outside the file, a malformed blob, or an out-of-order reference |
| `Gem::Result::OutOfRange` | The package's `Version` is outside this build's supported range |
| `Gem::Result::AlreadyExists` | `CCpkgWriter::Open` was called with `append = false` on an existing file |
| `Gem::Result::OutOfMemory` | An allocation for an in-memory table, directory, or scratch buffer failed |
| `Gem::Result::NotOpen` | The call was made on a reader or writer that is not open: before a successful `Open`, after `Close`, or after a writer failure closed it |

Every `Gem::Result`-returning method of `CCpkgWriter` and `CCpkgReader` other than `Open` - reads, writes, and `Close` alike - returns `Gem::Result::NotOpen` when the object is not open, and does nothing else. The reader's value-returning accessors (`GetBundleCount`, `GetBundleName`, ...) cannot report a code; on an object that is not open they return the same value they return for an out-of-range index.

`CCpkgWriter` is a single-threaded, call-in-order API. `CCpkgReader` is too, except for `ReadAsset`, which is safe to call concurrently from multiple threads on the same instance (see [Decompression Scratch Buffer](#decompression-scratch-buffer)).

### Writer failure closes the writer

Every `CCpkgWriter` method validates its arguments and call order before modifying anything. A validation failure returns `Gem::Result::InvalidArg` and leaves the writer and its temporary file exactly as they were; the writer stays open and later valid calls proceed normally.

Any other failure - an `IoError` or `OutOfMemory` from `BeginBundle`, `AppendAsset`, `EndBundle`, or `Close`, including a failed flush or rename in `Close` - closes the writer. The failing call releases the file handle, deletes the temporary file, and returns the code describing the cause. `fileName` is left exactly as it was before `Open` (see [Staged Writes](#staged-writes)). The writer is then not open, so every later call, including `Close`, returns `Gem::Result::NotOpen`. The cause of a failure is reported once, by the call that failed; a later `NotOpen` means an earlier call already failed or the writer was never opened.

A reader failure does not close the reader. `ReadAsset` modifies no state, so an `IoError` or `CorruptedData` from one call leaves the reader open and other assets readable.

## CCpkgWriter

A CCpkgWriter class writes bundles and assets to a package. CanvasPkg bundle and asset writes are append-only with header fixups (see [File Layout and Fixups](#file-layout-and-fixups)).

### Methods

**CCpkgWriter::Open**

Opens a package for write. If `fileName` does not exist, `Open` creates a new package and ignores `append`. If it does exist, `append = true` amends it and `append = false` fails with `Gem::Result::AlreadyExists`, leaving the file untouched. `Open` never destroys or modifies an existing file: all writes are staged in a temporary file alongside `fileName` and renamed into place by a successful `Close`. See [Staged Writes](#staged-writes) for the full behavior table and the staging sequence.

`Gem::Result CCpkgWriter::Open(const char *fileName, bool append)`

**CCpkgWriter::Close**

Writes the file-level bundle table, fixes up the file header, flushes the temporary file, and atomically renames it to `fileName` (see [Staged Writes](#staged-writes)). Fails with `Gem::Result::InvalidArg` if a bundle is still open, leaving the writer open; call `EndBundle` first. Any other failure deletes the temporary file, leaves `fileName` as it was before `Open`, and closes the writer (see [Writer failure closes the writer](#writer-failure-closes-the-writer)). Returns `Gem::Result::NotOpen` if the writer is not open.

`Gem::Result CCpkgWriter::Close()`

**CCpkgWriter::BeginBundle**

Begins a new bundle scope and writes the new bundle's index to `pBundleIndex`, which may be null. Every `BeginBundle` must be matched by an `EndBundle`. Fails with `Gem::Result::InvalidArg` if a bundle is already open, or if `bundleName` duplicates a bundle already in the package.

`Gem::Result CCpkgWriter::BeginBundle(const char *bundleName, uint32_t *pBundleIndex = nullptr)`

**CCpkgWriter::EndBundle**

Seals the open bundle: writes its asset table and fixes up its header (see [File Layout and Fixups](#file-layout-and-fixups)). The bundle is immutable afterward; there is no operation to reopen one. Fails with `Gem::Result::InvalidArg` if no bundle is open.

`Gem::Result CCpkgWriter::EndBundle()`

**CCpkgWriter::AppendAsset**

Appends one asset to the open bundle and writes its index to `pAssetIndex`, which may be null. Assets are indexed in append order within the bundle, starting at zero and shared across all asset types, so a caller appends assets of different types in whatever order it produces them - see [Assets](#assets).

`name` is null-terminated and must be unique within the bundle. `pData` points at `dataSize` bytes of raw, uncompressed payload, and `codec` is the codec to store it with (`CpkgCodec::None` for no compression). `AppendAsset` compresses the payload and writes the result to the file before returning, retaining nothing beyond the in-memory bundle and asset tables, so a caller stages and appends one asset at a time while building a large model.

Fails with `Gem::Result::InvalidArg` if no bundle is open, if `name` or `pData` is null, or if `name` duplicates an asset already in the bundle. All of these are checked before anything is written; any failure after that closes the writer (see [Writer failure closes the writer](#writer-failure-closes-the-writer)).

`Gem::Result CCpkgWriter::AppendAsset(AssetType assetType, const char *name, CpkgCodec codec, const void *pData, uint64_t dataSize, uint32_t *pAssetIndex = nullptr)`

## CCpkgReader

CanvasPkg reads are random-access. The file-level bundle table provides bundle locations and each bundle's own asset table provides asset locations, types, and compression info without needing to read payload bytes.

### Name Directories

Name lookups are opt-in: a caller builds a directory once (e.g. a loader resolving cross-bundle references) and searches it directly:

```cpp
// Built by BuildBundleDirectory/BuildAssetDirectory. Owns its own copies of
// the names it indexes, so it stays valid even after the CCpkgReader that
// built it is closed.
class CCpkgNameDirectory
{
public:
    // Returns the index associated with name (a bundle index if this
    // directory came from BuildBundleDirectory, an asset index if it came
    // from BuildAssetDirectory), or UINT32_MAX if name is not present.
    uint32_t Find(const char *name) const;

private:
    std::unordered_map<std::string, uint32_t> m_Entries;
    friend class CCpkgReader;
};
```

**CCpkgReader::BuildBundleDirectory**

Scans every bundle's packed name once and fills `*pDirectory`, mapping bundle name to bundle index.

`Gem::Result CCpkgReader::BuildBundleDirectory(CCpkgNameDirectory *pDirectory)`

**CCpkgReader::BuildAssetDirectory**

Scans the packed names of assets in the bundle at `bundleIndex` once and fills `*pDirectory`, mapping asset name to asset index. When `typeFilter` is `AssetType::Unknown` (the default), every asset in the bundle is indexed; otherwise only assets of that `AssetType` are, which keeps the directory small for a caller that looks up one asset type by name.

`Gem::Result CCpkgReader::BuildAssetDirectory(uint32_t bundleIndex, CCpkgNameDirectory *pDirectory, AssetType typeFilter = AssetType::Unknown)`

### Decompression Scratch Buffer

A compressed asset is read into a scratch buffer and decompressed from there into the caller's `pData`. An asset stored with `CpkgCodec::None` skips the scratch entirely and is read straight into `pData`, so in v1 - where `None` is the only codec - this pool is never exercised. `CCpkgReader` owns a private pool of reusable buffers, so concurrent `ReadAsset` calls on one instance do not contend for the same memory. The buffer size and pool size are fixed constants, not caller-configurable:

```cpp
// Internal to CCpkgReader; not part of the public API.
constexpr size_t kScratchBufferCapacity = 64 * 1024;  // 64 KiB
constexpr size_t kScratchBufferPoolSize = 4;          // concurrent ReadAsset calls before spillover
```

A `ReadAsset` call on a compressed asset checks out a free buffer for the duration of the call and returns it afterward. Two situations spill over to a one-off allocation freed at the end of that call: an asset whose `StoredSize` exceeds `kScratchBufferCapacity`, and a call arriving when all `kScratchBufferPoolSize` buffers are checked out. The pool's steady-state footprint stays at `kScratchBufferPoolSize * kScratchBufferCapacity` in either case. Both constants are sized for whatever codec is eventually added; until then they have no effect on any read.

Concurrent `ReadAsset` calls also require the underlying file access to be positional (`pread`, or `ReadFile` with an explicit offset) so they do not race on a shared seek cursor. The remaining accessors are subject to the single-threaded, call-in-order contract described in [Error handling and result reporting](#error-handling-and-result-reporting).

### Methods

**CCpkgReader::Open**

Opens a package for read. Validates `CpkgFileHeader::Magic`, then `Version` against this build's supported range:

```cpp
constexpr uint32_t kMinSupportedVersion = 1;  // oldest CpkgFileHeader::Version this build can still read
constexpr uint32_t kMaxSupportedVersion = 1;  // current format version this build implements
```

A file whose `Version` falls outside `[kMinSupportedVersion, kMaxSupportedVersion]` is rejected, including a file newer than `kMaxSupportedVersion`. Fails with `Gem::Result::NotFound` if `fileName` does not exist, `Gem::Result::CorruptedData` if the `Magic` does not match, and `Gem::Result::OutOfRange` if `Version` is outside the supported range.

`Gem::Result CCpkgReader::Open(const char *fileName)`

**CCpkgReader::Close**

Closes a CCpkgReader instance. Returns `Gem::Result::NotOpen` if the reader is not open.

`Gem::Result CCpkgReader::Close()`

**CCpkgReader::GetBundleCount**

Returns the number of bundles in the package.

`uint32_t CCpkgReader::GetBundleCount() const`

**CCpkgReader::GetBundleName**

Returns the name of the bundle at `bundleIndex`, or `nullptr` if `bundleIndex` is out of range. This reads the packed name at that bundle's `RecordOffset` (see [File Layout and Fixups](#file-layout-and-fixups)); an implementation may cache names after first access. The returned pointer is owned by the reader and valid until `Close()`.

`const char *CCpkgReader::GetBundleName(uint32_t bundleIndex) const`

**CCpkgReader::GetAssetCount**

Returns the number of assets in the bundle at `bundleIndex`.

`uint32_t CCpkgReader::GetAssetCount(uint32_t bundleIndex) const`

**CCpkgReader::GetAssetType** / **CCpkgReader::GetAssetCompression**

Return the `AssetType` and `CpkgCompressionScheme` of the asset at `assetIndex` within the bundle at `bundleIndex`. Both are lookups into the bundle's fixed-stride asset table and read no payload bytes. An out-of-range index returns `AssetType::Unknown` and a zeroed `CpkgCompressionScheme` respectively. `GetAssetCompression`'s `RawSize` is what a caller allocates before calling `ReadAsset`.

```cpp
AssetType              CCpkgReader::GetAssetType(uint32_t bundleIndex, uint32_t assetIndex) const
CpkgCompressionScheme  CCpkgReader::GetAssetCompression(uint32_t bundleIndex, uint32_t assetIndex) const
```

**CCpkgReader::GetAssetName**

Returns the name of the asset at `assetIndex` within the bundle at `bundleIndex`, or `nullptr` if either index is out of range. This reads the packed name at that asset's `RecordOffset` (see [File Layout and Fixups](#file-layout-and-fixups)); an implementation may cache names after first access. The returned pointer is owned by the reader and valid until `Close()`.

`const char *CCpkgReader::GetAssetName(uint32_t bundleIndex, uint32_t assetIndex) const`

**CCpkgReader::ReadAsset**

Reads one asset payload from an open package. `pData` points to a caller-owned buffer and `dataSize` is its capacity, which must be at least `GetAssetCompression(bundleIndex, assetIndex).RawSize`. Fails with `Gem::Result::InvalidArg` if either index is out of range, if `pData` is null, or if `dataSize` is short of that asset's `RawSize` - all checked before any read. `ReadAsset` reads the asset's on-disk bytes and decompresses internally, so `pData` always receives `RawSize` bytes of raw payload whatever codec the asset is stored with.

`Gem::Result CCpkgReader::ReadAsset(uint32_t bundleIndex, uint32_t assetIndex, void *pData, uint64_t dataSize)`

## Source Layout

| Path | Description |
|---|---|
| `src/Inc/CanvasPkg.h` | Public header: `CCpkgWriter`, `CCpkgReader`, `CCpkgNameDirectory`, `AssetType`, `CpkgCodec`/`CpkgCompressionScheme`, and every `Cpkg*Data` schema |
| `src/CanvasPkg/*.cpp` | Implementation |
| `src/CanvasPkg/*.h` | Internal-only headers (file layout structs, fixup bookkeeping); not part of the public surface |

This follows the same split as the rest of Canvas: public, consumable headers live under `src/Inc` alongside `CanvasCore.h`/`CanvasGfx.h`, while a component's own subdirectory under `src/` holds its implementation and any headers private to it.

## Future Directions

Directions the current design leaves room for. None are committed plans:

- **Compression**: `CpkgCompressionScheme` carries a `Codec` field and both a stored and a raw size, but only `CpkgCodec::None` is implemented. Adding a codec is additive; existing `None`-coded assets keep working.
- **Model-level manifest**: there is no asset type for the model-level metadata `XModel` carries (the active-camera-node designation, overall bounds). It would need its own `AssetType` value and schema.
- **Streaming / zero-copy reads**: `ReadAsset` always copies into a caller buffer. Large texture payloads may eventually want a memory-mapped access path alongside the copying one.
- **Decoded, GPU-ready texture data**: a schema for pre-decoded pixel data with mip chains, array slices, and a GPU tile-region layout, for direct-to-GPU-tile streaming.

## Review Concerns (Open)

Design-review findings that block or complicate implementation as currently specified. Each is removed from this list once the design above is updated to resolve it.

1. **Stored engine enumerations are not all explicitly based or numbered.** [Dependencies](#dependencies) states that public Canvas enumerations declare an explicit base type. `GfxPrimitiveTopology` (`CanvasGfx.h`), which `CpkgMeshData::Topology` stores, declares neither a base type nor explicit enumerator values; it is 4 bytes only because a scoped enumeration defaults to `int`, and its on-disk values are its declaration order, so reordering the engine enumeration silently changes the file format. Either give it `: uint32_t` and explicit values in `CanvasGfx.h`, or define a package-owned topology enumeration and translate at load time. (`LightType` is explicit on both counts; `MaterialLayerRole` is also implicit but is not stored.)

2. **`CanvasPkg.h` cannot include `CanvasCore.h` portably.** `CanvasCore.h` uses `UINT` and `PCSTR`, which neither it, `CanvasGfx.h`, nor `Gem.hpp` defines; they come from `<windows.h>` being included first. cpkgbuilder targets Linux as well as Windows. Either CanvasPkg stops including engine headers - it needs only `LightType`, `LightFlags`, and the topology enumeration (see concern 1) - or those headers gain portable typedefs.

3. **No owner for record serialization, and the structure declarations are not C++ layouts.** `CCpkgWriter`/`CCpkgReader` move opaque bytes, so something must turn a `Cpkg*Data` into payload bytes and back, but this document does not say whether CanvasPkg provides encode/decode helpers or whether cpkgbuilder and the loader each write their own (two parsers of one format). The declarations cannot be used directly either:
   - A record with a `CpkgBlob` member ahead of other members (`CpkgMeshDataGroup`, `CpkgAnimTrack`, `CpkgMeshSkinData`, ...) has no C++ struct equivalent.
   - Even the fixed-size records differ from their natural C++ layout. `CpkgFileHeader` is 20 bytes on disk but `sizeof` 24; `CpkgBundleHeader` is 12 vs 16; `CpkgAssetTableEntry` is 32 vs 40, from padding before `CpkgCompressionScheme::StoredSize` and before the embedded struct.

   State which types are real packed PODs (with `static_assert`ed sizes) and which are notation only; state the on-disk sizes of `CpkgFileHeader`, `CpkgBundleHeader`, `CpkgBundleTableEntry`, and `CpkgAssetTableEntry` alongside the per-asset sizes; and decide where the payload serializers live. `char Magic[4]` also needs a primitive in [On-disk primitives](#on-disk-primitives).

4. **What `CCpkgReader::Open` loads and validates is unspecified.** `GetAssetCount`, `GetAssetType`, and `GetAssetCompression` are `const` and cannot fail, which implies `Open` reads the bundle table, every bundle header, and every asset table into memory. State that, and list the checks `Open` performs on this untrusted input: every offset and count within the file size, every record offset past the file header, `RecordOffset + 4 + nameLength + StoredSize` within the file, a known `Codec`, and `StoredSize == RawSize` for `CpkgCodec::None`. Also define what `GetAssetCount` returns for an out-of-range `bundleIndex`.

5. **Lazily read names conflict with `const`, error reporting, and concurrent reads.** `GetBundleName`/`GetAssetName` are `const`, read the file, and "may cache", so the cache must be `mutable` and races with the concurrent `ReadAsset` calls the reader otherwise permits. They also return `nullptr` for both an out-of-range index and an I/O failure, so a caller cannot tell the two apart. Either read and validate all names at `Open` or return `Gem::Result`.

6. **`ReadAsset` must read the name length to find the payload.** The payload begins at `RecordOffset + 4 + nameLength`, so a read either costs an extra positional read of the length prefix or depends on the name cache from concern 5. Consider storing the payload offset (or the name length) in `CpkgAssetTableEntry`.

7. **Writer argument and call-order rules are incomplete.** Unspecified: empty names, names with embedded NULs or invalid UTF-8, `dataSize == 0` (and whether `pData` may then be null), an unrecognized `codec` value, a null `bundleName`, and calling `Open` on a reader or writer that is already open.

8. **Staging preconditions and durability details.** For an amend, step 2 of [Staged Writes](#staged-writes) overwrites from `BundleTableOffset` onward, which is safe only if the bundle table is the file's tail; `Open(append = true)` should verify `BundleTableOffset + BundleCount * 8 == file size` and reject the file otherwise. Amending a package whose `Version` is older than `kMaxSupportedVersion` but still supported is undefined - reject it, or rewrite its header version (only valid if the layouts agree). For a new package, the rename in `Close` must not overwrite a `fileName` that another process created after `Open`; it should fail with `AlreadyExists` instead (`MoveFileExW` without `MOVEFILE_REPLACE_EXISTING` on Windows, `renameat2` with `RENAME_NOREPLACE` on Linux). On POSIX, a durable `rename(2)` also requires `fsync` on the containing directory. Pick one of `ReplaceFileW`/`MoveFileExW`; they differ in ACL/attribute preservation and in whether the target must exist.

9. **The empty-bounds rule does not match the engine's.** `CpkgMeshData::LocalBounds` says `Min > Max` means "auto-compute", but `Math::AABB::IsEmpty` (`CanvasMath.hpp`) recognizes only the exact sentinel `Min = +FLT_MAX`, `Max = -FLT_MAX` in all three axes and deems any other inside-out box invalid. Specify that exact sentinel and treat any other inside-out box as corrupt.

10. **Undefined spatial and color conventions.** "Canvas space" is never defined - handedness, up axis, and linear units should be stated here or linked. Color spaces are likewise unstated: whether `BaseColorFactor`/`EmissiveFactor` are linear, and how a loader decides whether to decode an `Image` as sRGB or linear when a single image asset can be bound to both color and non-color roles.

11. **No mapping from a bundle to engine objects.** [CanvasPkg](#canvaspkg) says `CCpkgReader` is used to build world instances, but this document does not say how a bundle's `SceneNode` assets become an `XModel` (or several), whether a bundle may contain multiple roots (`ParentIndex = -1`), or where that loader code lives, given that CanvasPkg links no Canvas library.

12. **Degenerate mesh records are undefined.** Specify whether a `CpkgMeshData` may have zero groups, whether a group may have zero vertices, and whether `BoneIndices` without `BoneWeights` (or the reverse) is corrupt; presumably the two must be present or absent together.

13. **Cubemaps have no representation.** cpkgbuilder's manifest can name a six-face cubemap, but `CpkgImageData` is a single encoded image with no face or grouping metadata (see cpkgbuilder's [Review Concerns](../cpkgbuilder/README.md#review-concerns-open), concern 1). The answer belongs in this format: a cubemap asset type, face metadata on `CpkgImageData`, or a naming convention both documents commit to.

14. **Minor: notation drift.** `CpkgMeshSkinData::BoneNodeIndices` is written `CpkgBlob<int32_t>` rather than in on-disk notation; since bone indices cannot be `-1`, `u32` (or an explicit non-negative rule) fits better. `CpkgAnimTrack::NodeName` is `CpkgBlob<>` but is a UTF-8 byte array and could be written as one.

15. **Minor: the decompression scratch pool is designed ahead of any codec.** v1 implements only `CpkgCodec::None`, which bypasses the pool, and at 64 KiB per buffer most compressed texture payloads would spill to one-off allocations anyway. Consider deferring [Decompression Scratch Buffer](#decompression-scratch-buffer) to the design pass that picks a codec.
