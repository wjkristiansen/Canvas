# CanvasPkg

A CanvasPkg (Cpkg) is a package file containing world data - including models, geometry, materials, textures, lights, cameras, animations, (and eventually scripts, sounds, and other world data). The file is organized into bundles which contain related assets. Bundle assets can interrelate by asset index within a bundle. For example, a bundle might contain model data with object/bone hierarchy, meshes, materials, textures, animation clips. Other bundles might contain sky-box texture, scripts, music or sound effects.

CanvasPkg is a static library. API Success/failure codes use type `Gem::Result`.

`CCpkgReader` is used by Canvas itself to build world instances and set up scripts at load time. `CCpkgWriter` is used by `cpkgbuilder`, a separate command-line tool that cooks a `.cpkg` file from a JSON manifest (see [cpkgbuilder](../cpkgbuilder/README.md)).

## Dependencies

`CanvasPkg.h` includes `CanvasCore.h` and `CanvasGfx.h`. The dependency is header-only and CanvasPkg links no Canvas library, so `cpkgbuilder` can include it without linking the engine.

Engine enumerations are used by name in the record declarations below. Public Canvas enumerations declare an explicit base type; an enumeration field is stored as its base type, holding the enumerator's numeric value, and occupies that many bytes on disk. Because those numeric values are the on-disk encoding, every enumeration a `Cpkg*` record stores assigns each enumerator an explicit value.

## On-Disk Encoding

This policy governs every structure written to a `.cpkg` file: the file header, the bundle and asset tables, and every asset payload.

**Every on-disk structure is a C++ struct declared in `CanvasPkg.h`, and its on-disk form is its `sizeof(T)` in-memory bytes.** A structure is written by copying its bytes out and read by copying bytes in (or, inside a payload buffer, by accessing it in place - see [Payload Layout](#payload-layout)). An array of `T` is `sizeof(T)` bytes per element. Sizes and offsets in this document are always expressed as `sizeof`/`alignof` of the declared types, and are never restated as numbers.

Members are fixed-width types only: `float` (IEEE-754 binary32), `int32_t`, `uint32_t`, `uint64_t`, `char`, `uint8_t`, enumerations whose explicit base type is one of these, fixed-size arrays of these, other on-disk structs, and `CpkgSpan<T>`. No pointers, `bool`, `size_t`, `long`, or bit-fields.

Every member is naturally aligned: its offset within the struct is a multiple of its own `alignof`, and the struct's `sizeof` is a multiple of its `alignof`. **An on-disk struct has no implicit padding.** Wherever natural alignment would leave a gap, including at the end of the struct, the struct declares an explicit `Padding` member that fills it. **Every `Padding` member must be zero**; each is declared with a `= 0` default member initializer, so a struct built from its defaults satisfies this. A reader does not interpret `Padding` members. Every byte of an on-disk struct is therefore a declared member with a defined value.

`CanvasPkg.h` uses `static_assert` to guarantee the required alignment on every compiler that builds it. A struct or platform that breaks any of these fails to compile:

- `alignof(uint64_t) == 8`, so every 64-bit member is 8-aligned and lands at the same offset on every compiler.
- For each on-disk struct, `sizeof(T)` equals the sum of its members' sizes, so the compiler has inserted no implicit padding and every gap is a declared `Padding` member.
- For each on-disk struct, `alignof(T) <= kCpkgPayloadAlignment`, so the struct can be accessed in place in an aligned payload buffer.
- Each on-disk struct is trivially copyable and standard-layout.

Changing an on-disk struct changes the format.

All values are little-endian. CanvasPkg supports only little-endian hosts, and `CanvasPkg.h` fails to compile on any other (`static_assert(std::endian::native == std::endian::little)`).

Bundle and asset names are the only on-disk data that are not structs. Every such name is stored in a **name blob**: UTF-8 names concatenated with no length prefixes, separators, or terminators. The file has one blob holding every bundle name, and each bundle has one holding the names of its assets. A bundle or asset table entry locates its name by `NameOffset` and `NameLength` within the corresponding blob (see [Bundles](#bundles)).

Name blobs hold only the names of bundles and assets themselves. A name stored inside an asset payload, such as `CpkgAnimTrack::NodeName`, is not in a name blob: it is a `CpkgSpan<char>`, like any other variable-length array, with the same UTF-8 and no-terminator rules.

## Assets

Assets are the primary data block for CanvasPkg files. Each asset has an `AssetType`, a payload of bytes whose internal layout is defined by that `AssetType` (see [Package Data Types](#package-data-types)), and optionally a name. An asset's payload is stored on disk on its own, with nothing before or after it. A named asset's name is stored separately in its bundle's asset name blob, with no imposed maximum length. The bundle's asset table (see [Bundles](#bundles)) is a flat, fixed-stride array, indexed by asset index, holding the type, payload offset, name location, and compression info of every asset in the bundle. `CCpkgReader::Open` loads every bundle's asset table and asset name blob, so a name costs no file read.

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

Bundle names must be non-empty and unique within a package.

Asset names are optional. An asset referenced only by its index may be unnamed, which is stored as an empty name. An asset that is referenced by name must be named: a `SceneNode` targeted by an `AnimationClip` track, and any asset referenced from another bundle or package. Like the reference-order rule above, this is a contract on whoever produces the package; the writer cannot tell which assets will be referenced by name. Non-empty asset names must be unique within a bundle; any number of assets in a bundle may be unnamed.

## Bundles

Bundles represent a related collection of assets such as mesh data, materials and textures. Bundles in a package must be uniquely named.

A package file starts with a fixed-size file header. A bundle has no header or record of its own: it is described entirely by its entry in the file-level bundle table, just as an asset is described by its entry in its bundle's asset table.

```cpp
// First bytes of a .cpkg file.
struct CpkgFileHeader
{
    char     Magic[4];              // "CPKG"; CCpkgReader::Open fails if this doesn't match
    uint32_t Version;               // format version; CCpkgReader::Open fails if outside [kMinSupportedVersion, kMaxSupportedVersion]
    uint64_t BundleTableOffset;     // file offset of the file-level bundle table
    uint64_t BundleNameBlobOffset;  // file offset of the bundle name blob
    uint64_t BundleNameBlobSize;    // byte size of the bundle name blob
    uint32_t BundleCount;           // number of entries at BundleTableOffset
    uint32_t Padding = 0;
};
```

The two tables have the same shape. Each is a flat, fixed-stride array containing no name bytes, so an index resolves to an offset by multiplication, and each entry locates its own name with `NameOffset`/`NameLength` in a name blob:

```cpp
// One entry per bundle in the file-level bundle table.
struct CpkgBundleTableEntry
{
    uint64_t AssetTableOffset;      // file offset of this bundle's asset table
    uint64_t NameOffset;            // byte offset of this bundle's name within the bundle name blob
    uint64_t AssetNameBlobOffset;   // file offset of this bundle's asset name blob
    uint64_t AssetNameBlobSize;     // byte size of this bundle's asset name blob
    uint32_t AssetCount;            // number of entries at AssetTableOffset
    uint32_t NameLength;            // byte length of this bundle's name
};

// One entry per asset in a bundle's asset table.
struct CpkgAssetTableEntry
{
    uint64_t               PayloadOffset; // file offset of this asset's payload
    uint64_t               NameOffset;    // byte offset of this asset's name within the bundle's asset name blob
    AssetType              Type;
    uint32_t               NameLength;    // byte length of this asset's name; 0 = unnamed, and NameOffset is then 0
    CpkgCompressionScheme  Compression;   // see Compression
};
```

| | Table | Name blob |
|---|---|---|
| **File level** (bundles) | `CpkgFileHeader::BundleCount` entries of `CpkgBundleTableEntry`, starting at `BundleTableOffset` | `BundleNameBlobSize` bytes, starting at `BundleNameBlobOffset` |
| **Per bundle** (assets) | `CpkgBundleTableEntry::AssetCount` entries of `CpkgAssetTableEntry`, starting at `AssetTableOffset` | `AssetNameBlobSize` bytes, starting at `AssetNameBlobOffset` |

A bundle's or asset's name is the `NameLength` bytes at `NameOffset` within the corresponding blob. An asset's `PayloadOffset` points directly at its payload. `CCpkgReader::Open` loads both tables and both kinds of name blob into memory, so the reader never reads the file to find a name.

### File Layout and Fixups

Both tables and the name blobs are variable-length and grow as the writer appends, so none has a fixed, pre-reserved location. The writer accumulates each in memory and serializes it once, when the thing it describes is sealed.

`Open` writes `CpkgFileHeader::Magic`/`Version` immediately, leaving the remaining header fields zero as placeholders. `BeginBundle` writes nothing to the file; it starts a new in-memory bundle table entry. `EndBundle` seals the open bundle: it appends that bundle's asset table and asset name blob at the current end of file and records their locations in the bundle's in-memory table entry. `Close` appends the file-level bundle table, immediately followed by the bundle name blob, then seeks back and overwrites the file header's placeholder fields to point at them.

Every write is therefore an append, except the single fixup of the file header in `Close`. No previously written payload, table, or name blob is moved or rewritten.

A file whose writer never completed `Close` still has a zero `BundleTableOffset`, so `CCpkgReader::Open` rejects it as `Gem::Result::CorruptedData`.

## Package Data Types

`CCpkgWriter`/`CCpkgReader` move opaque, named, typed byte payloads. This section defines what those bytes mean for each `AssetType`.

**Every payload has the same shape: a header struct at offset 0, followed by a data area holding the payload's variable-length arrays.** The `AssetType` determines the header struct (`CpkgMeshData` for `AssetType::MeshData`, and so on). The header refers to each array through a `CpkgSpan<T>` member, and an array's elements may themselves hold spans. An asset type with no variable-length data (`SceneNode`, `Material`, `Camera`, `Light`) has a payload that is exactly its header, with no data area. The structs in this section follow the [On-Disk Encoding](#on-disk-encoding) rules, and [Payload Layout](#payload-layout) defines spans and the data area.

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
    uint32_t  Padding    = 0;
    uint64_t  StoredSize = 0;   // on-disk byte count
    uint64_t  RawSize    = 0;   // uncompressed byte count
};
```

A reader obtains an asset's scheme from `GetAssetCompression`, whose `RawSize` is the buffer size `ReadAsset` requires.

`CpkgCompressionScheme` is an on-disk record and a reader-side return value only. `AppendAsset` takes the codec and the raw payload as parameters and computes `StoredSize` itself; there is no caller-populated descriptor struct on either the write or the read path.

### Payload Layout

A variable-length array lives in the payload's data area, and the struct that owns it holds a span referring to it:

```cpp
// Count elements of T, stored contiguously in the data area of the payload
// that contains this span.
template <class T>
struct CpkgSpan
{
    uint64_t Offset = 0;  // byte offset of element 0 from the start of the payload
    uint64_t Count  = 0;  // number of elements; 0 = empty
};
```

These rules define where a span's elements are and when a span is valid:

- **Offsets are relative to the start of the payload**, the byte at which the header struct begins. That holds for every span, including one inside an array element such as `CpkgMeshDataGroup::Positions`. An offset is never relative to the span itself or to the struct containing it.
- Element `i` is at `Offset + i * sizeof(T)`.
- `Offset` is a multiple of `alignof(T)`.
- A non-empty span lies wholly after the header and within the payload: `Offset >= sizeof(header)` and `Offset + Count * sizeof(T) <= RawSize`, both evaluated without overflow.
- `Count == 0` is an empty array; for an optional array, empty means absent. An empty span has `Offset == 0`.
- Spans nest to whatever depth the schemas declare. `CpkgMeshData::Groups` refers to an array of `CpkgMeshDataGroup`, and each of those holds spans referring to that group's vertex streams.

A reader may treat a payload that violates any of these rules as corrupt (`Gem::Result::CorruptedData`).

**The data area has no prescribed order.** A reader finds every array through its span and never by position, so a writer may place arrays in any order. Spans must not overlap, and the alignment gaps between arrays must be zero; `CCpkgPayloadBuilder` guarantees both. A reader does not check for overlap, since every access is read-only, and does not interpret the gap bytes. For example, `CCpkgPayloadBuilder` (below) lays out a mesh with two groups in the order it is called, which for cpkgbuilder is:

```
CpkgMeshData                 header at offset 0; Groups refers to the group array
CpkgVertexPosition[N0]       group 0 Positions
CpkgVertexNormal[N0]         group 0 Normals
CpkgVertexPosition[N1]       group 1 Positions
CpkgVertexNormal[N1]         group 1 Normals
CpkgMeshDataGroup[2]         group array; each entry's spans refer to that group's streams above
```

Each array starts at the next offset that is a multiple of its element alignment.

**Alignment in memory.** Every span offset is aligned relative to the start of the payload, and no on-disk struct needs more than `kCpkgPayloadAlignment` (`static_assert`ed). So once a payload is loaded into a buffer whose start is aligned to `kCpkgPayloadAlignment`, a reader can use the header and every array in place, without copying:

```cpp
constexpr size_t kCpkgPayloadAlignment = 16;
```

Payloads are not aligned within the file. Alignment applies to the buffer that `ReadAsset` fills.

Parallel arrays must have equal counts: each per-vertex stream with its group's `Positions`, and `InvBindPoses` with `BoneNodeIndices`. A reader may treat a mismatch as corrupt.

### Payload Encoding Helpers

`CCpkgWriter` and `CCpkgReader` move opaque bytes. `CanvasPkg.h` provides header-only helpers that encode and decode payloads, so cpkgbuilder and the loader share one implementation of the layout rules above. The helpers link no library and touch no file.

```cpp
// Builds one payload in memory. The header slot at offset 0 is reserved on
// the first call; arrays are appended to the data area in call order.
template <class THeader>
    requires std::is_trivially_copyable_v<THeader>
class CCpkgPayloadBuilder
{
public:
    // Pads the data area to alignof(T), appends a copy of elements, and writes
    // the span referring to them to *pSpan. An empty elements appends nothing
    // and writes an empty span. T is deduced from pSpan only, so any
    // contiguous container of T converts to elements.
    template <class T>
        requires std::is_trivially_copyable_v<T>
    Gem::Result Append(std::span<const std::type_identity_t<T>> elements, CpkgSpan<T> *pSpan);

    // Copies header into the slot at offset 0. Typically called last, once
    // every span the header holds is known.
    Gem::Result SetHeader(const THeader &header);

    std::span<const std::byte> GetPayload() const;   // pass to CCpkgWriter::AppendAsset as data
};

// Returns the payload's header in place. Fails with InvalidArg if payload is
// empty or not aligned to kCpkgPayloadAlignment, and CorruptedData if
// payload.size() < sizeof(THeader).
template <class THeader>
    requires std::is_trivially_copyable_v<THeader>
Gem::Result CpkgGetPayloadHeader(std::span<const std::byte> payload, const THeader **ppHeader);

// Checks span against the Payload Layout rules and returns its elements in
// place, or an empty std::span for an empty span. Fails with InvalidArg if
// payload is empty or misaligned, and CorruptedData if the span is
// misaligned, overlaps the header, or extends past the end of payload.
template <class THeader, class T>
    requires std::is_trivially_copyable_v<T>
Gem::Result CpkgResolveSpan(std::span<const std::byte> payload, const CpkgSpan<T> &span, std::span<const T> *pElements);
```

A nested array is resolved the same way as a top-level one, against the same payload:

```cpp
// payload: the RawSize bytes ReadAsset filled, i.e. data.first(rawSize)
const CpkgMeshData *pMesh = nullptr;
std::span<const CpkgMeshDataGroup> groups;
// (Result checks omitted)
CpkgGetPayloadHeader(payload, &pMesh);
CpkgResolveSpan<CpkgMeshData>(payload, pMesh->Groups, &groups);
for (const CpkgMeshDataGroup &group : groups)
{
    std::span<const CpkgVertexPosition> positions;
    CpkgResolveSpan<CpkgMeshData>(payload, group.Positions, &positions);
    // ...
}
```

The helpers check only structure. Schema rules - parallel counts, the streams each topology requires, asset index ordering - are checked by the loader that interprets the payload.

### CpkgSceneNodeData (AssetType::SceneNode)

One node in a model's node/bone hierarchy. A bundle's `SceneNode` assets together describe the whole hierarchy; `ParentIndex` links a node to its parent by asset index.

Fixed size: the payload is one `CpkgSceneNodeData`.

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

The node's name comes from the `name` it was appended with, matched against cloned node names the same way `AnimationTrackDesc::NodeName` is today. An unnamed node cannot be targeted by an animation track.

### CpkgMeshData (AssetType::MeshData)

Corresponds to `MeshDataDesc`/`MeshDataGroupDesc`. Each vertex stream is its own array holding one element per vertex; the element structs below fix each stream's components.

Variable size. The payload's header is one `CpkgMeshData`. Its `Groups` span refers to an array of `CpkgMeshDataGroup` in the data area, and each group's spans refer to that group's vertex streams (see [Payload Layout](#payload-layout) for an example layout):

```cpp
struct CpkgVertexPosition    { float    XYZ[3];    };
struct CpkgVertexNormal      { float    XYZ[3];    };
struct CpkgVertexUV          { float    UV[2];     };
struct CpkgVertexTangent     { float    XYZW[4];   };  // xyz = tangent, w = bitangent sign
struct CpkgVertexBoneIndices { uint32_t Index[4];  };
struct CpkgVertexBoneWeights { float    Weight[4]; };

struct CpkgAABB
{
    float Min[3] = {  FLT_MAX,  FLT_MAX,  FLT_MAX };  // xyz
    float Max[3] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };  // xyz
};

// One material partition of a mesh. The vertex count is Positions.Count.
struct CpkgMeshDataGroup
{
    int32_t                         MaterialIndex = -1;  // asset index of a Material; -1 = none
    uint32_t                        Padding       = 0;
    CpkgSpan<CpkgVertexPosition>    Positions;           // required by every topology
    CpkgSpan<CpkgVertexNormal>      Normals;             // required by every topology
    CpkgSpan<CpkgVertexUV>          UV0;                 // required by PatchList4CP, optional otherwise
    CpkgSpan<CpkgVertexTangent>     Tangents;            // optional
    CpkgSpan<CpkgVertexBoneIndices> BoneIndices;         // optional; palette-relative, see below
    CpkgSpan<CpkgVertexBoneWeights> BoneWeights;         // optional
};

struct CpkgMeshData
{
    GfxPrimitiveTopology            Topology;            // CanvasGfx.h
    CpkgAABB                        LocalBounds;         // empty sentinel = compute from positions; see below
    uint32_t                        Padding = 0;
    CpkgSpan<CpkgMeshDataGroup>     Groups;
};
```

Every non-empty stream's `Count` must equal the group's `Positions.Count`.

**`LocalBounds` is either empty or well-formed.** Empty is the exact sentinel `Min = {+FLT_MAX, +FLT_MAX, +FLT_MAX}`, `Max = {-FLT_MAX, -FLT_MAX, -FLT_MAX}`: `CpkgAABB`'s default, and the state `Canvas::Math::AABB::IsEmpty` recognizes. It means the loader passes an empty `Canvas::MeshDataDesc::LocalBounds` and the backend computes the bounds from the positions. Otherwise `Min[i] <= Max[i]` must hold on every axis, which also excludes NaN. Any other box, including one that is inside-out on some axes or inverted without matching the sentinel, is not empty; a reader may treat it as corrupt (`Gem::Result::CorruptedData`).

**Which streams are required depends on `Topology`**, matching what the backend enforces in `CreateMeshData`: `TriangleList` requires `Positions` and `Normals`; `PatchList4CP` requires `Positions`, `Normals`, and `UV0` (per-control-point map UVs, and the base direction along which displacement extends each control point). `Tangents`, `BoneIndices`, and `BoneWeights` are optional under either topology.

A skinned mesh, its `MeshSkin` asset, and every bone it references must all live in the same bundle. `BoneIndices` is the one cross-reference field in this schema that is not a bundle asset index: it is a palette-relative index into the mesh's `CpkgMeshSkinData::BoneNodeIndices` array, and by the ordering correspondence into `InvBindPoses`. This matches the runtime skinning convention. `CpkgMeshSkinData` is the single place bones are referenced by asset index. An unused influence is encoded as weight 0 in `BoneWeights`, with its `BoneIndices` slot ignored; there is no reserved "none" bone-index sentinel.

### CpkgMaterialData (AssetType::Material)

Covers the material properties `XGfxMaterial` exposes today: texture-role bindings, the three PBR factors, and the optional displacement extension.

Fixed size: the payload is one `CpkgMaterialData`.

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
    float  BaseColorFactor[4];      // RGBA
    float  EmissiveFactor[4];       // RGBA
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

The `Displacement` block is always present, even when `HasDisplacement` is `0`, so the struct has a single fixed layout. A reader ignores its contents when `HasDisplacement` is `0`.

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
    uint32_t           Padding = 0;
    CpkgSpan<uint8_t>  ImageData;  // encoded bytes, exactly as read from the source
};
```

The image's byte count is `ImageData.Count`.

`Encoding` and `Compression.Codec` describe separate layers. `Encoding` describes the payload itself and is opaque to CanvasPkg, which stores and returns those bytes untouched. `Compression.Codec` is transport compression applied over the payload, and `ReadAsset` always reverses it before returning (see [Compression](#compression)). Any `CpkgCodec` is valid for any `Encoding`.

Tiled/decoded GPU-ready texture support (mip chains, array slices, GPU tile layout) is a future design pass - see [Future Directions](#future-directions).

### CpkgAnimClipData (AssetType::AnimationClip)

Corresponds to `AnimationClipDesc`/`AnimationTrackDesc`. Tracks target nodes **by name**, as `AnimationTrackDesc` does, so a clip binds to any model or instance whose node names match, independent of that hierarchy's index layout.

Variable size. The payload's header is one `CpkgAnimClipData`. Its `Tracks` span refers to an array of `CpkgAnimTrack` in the data area, and each track's spans refer to that track's name and keyframes:

```cpp
struct CpkgKeyframe
{
    float Time;                    // seconds from clip start
    float Translation[3];          // xyz
    float Rotation[4];             // unit quaternion, XYZW
    float Scale[3];                // xyz
};

struct CpkgAnimTrack
{
    CpkgSpan<char>          NodeName;    // UTF-8, not null-terminated; matched by name at bind time
    CpkgSpan<CpkgKeyframe>  Keyframes;   // sorted ascending by Time
};

struct CpkgAnimClipData
{
    float                   Duration;    // seconds
    uint32_t                Padding = 0;
    CpkgSpan<CpkgAnimTrack> Tracks;
};
```

### CpkgMeshSkinData (AssetType::MeshSkin)

Corresponds to `MeshSkinDesc`. A skin binding is tied to one model's node topology, so bones are referenced by asset index.

Variable size. The payload's header is one `CpkgMeshSkinData`, whose spans refer to the two per-bone arrays in the data area:

```cpp
// Row-major, Canvas row-vector convention: v' = v * M, translation in row 3.
struct CpkgInvBindPose { float M[16]; };

struct CpkgMeshSkinData
{
    int32_t                     MeshNodeIndex = -1;  // asset index of the SceneNode carrying the skinned mesh
    uint32_t                    Padding       = 0;
    CpkgSpan<uint32_t>          BoneNodeIndices;     // asset indices of SceneNode bones; one per bone
    CpkgSpan<CpkgInvBindPose>   InvBindPoses;        // one per bone, same order as BoneNodeIndices
};
```

The bone count is `BoneNodeIndices.Count`; `InvBindPoses.Count` must equal it.

### CpkgCameraData (AssetType::Camera)

Corresponds to `XCamera`'s settable state (`Camera.h`). Fixed size: the payload is one `CpkgCameraData`.

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

Corresponds to `XLight`'s settable state (`Light.h`): light type, color/intensity, point/spot attenuation, spot cone angles, and shadow parameters. `Flags` is a `LightFlags` bit set (`CastsShadows`, `Enabled`); the shadow fields take effect only when `CastsShadows` is set. Fixed size: the payload is one `CpkgLightData`.

```cpp
struct CpkgLightData
{
    LightType Type                 = LightType::Point;
    float     Color[4]             = { 1.0f, 1.0f, 1.0f, 1.0f };  // RGBA
    float     Intensity            = 1.0f;
    LightFlags Flags               = LightFlags::Enabled;

    // Attenuation (Point and Spot lights)
    float     AttenuationConstant  = 1.0f;
    float     AttenuationLinear    = 0.0f;
    float     AttenuationQuadratic = 0.0f;
    float     Range                = 100.0f;

    // Spot light cone angles, in radians (only valid for Spot lights)
    float     SpotInnerAngle       = 0.785398f;
    float     SpotOuterAngle       = 1.047198f;

    // Shadow parameters (consumed by the backend when Flags has CastsShadows set)
    uint32_t  ShadowResolution     = 0;      // 0 = backend default (2048)
    float     ShadowConstantBias   = 1e-4f;
    float     ShadowSlopeScaleBias = 2.0f;
    float     ShadowNormalOffset   = 0.5f;   // in shadow-map texels
};
```

The shadow fields are always present, even when `Flags` does not have `CastsShadows` set, so the struct has a single fixed layout. The backend ignores them in that case.

## Error handling and result reporting

Every call that can fail returns `Gem::Result`. A call that produces an index writes it through an optional out parameter, which may be null and is written only on success:

```cpp
uint32_t assetIndex = 0;
Gem::Result r = writer.AppendAsset(AssetType::Material, "Brass", CpkgCodec::None, builder.GetPayload(), &assetIndex);
```

The codes this API reports:

| Code | Meaning |
|---|---|
| `Gem::Result::Success` | |
| `Gem::Result::InvalidArg` | Caller error: an out-of-range index, a null required argument, a `ReadAsset` destination buffer smaller than the asset's `RawSize`, an `Open` call on an object that is already open, a writer call or argument that breaks a [writer argument rule](#writer-argument-rules), or an empty or misaligned payload passed to a [payload helper](#payload-encoding-helpers) |
| `Gem::Result::NotFound` | The named file does not exist |
| `Gem::Result::IoError` | A read, write, or seek on the package file failed, or the writer could not create it |
| `Gem::Result::DataNotRecognized` | The file is not a package: it is too short to hold a `CpkgFileHeader`, or its `Magic` does not match. The file may be valid data of some other kind |
| `Gem::Result::CorruptedData` | The file is a package (its `Magic` matches) but its structure is damaged: a table, name blob, name, or payload outside the file, an empty bundle name, an unknown `AssetType` or `CpkgCodec`, a `CpkgCodec::None` asset whose `StoredSize` differs from its `RawSize`, or an out-of-order reference (see `CCpkgReader::Open`). From a payload helper: a payload too small for its header, or a span that breaks a [Payload Layout](#payload-layout) rule |
| `Gem::Result::OutOfRange` | The package's `Version` is outside this build's supported range |
| `Gem::Result::AlreadyExists` | `CCpkgWriter::Open` was called on an existing file |
| `Gem::Result::OutOfMemory` | An allocation for an in-memory table, directory, or scratch buffer failed |
| `Gem::Result::NotOpen` | The call was made on a reader or writer that is not open: before a successful `Open`, after `Close`, or after a writer failure closed it |

Every `Gem::Result`-returning method of `CCpkgWriter` and `CCpkgReader` other than `Open` - reads, writes, and `Close` alike - returns `Gem::Result::NotOpen` when the object is not open, and does nothing else. Conversely, `Open` on an object that is already open returns `Gem::Result::InvalidArg` and leaves the open package untouched; call `Close` first. The reader's value-returning accessors (`GetBundleCount`, `GetBundleName`, ...) cannot report a code; on an object that is not open they return the same value they return for an out-of-range index.

Reader method names follow one convention, which new methods must keep:

- **`Get*`** methods answer from the structure `Open` loaded into memory. They read no file data, cannot fail, and return their result directly. An out-of-range index or a reader that is not open yields a documented sentinel value, so callers check indices against `GetBundleCount`/`GetAssetCount` (typically with an `assert`) instead of checking a result code.
- **`Read*`** methods read file data. They can fail, so they return `Gem::Result` and deliver their output through a parameter.

A future accessor for data `Open` does not load (per-bundle or per-asset metadata, for example) is therefore a `Read*` method, not a `Get*` method.

`CCpkgWriter` is a single-threaded, call-in-order API. A `CCpkgReader`'s state does not change between `Open` and `Close`, so every `const` method is safe to call concurrently from multiple threads on the same instance. `Open` and `Close` are not, and must not overlap any other call on that instance.

### Writer failure closes the writer

Every `CCpkgWriter` method validates its arguments and call order before modifying anything. A validation failure returns `Gem::Result::InvalidArg` and leaves the writer and its file exactly as they were; the writer stays open and later valid calls proceed normally.

Any other failure - an `IoError` or `OutOfMemory` from `BeginBundle`, `AppendAsset`, `EndBundle`, `CopyBundle`, or `Close` - closes the writer. The failing call releases the file handle, deletes the partially written file, and returns the code describing the cause. Destroying a `CCpkgWriter` that is still open does the same cleanup. The writer is then not open, so every later call, including `Close`, returns `Gem::Result::NotOpen`. The cause of a failure is reported once, by the call that failed; a later `NotOpen` means an earlier call already failed or the writer was never opened.

A reader failure does not close the reader. `ReadAsset` modifies no state, so an `IoError` or `CorruptedData` from one call leaves the reader open and other assets readable.

## CCpkgWriter

A CCpkgWriter class writes bundles and assets to a package. CanvasPkg bundle and asset writes are append-only, with a single fixup of the file header in `Close` (see [File Layout and Fixups](#file-layout-and-fixups)).

### Copying Bundles

`CopyBundle` copies a sealed bundle from a package open in a `CCpkgReader` into the package being written. The copy is byte-for-byte: each asset keeps its index, type, name, and compression scheme, and its stored payload is copied without being decompressed or interpreted. Because asset indices are preserved, the bundle's intra-bundle references remain valid in the new package.

This makes whole-bundle operations cheap without the format supporting in-place modification:

- **Merging packages.** One writer can copy bundles from any number of open readers, producing a single package from several. A bundle can be given a new name as it is copied, so bundles with the same name in different source packages can coexist.
- **Append-like updates.** Copy every bundle of an existing package into a new file, add, replace, or drop bundles along the way, then replace the original file with the new one.

Bundle indices are not preserved: a copied bundle's index is its position in the new package. Nothing in the format stores a bundle index, so this breaks no reference.

### Writer argument rules

The writer rejects any argument that would break a format rule or produce a file `CCpkgReader::Open` rejects, so every package `CCpkgWriter` produces is one its reader accepts. Each rule is checked before anything is written, and a violation returns `Gem::Result::InvalidArg` and leaves the writer unchanged (see [Writer failure closes the writer](#writer-failure-closes-the-writer)):

- **Call order**: `Open` while the writer is already open; `BeginBundle` or `CopyBundle` while a bundle is open; `AppendAsset` or `EndBundle` with no bundle open; `Close` while a bundle is open.
- **`CopyBundle` source**: `reader` must be open and `bundleIndex` in range.
- **Names** (`bundleName`, `name`, and the name a `CopyBundle` copy receives): must be valid UTF-8, must not contain a NUL byte, and must be at most `UINT32_MAX` bytes (the width of `NameLength`). A bundle name must be non-empty and unique within the package; an asset name may be empty, and a non-empty one must be unique within its bundle (see [Assets](#assets)).
- **`assetType`**: must be a named `AssetType` enumerator other than `AssetType::Unknown`.
- **`codec`**: must be a named `CpkgCodec` enumerator.
- **`data`**: must be non-empty. Every payload begins with its header struct (see [Package Data Types](#package-data-types)), so no asset type has an empty payload.
- **Counts**: a `BeginBundle` that would make the package's bundle count exceed `UINT32_MAX`, or an `AppendAsset` that would make the open bundle's asset count exceed it (the widths of `BundleCount` and `AssetCount`).

The reader does not recheck the name rules: a name that breaks them cannot cause an out-of-bounds read.

### Methods

**CCpkgWriter::Open**

Creates `fileName` and opens it for write. Fails with `Gem::Result::AlreadyExists` if `fileName` already exists, leaving it untouched.

`Gem::Result CCpkgWriter::Open(const char *fileName)`

**CCpkgWriter::Close**

Writes the file-level bundle table and bundle name blob, fixes up the file header, and closes the file (see [File Layout and Fixups](#file-layout-and-fixups)). Fails with `Gem::Result::InvalidArg` if a bundle is still open, leaving the writer open; call `EndBundle` first. Any other failure deletes the file and closes the writer (see [Writer failure closes the writer](#writer-failure-closes-the-writer)). Returns `Gem::Result::NotOpen` if the writer is not open.

`Gem::Result CCpkgWriter::Close()`

**CCpkgWriter::BeginBundle**

Begins a new bundle scope and writes the new bundle's index to `pBundleIndex`, which may be null. Writes nothing to the file; the bundle's table entry is held in memory until `Close`. Every `BeginBundle` must be matched by an `EndBundle`. Fails with `Gem::Result::InvalidArg` if a bundle is already open or `bundleName` breaks a [writer argument rule](#writer-argument-rules).

`Gem::Result CCpkgWriter::BeginBundle(std::string_view bundleName, uint32_t *pBundleIndex = nullptr)`

**CCpkgWriter::EndBundle**

Seals the open bundle: appends its asset table and asset name blob and records their locations in the bundle's in-memory table entry (see [File Layout and Fixups](#file-layout-and-fixups)). The bundle is immutable afterward; there is no operation to reopen one. Fails with `Gem::Result::InvalidArg` if no bundle is open.

`Gem::Result CCpkgWriter::EndBundle()`

**CCpkgWriter::AppendAsset**

Appends one asset to the open bundle and writes its index to `pAssetIndex`, which may be null. Assets are indexed in append order within the bundle, starting at zero and shared across all asset types, so a caller appends assets of different types in whatever order it produces them - see [Assets](#assets).

`name` may be empty for an asset referenced only by index (see [Assets](#assets)); a non-empty `name` must be unique within the bundle. `data` is the raw, uncompressed payload, and `codec` is the codec to store it with (`CpkgCodec::None` for no compression). `AppendAsset` compresses the payload and writes the result to the file before returning, retaining nothing beyond the in-memory bundle and asset tables and the open bundle's name blob, so a caller stages and appends one asset at a time while building a large model.

Fails with `Gem::Result::InvalidArg` if no bundle is open or any argument breaks a [writer argument rule](#writer-argument-rules). All of these are checked before anything is written; any failure after that closes the writer (see [Writer failure closes the writer](#writer-failure-closes-the-writer)).

`Gem::Result CCpkgWriter::AppendAsset(AssetType assetType, std::string_view name, CpkgCodec codec, std::span<const std::byte> data, uint32_t *pAssetIndex = nullptr)`

**CCpkgWriter::CopyBundle**

Copies the bundle at `bundleIndex` in `reader` into the package as a new, sealed bundle, and writes its index in this package to `pBundleIndex`, which may be null (see [Copying Bundles](#copying-bundles)). The copy is named `newName`, or the source bundle's name if `newName` is empty. Payload bytes are copied as stored and never inspected, so a payload that is corrupt in the source is equally corrupt in the copy.

Fails with `Gem::Result::InvalidArg` if a bundle is open, `reader` is not open, `bundleIndex` is out of range, or the copy's name breaks a [writer argument rule](#writer-argument-rules), including one already used in this package. All of these are checked before anything is written. A failed read from `reader` returns `Gem::Result::IoError` and, like any failure after validation, closes the writer; `reader` is unaffected.

`Gem::Result CCpkgWriter::CopyBundle(const CCpkgReader &reader, uint32_t bundleIndex, std::string_view newName = {}, uint32_t *pBundleIndex = nullptr)`

## CCpkgReader

CanvasPkg reads are random-access. The file-level bundle table and bundle name blob provide bundle names and the location of each bundle's asset table, and each bundle's asset table and asset name blob provide asset locations, names, types, and compression info without needing to read payload bytes.

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
    uint32_t Find(std::string_view name) const;

private:
    struct NameHash
    {
        using is_transparent = void;
        size_t operator()(std::string_view name) const noexcept { return std::hash<std::string_view>{}(name); }
    };

    std::unordered_map<std::string, uint32_t, NameHash, std::equal_to<>> m_Entries;
    friend class CCpkgReader;
};
```

`Find` looks names up without allocating, so a loader can search directly for a name it holds as a `std::string_view`, such as one resolved from a payload's `CpkgSpan<char>`.

**CCpkgReader::BuildBundleDirectory**

Fills `*pDirectory` from the bundle names loaded by `Open`, mapping bundle name to bundle index. Reads no file data.

`Gem::Result CCpkgReader::BuildBundleDirectory(CCpkgNameDirectory *pDirectory) const`

**CCpkgReader::BuildAssetDirectory**

Fills `*pDirectory` from the asset names of the bundle at `bundleIndex` loaded by `Open`, mapping asset name to asset index. Reads no file data. When `typeFilter` is `AssetType::Unknown` (the default), every named asset in the bundle is indexed; otherwise only assets of that `AssetType` are, which keeps the directory small for a caller that looks up one asset type by name.

`Gem::Result CCpkgReader::BuildAssetDirectory(uint32_t bundleIndex, CCpkgNameDirectory *pDirectory, AssetType typeFilter = AssetType::Unknown) const`

### Decompression Scratch Buffer

A compressed asset is read into a scratch buffer and decompressed from there into the caller's `data`. An asset stored with `CpkgCodec::None` skips the scratch entirely and is read straight into `data`, so in v1 - where `None` is the only codec - this pool is never exercised. `CCpkgReader` owns a private pool of reusable buffers, so concurrent `ReadAsset` calls on one instance do not contend for the same memory. The buffer size and pool size are fixed constants, not caller-configurable:

```cpp
// Internal to CCpkgReader; not part of the public API.
constexpr size_t kScratchBufferCapacity = 64 * 1024;  // 64 KiB
constexpr size_t kScratchBufferPoolSize = 4;          // concurrent ReadAsset calls before spillover
```

A `ReadAsset` call on a compressed asset checks out a free buffer for the duration of the call and returns it afterward. Two situations spill over to a one-off allocation freed at the end of that call: an asset whose `StoredSize` exceeds `kScratchBufferCapacity`, and a call arriving when all `kScratchBufferPoolSize` buffers are checked out. The pool's steady-state footprint stays at `kScratchBufferPoolSize * kScratchBufferCapacity` in either case. Both constants are sized for whatever codec is eventually added; until then they have no effect on any read.

Concurrent `ReadAsset` calls also require the underlying file access to be positional (`pread`, or `ReadFile` with an explicit offset) so they do not race on a shared seek cursor. The pool is the only reader state that changes after `Open`; it is synchronized internally, so it does not affect the concurrency contract described in [Error handling and result reporting](#error-handling-and-result-reporting).

### Methods

**CCpkgReader::Open**

Opens a package for read. Validates `CpkgFileHeader::Magic`, then `Version` against this build's supported range:

```cpp
constexpr uint32_t kMinSupportedVersion = 1;  // oldest CpkgFileHeader::Version this build can still read
constexpr uint32_t kMaxSupportedVersion = 1;  // current format version this build implements
```

A file whose `Version` falls outside `[kMinSupportedVersion, kMaxSupportedVersion]` is rejected, including a file newer than `kMaxSupportedVersion`.

`Open` then loads the package's structure into memory: the file-level bundle table and bundle name blob, and every bundle's asset table and asset name blob. Each is one contiguous read. Payloads are not read.

The file is untrusted input, so `Open` validates everything it loads before accepting it. With `fileSize` the size of the file, and every sum and product evaluated without overflow:

- **File header**: `fileSize >= sizeof(CpkgFileHeader)`, `Magic` matches, and `Version` is supported.
- **Bundle table and bundle name blob**: `BundleTableOffset >= sizeof(CpkgFileHeader)` and `BundleTableOffset + BundleCount * sizeof(CpkgBundleTableEntry) <= fileSize`; `BundleNameBlobOffset >= sizeof(CpkgFileHeader)` and `BundleNameBlobOffset + BundleNameBlobSize <= fileSize`.
- **Bundle table entries**: for each bundle, `NameLength > 0` and `NameOffset + NameLength <= BundleNameBlobSize`; `AssetTableOffset >= sizeof(CpkgFileHeader)` and `AssetTableOffset + AssetCount * sizeof(CpkgAssetTableEntry) <= fileSize`; `AssetNameBlobOffset >= sizeof(CpkgFileHeader)` and `AssetNameBlobOffset + AssetNameBlobSize <= fileSize`.
- **Asset table entries**: for each asset, `NameOffset + NameLength <= AssetNameBlobSize` for its bundle, and `NameOffset == 0` when `NameLength == 0`; `Type` is a named `AssetType` enumerator other than `Unknown`; `Compression.Codec` is a named `CpkgCodec` enumerator; `Compression.StoredSize == Compression.RawSize` when `Codec` is `CpkgCodec::None`; `PayloadOffset >= sizeof(CpkgFileHeader)` and `PayloadOffset + Compression.StoredSize <= fileSize`.

Each table's and name blob's extent is checked against `fileSize` before it is allocated, so a corrupt count or size cannot cause an allocation larger than the file. Every bound is checked here, so no later call can find a table entry, name, or payload outside the file.

`Open` does not check that tables, name blobs, and payloads are disjoint, that names are unique, or anything inside a payload: none of these can cause an out-of-bounds read. `CCpkgWriter` never produces duplicate names; in a file that contains them anyway, a [name directory](#name-directories) maps the name to one of the duplicates, unspecified which. Payload contents are checked by the [payload helpers](#payload-encoding-helpers) and the loader.

Fails with `Gem::Result::NotFound` if `fileName` does not exist, `Gem::Result::IoError` if the file cannot be read, `Gem::Result::DataNotRecognized` if the file is too short for a `CpkgFileHeader` or its `Magic` does not match, `Gem::Result::CorruptedData` if any later check fails other than `Version`, `Gem::Result::OutOfRange` if `Version` is outside the supported range, and `Gem::Result::OutOfMemory` if the in-memory tables cannot be allocated. On failure the reader is not open and holds nothing.

`Gem::Result CCpkgReader::Open(const char *fileName)`

**CCpkgReader::Close**

Closes a CCpkgReader instance. Returns `Gem::Result::NotOpen` if the reader is not open.

`Gem::Result CCpkgReader::Close()`

**CCpkgReader::GetBundleCount**

Returns the number of bundles in the package, or `0` if the reader is not open.

`uint32_t CCpkgReader::GetBundleCount() const`

**CCpkgReader::GetBundleName**

Returns the name of the bundle at `bundleIndex`, or an empty view if `bundleIndex` is out of range. Bundle names are never empty, so an empty view always means out of range. Bundle names are loaded by `Open`, so this reads no file data. The view refers to storage owned by the reader and is valid until `Close()`.

`std::string_view CCpkgReader::GetBundleName(uint32_t bundleIndex) const`

**CCpkgReader::GetAssetCount**

Returns the number of assets in the bundle at `bundleIndex`, or `0` if `bundleIndex` is out of range.

`uint32_t CCpkgReader::GetAssetCount(uint32_t bundleIndex) const`

**CCpkgReader::GetAssetType** / **CCpkgReader::GetAssetCompression**

Return the `AssetType` and `CpkgCompressionScheme` of the asset at `assetIndex` within the bundle at `bundleIndex`. Both are lookups into the bundle's fixed-stride asset table and read no payload bytes. An out-of-range index returns `AssetType::Unknown` and a zeroed `CpkgCompressionScheme` respectively. `GetAssetCompression`'s `RawSize` is what a caller allocates before calling `ReadAsset`.

```cpp
AssetType              CCpkgReader::GetAssetType(uint32_t bundleIndex, uint32_t assetIndex) const
CpkgCompressionScheme  CCpkgReader::GetAssetCompression(uint32_t bundleIndex, uint32_t assetIndex) const
```

**CCpkgReader::GetAssetName**

Returns the name of the asset at `assetIndex` within the bundle at `bundleIndex`, or an empty view if either index is out of range or the asset is unnamed. A caller that needs to tell the two apart checks the indices against `GetBundleCount`/`GetAssetCount`. Asset names are loaded with their bundle's asset name blob by `Open`, so this reads no file data. The view refers to storage owned by the reader and is valid until `Close()`.

`std::string_view CCpkgReader::GetAssetName(uint32_t bundleIndex, uint32_t assetIndex) const`

**CCpkgReader::ReadAsset**

Reads one asset payload from an open package into `data`, a caller-owned buffer whose size must be at least `GetAssetCompression(bundleIndex, assetIndex).RawSize`. Fails with `Gem::Result::InvalidArg` if either index is out of range or if `data` is smaller than that asset's `RawSize` - both checked before any read. `ReadAsset` reads the asset's on-disk bytes and decompresses internally, so the first `RawSize` bytes of `data` always receive the raw payload whatever codec the asset is stored with; `data.first(RawSize)` is the payload to pass to the [payload helpers](#payload-encoding-helpers).

`Gem::Result CCpkgReader::ReadAsset(uint32_t bundleIndex, uint32_t assetIndex, std::span<std::byte> data) const`

## Source Layout

| Path | Description |
|---|---|
| `src/Inc/CanvasPkg.h` | Public header: `CCpkgWriter`, `CCpkgReader`, `CCpkgNameDirectory`, `AssetType`, `CpkgCodec`/`CpkgCompressionScheme`, `CpkgSpan`, the header-only payload helpers (`CCpkgPayloadBuilder`, `CpkgGetPayloadHeader`, `CpkgResolveSpan`), and every `Cpkg*Data` schema |
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

1. **Undefined spatial and color conventions.** "Canvas space" is never defined - handedness, up axis, and linear units should be stated here or linked. Color spaces are likewise unstated: whether `BaseColorFactor`/`EmissiveFactor` are linear, and how a loader decides whether to decode an `Image` as sRGB or linear when a single image asset can be bound to both color and non-color roles.

2. **No mapping from a bundle to engine objects.** [CanvasPkg](#canvaspkg) says `CCpkgReader` is used to build world instances, but this document does not say how a bundle's `SceneNode` assets become an `XModel` (or several), whether a bundle may contain multiple roots (`ParentIndex = -1`), or where that loader code lives, given that CanvasPkg links no Canvas library.

3. **Degenerate mesh records are undefined.** Specify whether a `CpkgMeshData` may have zero groups, whether a group may have zero vertices, and whether `BoneIndices` without `BoneWeights` (or the reverse) is corrupt; presumably the two must be present or absent together.

4. **Cubemaps have no representation.** cpkgbuilder's manifest can name a six-face cubemap, but `CpkgImageData` is a single encoded image with no face or grouping metadata (see cpkgbuilder's [Review Concerns](../cpkgbuilder/README.md#review-concerns-open), concern 1). The answer belongs in this format: a cubemap asset type, face metadata on `CpkgImageData`, or a naming convention both documents commit to.

5. **Minor: the decompression scratch pool is designed ahead of any codec.** v1 implements only `CpkgCodec::None`, which bypasses the pool, and at 64 KiB per buffer most compressed texture payloads would spill to one-off allocations anyway. Consider deferring [Decompression Scratch Buffer](#decompression-scratch-buffer) to the design pass that picks a codec.
