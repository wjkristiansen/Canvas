# CanvasPkg

A CanvasPkg (Cpkg) is a package file containing world data - including models, geometry, materials, textures, lights, cameras, animations, (and eventually scripts, sounds, and other world data). The file is organized into bundles which contain related assets. A shared asset, such as a mesh or a texture, may be referenced by path from any bundle of any package in the same [project](#projects); a bundle's local assets reference each other by asset index. For example, a bundle might contain model data with object/bone hierarchy, meshes, materials, textures, animation clips. Other bundles might contain sky-box texture, scripts, music or sound effects.

CanvasPkg is a static library. API Success/failure codes use type `Gem::Result`.

`CCpkgReader` is used by Canvas itself to build world instances and set up scripts at load time. `CCpkgWriter` is used by `cpkgbuilder`, a separate command-line tool that builds a `.cpkg` file from a JSON package source (see [cpkgbuilder](../cpkgbuilder/README.md)).

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

Names are the only on-disk data that are not structs. The package name is stored as UTF-8 bytes with no terminator, located by `CpkgFileHeader::PackageNameOffset` and `PackageNameLength`. Every bundle and asset name is stored in a **name blob**: UTF-8 names concatenated with no length prefixes, separators, or terminators. The file has one blob holding every bundle name, and each bundle has one holding the names of its assets. A bundle or asset table entry locates its name by `NameOffset` and `NameLength` within the corresponding blob (see [Bundles](#bundles)).

Name blobs hold only the names of bundles and assets themselves. A name stored inside an asset payload, such as the path in `CpkgMaterialData::AlbedoTexture`, is not in a name blob: it is a `CpkgSpan<char>`, like any other variable-length array, with the same UTF-8 and no-terminator rules.

## Assets

Assets are the primary data block for CanvasPkg files. Each asset has an `AssetType`, a payload of bytes whose internal layout is defined by that `AssetType` (see [Package Data Types](#package-data-types)), and optionally a name. An asset's payload is stored on disk on its own, with nothing before or after it. A named asset's name is stored separately in its bundle's asset name blob, with no imposed maximum length. The bundle's asset table (see [Bundles](#bundles)) is a flat, fixed-stride array, indexed by asset index, holding the type, payload offset, name location, and compression info of every asset in the bundle. `CCpkgReader::Open` loads every bundle's asset table and asset name blob, so a name costs no file read.

Asset indices are per-bundle and span all asset types: the first asset appended to a bundle is index 0, the second is index 1, and so on, in append order, regardless of `AssetType`. `CCpkgReader::ReadAsset` and the `Cpkg*Data` index fields described below (`ParentIndex`, `NodeIndex`, ...) all use this index.

`CCpkgWriter::AppendAsset` requires an open bundle; call `BeginBundle` first, and `EndBundle` when the bundle is complete.

Bundle names must be non-empty and unique within a package. Package, bundle, and asset names never contain `/`, which separates names in a [path](#paths). Names compare byte-for-byte: case-sensitive UTF-8, with no normalization. This applies to every name match in CanvasPkg, including path resolution.

An asset's name, when it has one, is unique within its bundle. An unnamed asset is stored with an empty name. Every shared asset is named (see [Shared and Local Assets](#shared-and-local-assets)).

### Shared and Local Assets

Every asset type is one of two classes, fixed by its `AssetType`. The class decides how the asset is referenced and whether instances share it:

| Class | Asset types | Referenced by | Across instances |
|---|---|---|---|
| **Shared** | `Image`, `Material`, `MeshData`, `MeshSkin`, `Skeleton`, `BoneAnimationClip`, `Skybox` | [Asset path](#paths) only | One asset, common to every instance |
| **Local** | `TransformNode`, `Placement`, `ImportNode`, `SkeletonInstance`, `MeshInstance`, `Camera`, `Light`, `NodeAnimationClip`, `ExportList`, `WorldSettings` | Asset index within its bundle | Part of each instance |

**A shared asset's payload cannot reference other assets by index.** It references other assets only by path, so its meaning does not depend on the bundle that holds it, and any bundle of any package in its project may reference it. A bundle may hold only shared assets, such as a material library that other bundles reference. A shared asset is always named, since a path finds it by name.

**A local asset is part of its bundle's structure.** Its payload may itself reference earlier local assets in the same bundle by index. For example, `CpkgTransformNodeData::ParentIndex` references the node's parent. A local asset may be unnamed, except an [exported](#exports-and-imports) node, whose name is its export name.

`CanvasPkg.h` classifies each type with a `constexpr` helper, so `cpkgbuilder` and the loader classify them identically:

```cpp
// True for the shared asset types; false for the local ones and for Unknown.
constexpr bool CpkgIsSharedAssetType(AssetType type);
```

### Append Order

**In a bundle's asset table, every asset follows the assets it indexes.** Every local asset index must be strictly less than the index of the local asset holding it. Forward indices are not part of the format, so every index can be resolved in a single forward pass over the asset table, with no deferred fixups and no second pass. Because every index points backward, the indices in a bundle form an acyclic graph.

A producer satisfies the rule by appending each asset after every asset it indexes. These are the index fields, and the type each references:

| Field | Held by | References |
|---|---|---|
| `ParentIndex` | `TransformNode` | [node](#cpkgtransformnodedata-assettypetransformnode) or `SkeletonInstance` |
| `NodeIndex` | `Placement`, `SkeletonInstance`, `MeshInstance`, `Camera`, `Light`, `NodeAnimationClip` (per track) | node |
| `PlacementIndex` | `ImportNode` | `Placement` |
| `SkeletonInstanceIndex` | `MeshInstance` | `SkeletonInstance` |
| `NodeIndices` | `ExportList` | node |
| `ActiveCameraIndex` | `WorldSettings` | `Camera` |

The canonical order is: `Image`, `Skybox`, `Material`, `MeshData`, `Skeleton`, `MeshSkin`, `BoneAnimationClip`, the hierarchy assets, `MeshInstance`, `Camera`, `Light`, `NodeAnimationClip`, `ExportList`, `WorldSettings`. The **hierarchy assets** are `TransformNode`, `Placement`, `ImportNode`, and `SkeletonInstance`: each is relative to one other asset (a node's parent, a placement's node, an import node's placement, a skeleton instance's node), so together they form one hierarchy. Appending them together in pre-order satisfies the rule for every index among them and guarantees parent-before-child, so an asset's parent always precedes it.

`AppendAsset` takes an opaque payload and does not interpret indices out of it, so the writer cannot enforce this rule. It is a contract on whoever produces the payloads - `cpkgbuilder` orders its appends to satisfy it - and a package that violates it is malformed.

### Paths

A **path reference** is a `CpkgSpan<char>` field holding a **path**: UTF-8 names separated by `/`, resolved by name relative to the bundle that holds it, called the referencing bundle. A path reference refers to an asset or bundle that may live in another bundle or package. Each path reference field is either an **asset path** or a **bundle path**, and that fixes the forms its path may take:

| Kind | Path | Resolves to |
|---|---|---|
| Asset path | `asset` | the asset named `asset` in the referencing bundle |
| | `bundle/asset` | the asset named `asset` in bundle `bundle` of the referencing package |
| | `package/bundle/asset` | the asset named `asset` in bundle `bundle` of package `package` |
| Bundle path | `bundle` | bundle `bundle` of the referencing package |
| | `package/bundle` | bundle `bundle` of package `package` |

Path references follow these rules:

- **An empty path is null.** Each path reference field states whether it may be null. For example, a placement's bundle is required.
- **A qualified path may refer to its own scope.** A `package/bundle/asset` path that spells out the referencing package and bundle resolves to the same asset as `asset`.
- **Malformed paths.** A non-empty path containing an empty name (a leading, trailing, or doubled `/`), or more names than its kind allows, is malformed.
- **Each path reference declares its target.** An asset path declares a [shared](#shared-and-local-assets) `AssetType` and must resolve to an asset of that type. A bundle path must resolve to a bundle other than a [world bundle](#worlds).
- **Every non-null path must resolve**, within each [project](#projects) that lists its package, to a bundle or asset that exists.

The [Canvas engine spec](../../README.md) defines how path references are resolved and how an unresolved or mistyped one is handled (see [Resolving path references](#resolving-path-references)).

`CCpkgWriter::CopyBundle` copies path references unchanged. A relative path in a copied bundle therefore resolves within the package it is copied into, while a fully qualified path keeps referencing the original package.

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
    uint64_t PackageNameOffset;     // file offset of the package name
    uint32_t BundleCount;           // number of entries at BundleTableOffset
    uint32_t PackageNameLength;     // byte length of the package name; never 0
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

`Open` writes the file header immediately, followed by the package name. The header's `Magic`, `Version`, `PackageNameOffset`, and `PackageNameLength` hold their final values; the remaining fields are zero placeholders. `BeginBundle` writes nothing to the file; it starts a new in-memory bundle table entry. `EndBundle` seals the open bundle: it appends that bundle's asset table and asset name blob at the current end of file and records their locations in the bundle's in-memory table entry. `Close` appends the file-level bundle table, immediately followed by the bundle name blob, then seeks back and overwrites the file header's placeholder fields to point at them.

Every write is therefore an append, except the single fixup of the file header in `Close`. No previously written payload, table, or name blob is moved or rewritten.

A file whose writer never completed `Close` still has a zero `BundleTableOffset`, so `CCpkgReader::Open` rejects it as `Gem::Result::CorruptedData`.

## Projects

A `.cpkg` file holds exactly one package, so a path naming another package references another file. A **project** is a set of packages used together, declared by its **project manifest**: a JSON file that names the project and lists its packages, each by name and file location. Its shape is defined by [project.schema.json](project.schema.json).

```json
{
  "formatVersion": 1,
  "name": "Game",
  "packages": [
    { "name": "Levels",     "location": "levels.cpkg" },
    { "name": "Characters", "location": "../Shared/characters.cpkg" },
    { "name": "Textures",   "location": "D:/Assets/textures.cpkg" }
  ]
}
```

| Field | Meaning |
|---|---|
| `formatVersion` | Manifest format version. This spec defines version `1`. |
| `name` | The project's name; non-empty. |
| `packages` | The project's packages, one entry per package. |
| `packages[].name` | The package's declared name (`CCpkgReader::GetPackageName`); the file at `location` must declare the same name. |
| `packages[].location` | File path of the package's `.cpkg` file, using `/` as its separator: absolute, or relative to the directory holding the manifest. |

Projects follow these rules:

- **A project has one manifest, and the manifest is self-contained.** A project's packages are exactly those its manifest lists.
- **Package names are unique within a project.** No two entries of a manifest share a name.
- **Paths resolve within the project.** The `package` name in a [path](#paths) names a package of the project. A path that names no package, such as `bundle/asset`, resolves within its own package.
- **A project lists every package its packages reference.** Every package named by a path in any listed package is itself listed. This includes packages that only other packages reference: a project that lists a shared characters package also lists every package the characters package's paths name.
- **Projects may share packages.** A texture library may be listed by any number of projects. Each project resolves the shared package's paths among its own packages, so the projects that share a package list, under the names that package's paths use, the packages those paths reference.

How the engine opens a project, and how it handles a package file that is missing or declares a different name than its entry, is defined by the [Canvas engine spec](../../README.md).

## Instantiation Model

This section describes, at a high level, how the content of a package becomes objects in a Canvas world. The [Canvas engine spec](../../README.md) defines the actual mapping to engine objects (scene-graph nodes, `XModel`, GPU resources, physics bodies). What this section defines is what a package *declares*, so that any conforming engine instantiates it the same way.

**The package declares structural roles; the engine user supplies gameplay meaning.** A package says what its content is and how the engine may treat it: which bundles a bundle places, which nodes it exports, whether placed content may move, and (later) what collision or scripts are attached.

### Instances

The engine can **instantiate** any bundle any number of times, and each resulting **instance** is independent of the others. An instance is built from the bundle's [local assets](#shared-and-local-assets): its hierarchy of nodes, placements, and skeleton instances, and the attachments and clips that go with it. A bundle that holds only shared assets, such as an image library, contributes no structure: other bundles reference its assets by path. How the engine represents an instance is defined by the [Canvas engine spec](../../README.md).

The engine instantiates a bundle as a nested instance of a bundle that places it (see [Placements](#placements)), as a [world bundle](#worlds) or one of its [world regions](#world-regions), or when the engine user requests one at runtime, for example to spawn an enemy. Nothing in a bundle records how it is used, and one bundle may be both placed and used as a region.

A placed bundle typically describes one thing, such as a goblin, a chair, or a tree. Each instance has its own nodes; the [shared assets](#shared-and-local-assets) they reference, which may live in any bundle of any package in the project, are common to every instance. A bundle may itself place other bundles.

Each instance has an **instance transform**, which depends on what the instance is for: a nested instance uses its placement transform (see [Placements](#placements)), a world bundle or world region uses the world origin, and the engine user supplies one for an instance it requests at runtime. A `TransformNode` with `ParentIndex = -1` is relative to its instance's instance transform, so a bundle's nodes may form several trees and an instance still moves as one unit.

### Placements

A bundle may **place** other bundles. Each **placement** (`AssetType::Placement`) it holds declares **nested instances** of one **placed bundle** within the bundle's space. A placement declares one nested instance per entry in its `Transforms` array, all with the placement's declared [mobility](#mobility). When the engine instantiates a bundle, it also instantiates each placed bundle once for each nested instance the bundle's placements declare.

Each entry is relative to the [node](#cpkgtransformnodedata-assettypetransformnode) the placement attaches to, or to the instance transform of the bundle holding the placement when `NodeIndex` is `-1`. That entry, composed with what it is relative to, is the nested instance's **placement transform**, which serves as its instance transform. A placement references the placed bundle by [bundle path](#paths): `bundle` for a bundle in the same package, or `package/bundle` for a bundle in another package, where `package` is that package's declared name (`CCpkgReader::GetPackageName`). Placed bundles are therefore shared across packages: world regions in several packages can each place a goblin bundle stored once in a shared characters package.

```cpp
// Draft schema.
struct CpkgPlacementData
{
    int32_t                  NodeIndex = -1;                   // asset index of the node this placement attaches to; -1 = instance transform
    CpkgMobility             Mobility  = CpkgMobility::Static; // mobility of each nested instance's instance transform
    CpkgSpan<char>           Bundle;                           // bundle path to the placed bundle; required
    CpkgSpan<CpkgTransform>  Transforms;                       // one nested instance per entry, relative to NodeIndex; at least one
};
```

A placement with one entry declares a single nested instance; a placement with many declares a batch that shares one bundle path, one mobility, and one node. For example:

- **A hillside of flowers** is one placement in the region, with `NodeIndex = -1` and one entry per flower in region space.
- **A flower shop** with individual flowers in pots on shelves uses one placement per flower, each attached to its pot's node, so every flower moves with its pot.
- **A lamp hung from a beam** is a one-entry placement attached to the beam node, with the entry offsetting the lamp from the beam.

A nested instance in a batch has no node of its own in the bundle holding the placement, so nothing can attach to one nested instance of a batch individually; an [import node](#exports-and-imports) of a batch attaches content to every nested instance. Content that must be addressed individually, such as a trap door and the trigger placed beside it, uses a `TransformNode` per nested instance, with each placement attached to it. A node may carry any number of placements.

The bundles that placements reference must form an acyclic graph: no bundle places itself, directly or through other bundles.

### Exports and Imports

A bundle may **export** nodes, which lets a bundle that places it attach content to those nodes in each nested instance. For example, a goblin bundle exports `Hand_R`, a node [parented to its hand bone](#cpkgtransformnodedata-assettypetransformnode), and a region that places the goblin bundle attaches a sword there.

**Exports.** A bundle's [`ExportList`](#cpkgexportlistdata-assettypeexportlist) lists the nodes it exports, which are nodes of that bundle. Each exported node is named, and its name is its **export name**. A bundle's exports are exactly the nodes its `ExportList` lists, and a bundle holds at most one `ExportList`.

**Imports.** An [`ImportNode`](#cpkgimportnodedata-assettypeimportnode) is a node in a bundle that represents a node exported by a bundle it places. It names one of its bundle's placements (`PlacementIndex`) and one export of that placement's placed bundle (`ExportName`). Other assets reference the import node by asset index like any other node: a placement may attach to it, and a `TransformNode` may use it as its parent. Its `LocalTransform` offsets it from the exported node. In the goblin example, the region holds an import node naming the goblin placement and `Hand_R`, the sword placement attaches to that import node, and the import node's `LocalTransform` offsets the grip.

`ExportName` is a name, not a [path](#paths): it must name an export of the referenced placement's placed bundle. An `ExportList`'s node indices are resolved within its own bundle, so no asset index crosses a bundle boundary.

**Re-exporting.** A bundle may export an import node, which passes an export of a bundle it places through to any bundle that places it. For example, a goblin rider bundle that places a goblin exports its `Hand_R` import node, so a region that places the rider can arm the goblin in turn.

**Imports from a batch.** An import node represents one node in each nested instance its placement declares. A `TransformNode` represents one node, and a placement declares its entry count times the number of nodes its `NodeIndex` represents (one for `-1`). Only a placement may attach to an import node that represents more than one node; it declares nested instances at each of them, once per entry. If one placement in a region declares 50 nested instances of the goblin bundle, a one-entry sword placement attached to their `Hand_R` import node declares 50 nested instances of the sword bundle, one in each hand. Every other reference to an import node, such as a `TransformNode`'s parent, a skeleton instance, mesh instance, camera, or light, a clip track, or an `ExportList` entry, requires one that represents exactly one node.

### World Regions

A **world region** is a bundle that a [world](#worlds) lists in its `Regions`, together with the region's streaming bounds. A region's [instance transform](#instances) is the world origin, so its content is positioned in world space. A region typically holds placements, and may also hold its own nodes, such as terrain. Content that exists only once in the world (a cathedral, a cliff face) is a bundle placed once. A large world can be divided into many world regions; a small game's entire level can live in its world bundle with no regions at all.

### Worlds

A **world** is the top-level unit a game loads: one place, such as a level, a map, or an open world, made of always-present content and world regions. A world is declared by a **world bundle**, a bundle that holds exactly one `AssetType::WorldSettings` asset. A bundle holding a `WorldSettings` asset is always a world bundle.

Besides its `WorldSettings` asset, a world bundle may hold assets of any type: the world's always-present content, such as `TransformNode` hierarchies, placements, and shared assets such as the world's `Skybox` assets.

A world bundle's [instance transform](#instances) is the world origin, so its content is positioned in world space, as a region's is. A world bundle is used only as a world, so neither a placement nor a region list references one. How and when a world and its regions are instantiated is defined by the [Canvas engine spec](../../README.md).

```cpp
// Draft schema.
struct CpkgWorldRegionRef
{
    CpkgSpan<char>  Region;           // bundle path to the region's bundle; required
    CpkgAABB        StreamingBounds;  // world space; must be well-formed (see below)
};

struct CpkgWorldSettingsData
{
    CpkgSpan<CpkgWorldRegionRef>  Regions;
    CpkgSpan<CpkgSpan<char>>      Skyboxes;                    // asset paths to Skybox assets; each required
    int32_t                       DefaultSkybox         = -1;  // position in Skyboxes (not an asset index); -1 = none
    int32_t                       ActiveCameraIndex     = -1;  // asset index of a Camera in this bundle; -1 = none
    float                         BackgroundColor[4];          // linear RGBA; the background while no skybox is in effect
};
```

**Regions** lists the world's regions. `StreamingBounds` declares the world-space area the region covers, authored like a streaming grid cell, and it satisfies `Min[i] <= Max[i]` on every axis. The bounds say where a region's content is; when a region is loaded is not part of the package. A region may belong to several worlds, and a package never records which worlds use its bundles.

**Skyboxes** lists every `Skybox` asset the world uses, such as day, dusk, and night skies. Each entry is an asset path, so a skybox may live in the world bundle or in a shared sky bundle that several worlds use. `DefaultSkybox` declares the skybox in effect when the world starts, or none. `BackgroundColor` declares the background while no skybox is in effect.

**ActiveCameraIndex** declares the world's initial camera: it references a `Camera` of the world bundle. A world with no authored camera, such as one whose game creates a player camera at runtime, leaves it at `-1`.

### Mobility

**Mobility** tells the engine which parts of the content may change after instantiation. Each [`TransformNode`](#cpkgtransformnodedata-assettypetransformnode) declares the mobility of its local transform and its attachments, and each [placement](#placements) declares the mobility of the instance transforms of the nested instances it declares:

```cpp
enum class CpkgMobility : uint32_t
{
    Static     = 0,   // transform and attributes are fixed
    Stationary = 1,   // transform is fixed; attributes may change
    Movable    = 2,   // transform and attributes may change
};
```

A node's transform is its local transform, and its attributes are those of the assets attached to it, such as a light's color or a skeleton instance's pose. An instance's transform is its instance transform.

| Mobility | Example | What the engine may do |
|---|---|---|
| `Static` | A wall, a boulder, terrain | Bake lighting and shadows, merge or batch geometry, add it to static acceleration structures, and skip it in per-frame updates |
| `Stationary` | A flickering lamp, a flag waving on its skeleton | Add it to static spatial structures, using bounds that enclose any skeleton animation, and skip transform updates for it |
| `Movable` | A door's hinge, a cart, a goblin | Update it every frame |

The two declarations describe different things. A placement's mobility describes how the placing bundle uses the placed bundle: one chair bundle can be placed `Static` as decor in one room and `Movable` as a throwable prop in another. A node's mobility describes motion within its own bundle wherever that bundle is placed: a door bundle declares its hinge node `Movable`, so every door swings, while the door frame stays `Static`.

**Mobility is inherited from parent to child.** Mobility values are ordered `Static` < `Stationary` < `Movable`, and each node and instance has an **effective mobility**, the most mobile of its declared mobility and its parent's effective mobility:

- A `TransformNode` whose `ParentIndex` references a node has that node as its parent. One parented to a bone has, as its parent, the node its skeleton instance attaches to. One with `ParentIndex = -1` has its instance as its parent.
- A nested instance has, as its parent, the node its placement attaches to, or the instance holding the placement when `NodeIndex` is `-1`. Its declared mobility is its placement's.
- A world bundle or world region instance has the effective mobility `Static`, because its instance transform is the world origin.
- An instance the engine user requests at runtime has the mobility the engine user supplies along with its instance transform.
- An [import node](#exports-and-imports) has the effective mobility of the exported node it represents, so a sword attached to a `Movable` goblin's hand is effectively `Movable`.

Mobility passes only from parent to child. A region's `Movable` camera rig makes the nodes below it effectively `Movable` and leaves the terrain beside it `Static`, so the engine can bake the terrain. A house placed `Movable` on a ship moves with the ship, and so does the `Static` furniture placed inside the house bundle. A `Static` windmill placement whose blade node is declared `Movable` bakes the building and updates only the blades. A bundle can therefore be placed anywhere, regardless of the mobility its nodes and nested placements declare.

### Resolving path references

[Path references](#paths), such as a placement's bundle, a mesh instance's mesh, or a world's regions and skyboxes, and [export names](#exports-and-imports) are resolved by engine-side code built on `CCpkgReader` and its [name directories](#name-directories), as defined by the [Canvas engine spec](../../README.md). A path names a package by its declared name, among the packages of a [project](#projects).

## Package Data Types

`CCpkgWriter`/`CCpkgReader` move opaque, named, typed byte payloads. This section defines what those bytes mean for each `AssetType`.

**Every payload has the same shape: a header struct at offset 0, followed by a data area holding the payload's variable-length arrays.** The `AssetType` determines the header struct (`CpkgMeshData` for `AssetType::MeshData`, and so on). The header refers to each array through a `CpkgSpan<T>` member, and an array's elements may themselves hold spans. An asset type with no variable-length data (`TransformNode`, `Camera`, `Light`) has a payload that is exactly its header, with no data area. The structs in this section follow the [On-Disk Encoding](#on-disk-encoding) rules, and [Payload Layout](#payload-layout) defines spans and the data area.

A relationship between assets is stored according to the referenced asset's [class](#shared-and-local-assets). A field referencing a shared asset - the mesh a mesh instance draws, the skin that deforms it, the skeleton a skeleton instance poses, the material a mesh group draws with, an image a material samples - is an asset path (see [Paths](#paths)). A field referencing a local asset - a node's parent, the node an asset attaches to, the skeleton instance whose bone is a node's parent - is the intra-bundle asset index of that asset. An import node names an export of a placed bundle (see [Exports and Imports](#exports-and-imports)). Records correspond to the descriptors in `CanvasCore.h`/`CanvasGfx.h`: `MeshDataDesc`, `AnimationClipDesc`, `MeshSkinDesc`, and so on.

### Conventions

Every payload stores values in the Canvas engine's own conventions, so they need no conversion between package and engine. Converting from a source format's conventions is the writer's job (for example `cpkgbuilder` and its importers).

**Canvas space** is the engine's world and scene-graph coordinate system (see [Coordinate System and Local Basis](../../README.md#coordinate-system-and-local-basis)):

- Right-handed, with +Z up.
- Linear units are meters. This covers translations, positions, bounds, light `Range`, camera clip distances, and displacement `MapScale`/`MapBias`.
- Matrices use the row-vector convention, `v' = v * M`, with translation in row 3 (see [Row-Vector Matrix Convention](../../README.md#row-vector-matrix-convention)).
- Rotations are unit quaternions stored XYZW.
- A camera or light faces along the local +X axis of the node it attaches to, with +Y to its left and +Z up.
- A triangle's front face has counter-clockwise winding when viewed from the side its normal points toward.
- `CpkgVertexTangent::XYZW[3]` is +1 or -1, and the bitangent is `XYZW[3] * cross(N, T)`.
- Angles are radians and times are seconds.

**Transforms.** Every local transform a payload stores is a `CpkgTransform`. `CpkgTransformNodeData`, `CpkgImportNodeData`, `CpkgPlacementData`, and `CpkgKeyframe` all use it.

```cpp
struct CpkgTransform
{
    float Scale[3]       = { 1.0f, 1.0f, 1.0f };        // xyz
    float Rotation[4]    = { 0.0f, 0.0f, 0.0f, 1.0f };  // unit quaternion, XYZW
    float Translation[3] = { 0.0f, 0.0f, 0.0f };        // xyz, meters
};
```

**Colors.** Every color stored as `float` is linear RGB with straight (non-premultiplied) alpha where alpha is meaningful. This covers `BaseColorFactor`, `EmissiveFactor` (its alpha is unused), and `CpkgLightData::Color`. These values are not sRGB-encoded.

**Image color space depends on the binding role, not on the image.** `CpkgImageData` stores no color space. The field that references an image declares how its pixel data is interpreted:

| Role | Texture field | Interpretation |
|---|---|---|
| Albedo | `AlbedoTexture` | sRGB-encoded color |
| Emissive | `EmissiveTexture` | sRGB-encoded color |
| Normal | `NormalTexture` | Linear data |
| Roughness | `RoughnessTexture` | Linear data |
| Metallic | `MetallicTexture` | Linear data |
| Ambient occlusion | `AmbientOcclusionTexture` | Linear data |
| Displacement | `Displacement.DisplacementMap` | Linear data |
| Skybox | `CpkgSkyboxData::Image` | sRGB-encoded color |

The sRGB rule covers integer-valued pixel data only. Floating-point pixel data, such as a floating-point TIFF, is always linear, in every role.

One `Image` asset may be referenced in both a color role and a linear role, and each reference has its own role's interpretation. A writer that wants a single interpretation stores two `Image` assets.

### AssetType

```cpp
enum class AssetType : uint32_t
{
    Unknown           = 0,   // sentinel; never assigned to a real asset
    TransformNode     = 1,
    MeshData          = 2,
    Material          = 3,
    Image             = 4,
    NodeAnimationClip = 5,
    MeshSkin          = 6,
    Camera            = 7,
    Light             = 8,
    Placement         = 9,   // see Placements
    Skybox            = 10,
    WorldSettings     = 11,  // see Worlds
    MeshInstance      = 12,
    ImportNode        = 13,  // see Exports and Imports
    ExportList        = 14,  // see Exports and Imports
    Skeleton          = 15,  // see CpkgSkeletonData
    SkeletonInstance  = 16,
    BoneAnimationClip = 17,
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

A payload that violates any of these rules is malformed, and `CpkgResolveSpan` reports such a span as `Gem::Result::CorruptedData`.

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

Parallel arrays must have equal counts: each per-vertex stream with its group's `Positions`, and `InvBindPoses` with `Bones`.

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

The helpers check only structure. Schema rules - parallel counts, the streams each topology requires, asset index ordering - are defined by each schema below.

### CpkgTransformNodeData (AssetType::TransformNode)

One node of a bundle's node hierarchy: a parent and a local transform. `ParentIndex` links a node to its parent by asset index. The parent is another node, or one bone of a [skeleton instance](#cpkgskeletoninstancedata-assettypeskeletoninstance).

Fixed size: the payload is one `CpkgTransformNodeData`.

```cpp
struct CpkgTransformNodeData
{
    int32_t        ParentIndex = -1;                    // asset index of the parent node or SkeletonInstance; -1 = instance transform
    int32_t        ParentBone  = -1;                    // bone index of the parent bone when ParentIndex is a SkeletonInstance; otherwise -1
    CpkgMobility   Mobility    = CpkgMobility::Static;  // see Mobility; inherited by descendants
    CpkgTransform  LocalTransform;                      // relative to the parent node or bone (see Conventions)
};
```

`Mobility` declares whether the node's local transform and its attachments may change after instantiation. The node's effective mobility is the most mobile of its declared `Mobility` and its parent's effective mobility, so `Static`, the default, makes the node exactly as mobile as its parent (see [Mobility](#mobility)).

When `ParentIndex` references a `SkeletonInstance`, `ParentBone` is less than the bone count of its `Skeleton`, and the node follows that bone in the skeleton instance's pose. When `ParentIndex` references a node or is `-1`, `ParentBone` is `-1`.

**A node parented to a bone attaches content to that bone**, such as a sword in a hand or a helmet on a head. Placements and other attachments attach to the node, and an `ExportList` may export it, which lets a bundle that places this one attach content to the bone (see [Exports and Imports](#exports-and-imports)). Only bones that content attaches to need such a node.

A node's name is the optional `name` it was appended with. A node parented to a bone has its own name, separate from the bone's name in the skeleton; when the node is exported, its own name is its export name.

A **node** is a `TransformNode` or an [`ImportNode`](#cpkgimportnodedata-assettypeimportnode). `SkeletonInstance`, `MeshInstance`, `Camera`, `Light`, and [`Placement`](#placements) assets each attach to one node through a `NodeIndex` field. A node may have any number of attachments, of any types, or none; a node with none is a pure transform, such as a pivot. `NodeIndex` is required, except that a placement's may be `-1` to place relative to the instance transform. Like every asset index, a `NodeIndex` references an earlier asset, so attachments are appended after the nodes they attach to.

### CpkgImportNodeData (AssetType::ImportNode)

A node that represents a node exported by a placed bundle (see [Exports and Imports](#exports-and-imports)).

The payload is a `CpkgImportNodeData` header followed by a data area holding the export name.

```cpp
struct CpkgImportNodeData
{
    int32_t         PlacementIndex = -1;  // asset index of the Placement whose nested instances export the node; required
    uint32_t        Padding        = 0;
    CpkgSpan<char>  ExportName;           // export name in the placed bundle's ExportList; required
    CpkgTransform   LocalTransform;       // relative to the exported node (see Conventions)
};
```

### CpkgExportListData (AssetType::ExportList)

Lists the nodes a bundle exports (see [Exports and Imports](#exports-and-imports)). A bundle holds at most one `ExportList`.

The payload is a `CpkgExportListData` header followed by a data area holding the node indices.

```cpp
struct CpkgExportListData
{
    CpkgSpan<int32_t>  NodeIndices;  // asset indices of exported nodes; each named
};
```

Each listed node appears once, and each listed `ImportNode` represents exactly one node. A node's export name is its asset name, so export names are unique within the bundle.

### CpkgSkeletonData (AssetType::Skeleton)

A skeleton: a hierarchy of bones, each with a name, a parent bone, and a rest transform. A skeleton is a shared asset, used by any number of bundles much as an image is. A skeleton's bones are data within the asset, not nodes. A bundle poses a skeleton through a [`SkeletonInstance`](#cpkgskeletoninstancedata-assettypeskeletoninstance), and a [`TransformNode`](#cpkgtransformnodedata-assettypetransformnode) attaches to a bone by naming the skeleton instance as its parent. Skins, clips, and transform nodes reference a skeleton's bones by **bone index**, a position in its `Bones` array (see [`CpkgMeshSkinData`](#cpkgmeshskindata-assettypemeshskin) and [`CpkgBoneAnimClipData`](#cpkgboneanimclipdata-assettypeboneanimationclip)).

Variable size. The payload's header is one `CpkgSkeletonData`. Its `Bones` span refers to an array of `CpkgSkeletonBone` in the data area, and each bone's `Name` span refers to that bone's name:

```cpp
struct CpkgSkeletonBone
{
    int32_t         ParentBone = -1;  // bone index of the parent bone; -1 = root
    uint32_t        Padding    = 0;
    CpkgSpan<char>  Name;             // empty = unnamed; a non-empty name is unique within the skeleton
    CpkgTransform   RestTransform;    // relative to the parent bone, or for a root to the skeleton instance's node
};

struct CpkgSkeletonData
{
    CpkgSpan<CpkgSkeletonBone>  Bones;  // at least one
};
```

The skeleton's bone count is `Bones.Count`. Every bone's `ParentBone` is less than its own bone index, so a bone's parent always precedes it and the bones form one or more trees. Bone names let the engine user and tools find bones by name; skins, clips, and transform nodes reference bones by bone index.

### CpkgSkeletonInstanceData (AssetType::SkeletonInstance)

Describes a skeleton attached to a node: the node carries a pose of a shared `Skeleton`, with its root bones relative to the node. Each instance of the bundle has its own skeleton instance, and so its own pose. Skinned [mesh instances](#cpkgmeshinstancedata-assettypemeshinstance) bind to a skeleton instance, and [transform nodes](#cpkgtransformnodedata-assettypetransformnode) parented to its bones follow them. Several mesh instances, such as a character's body and armor, may bind to one skeleton instance and share its pose.

The payload is a `CpkgSkeletonInstanceData` header followed by a data area holding the skeleton path.

```cpp
struct CpkgSkeletonInstanceData
{
    int32_t         NodeIndex = -1;  // asset index of the node this skeleton instance attaches to; required
    uint32_t        Padding   = 0;
    CpkgSpan<char>  Skeleton;        // asset path to a Skeleton; required
};
```

The clips a skeleton instance may play are the [bone animation clips](#cpkgboneanimclipdata-assettypeboneanimationclip) built for its `Skeleton`. Which clip plays, and when, is supplied by the engine user.


### CpkgMeshInstanceData (AssetType::MeshInstance)

Describes mesh data attached to a node: the node it attaches to draws a shared `MeshData`, optionally deformed by a shared `MeshSkin`. Each instance of the bundle has its own mesh instance, and all of them draw the same mesh.

The payload is a `CpkgMeshInstanceData` header followed by a data area holding the mesh and skin paths.

```cpp
struct CpkgMeshInstanceData
{
    int32_t         NodeIndex             = -1;  // asset index of the node this mesh instance attaches to; required
    int32_t         SkeletonInstanceIndex = -1;  // asset index of the SkeletonInstance that poses the skin; -1 = not skinned
    CpkgSpan<char>  MeshData;                    // asset path to a MeshData; required
    CpkgSpan<char>  Skin;                        // asset path to a MeshSkin; null = not skinned
};
```

**A skinned mesh instance binds its skin to a skeleton instance.** When `Skin` is not null, `SkeletonInstanceIndex` references a [`SkeletonInstance`](#cpkgskeletoninstancedata-assettypeskeletoninstance) whose `Skeleton` path resolves to the same skeleton as the skin's `Skeleton` path, and the skin's bones are that skeleton instance's bones. When `Skin` is null, `SkeletonInstanceIndex` is `-1`.

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
    CpkgSpan<char>                  Material;            // asset path to a Material; empty = none
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
    CpkgAABB                        LocalBounds;         // empty sentinel = no bounds declared; see below
    uint32_t                        Padding = 0;
    CpkgSpan<CpkgMeshDataGroup>     Groups;
};
```

**A mesh has at least one group, and every group has at least one vertex.** `Groups.Count` and each group's `Positions.Count` are nonzero, matching `CreateMeshData`, which rejects a zero `GroupCount` or `VertexCount`. Every non-empty stream's `Count` must equal the group's `Positions.Count`. A mesh is skinned in full or not at all: either every group carries both `BoneIndices` and `BoneWeights`, or no group carries either. A payload that breaks any of these rules is malformed.

**`LocalBounds` is either empty or well-formed.** Empty is the exact sentinel `Min = {+FLT_MAX, +FLT_MAX, +FLT_MAX}`, `Max = {-FLT_MAX, -FLT_MAX, -FLT_MAX}`: `CpkgAABB`'s default, and the state `Canvas::Math::AABB::IsEmpty` recognizes. It means the mesh declares no bounds. Otherwise `Min[i] <= Max[i]` must hold on every axis, which also excludes NaN. Any other box, including one that is inside-out on some axes or inverted without matching the sentinel, is not empty, and is malformed.

**Which streams are required depends on `Topology`**, matching what the backend enforces in `CreateMeshData`: `TriangleList` requires `Positions` and `Normals`; `PatchList4CP` requires `Positions`, `Normals`, and `UV0` (per-control-point map UVs, and the base direction along which displacement extends each control point). `Tangents`, `BoneIndices`, and `BoneWeights` are optional under either topology.

A skinned mesh holds no asset index, so like any shared asset it may live in any bundle. `BoneIndices` references bones without naming an asset: it is a palette-relative index into the `Bones` array of the [`MeshSkin`](#cpkgmeshskindata-assettypemeshskin) that deforms the mesh, and by the ordering correspondence into its `InvBindPoses`. This matches the runtime skinning convention. An unused influence is encoded as weight 0 in `BoneWeights`, with its `BoneIndices` slot ignored; there is no reserved "none" bone-index sentinel.

**A mesh drawn with a `MeshSkin` carries bone streams in every group.** The mesh of a mesh instance whose `Skin` is not null must be a skinned mesh in the sense above, and each `BoneIndices` entry of an influence with a nonzero weight must be less than the skin's palette size. A rigid part of a skinned model is weighted 1.0 to a single bone; a separate rigid object is its own unskinned mesh, drawn by a mesh instance attached to a node parented to the bone. A mesh instance whose skin deforms a mesh that lacks bone streams is malformed.

### CpkgMaterialData (AssetType::Material)

Covers the material properties `XGfxMaterial` exposes today: texture-role bindings, the three PBR factors, and the optional displacement extension.

The payload is a `CpkgMaterialData` header followed by a data area holding its texture paths. Each texture field is an asset path to an `Image`; an empty path means the role has no texture.

```cpp
struct CpkgDisplacementData
{
    CpkgSpan<char> DisplacementMap;   // asset path to an Image; required if present
    float   MapScale          = 1.0f;
    float   MapBias           = 0.0f;
    float   MinTessFactor     = 2.0f;
    float   MaxTessFactor     = 32.0f;
    float   DistanceLodScale  = 10.0f;
    float   CurvatureLodScale = 0.5f;
};

struct CpkgMaterialData
{
    float  BaseColorFactor[4];      // linear RGBA
    float  EmissiveFactor[4];       // linear RGB, A unused
    float  RoughMetalAOFactor[4];   // R=Roughness, G=Metallic, B=AmbientOcclusion, A=spare

    CpkgSpan<char> AlbedoTexture;            // asset path to an Image; empty = none, one per MaterialLayerRole
    CpkgSpan<char> NormalTexture;
    CpkgSpan<char> RoughnessTexture;
    CpkgSpan<char> MetallicTexture;
    CpkgSpan<char> AmbientOcclusionTexture;
    CpkgSpan<char> EmissiveTexture;

    uint32_t               HasDisplacement = 0;  // 0 or 1
    uint32_t               Padding         = 0;
    CpkgDisplacementData   Displacement;         // meaningful only when HasDisplacement is 1
};
```

The `Displacement` block is always present, even when `HasDisplacement` is `0`, so the header has a single fixed layout. When `HasDisplacement` is `0`, `Displacement.DisplacementMap` is empty and a reader ignores the block's other fields.

### CpkgImageData (AssetType::Image)

An image asset is a texture: a 1D, 2D, 3D, or cube texture with optional array slices and a stored mip chain. Its pixels are held as encoded 2D images, each stored verbatim, exactly as read from the source. `Encoding` identifies how they are encoded. CanvasPkg neither decodes nor re-encodes the bytes.

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

enum class CpkgImageDimension : uint32_t   // values match GfxSurfaceDimension
{
    Texture1D = 0,
    Texture2D = 1,
    Texture3D = 2,
    Cube      = 3,
};

struct CpkgImageData
{
    CpkgImageEncoding            Encoding;
    CpkgImageDimension           Dimension;
    uint32_t                     Width;      // of mip 0
    uint32_t                     Height;     // of mip 0; 1 for Texture1D
    uint32_t                     Depth;      // of mip 0; 1 unless Texture3D
    uint32_t                     ArraySize;  // array slices; a multiple of 6 for Cube; 1 for Texture3D
    uint32_t                     MipLevels;  // mip levels stored; at least 1
    uint32_t                     Padding = 0;
    CpkgSpan<CpkgSpan<uint8_t>>  Images;     // encoded 2D images, in the order below
};
```

The shape fields mirror `GfxSurfaceDesc`. `ArraySize` counts 2D slices, as `GfxSurfaceDesc::ArraySize` does: a single cubemap has `ArraySize == 6`, and a cube array of `N` cubes has `ArraySize == 6 * N`. Mip `m` measures `max(1, Width >> m)` by `max(1, Height >> m)` by `max(1, Depth >> m)`. `MipLevels` may be less than the full chain.

Each `Images` entry is one encoded 2D image, ordered as follows:

- **`Texture1D`, `Texture2D`, `Cube`**: one entry per subresource, `ArraySize * MipLevels` entries in all, in subresource order: entry `arraySlice * MipLevels + mip`. This is the order in which `XGfxDevice::UploadTextureRegion` numbers subresources, so entry `i` uploads to subresource `i`. Within each cube, the slices are the faces +X, -X, +Y, -Y, +Z, -Z, in that order. A `Texture1D` entry is an image of height 1.
- **`Texture3D`**: mip 0's depth slices, front to back, then mip 1's, and so on. Mip `m` contributes `max(1, Depth >> m)` entries.
- **Container encodings** (such as DDS or KTX, once added) carry their own layout. For a container encoding, `Images` holds exactly one entry, the whole container file, and the header's shape fields equal the container's.

An image whose `Images.Count` differs from the count these rules give is malformed. Cube faces are in cube-texture space; a skybox's `Orientation` rotates them into Canvas space.

Every entry uses the header's `Encoding` and the same pixel format, and decodes to the size of its mip. CanvasPkg cannot check these without decoding, so they are a contract on the writer.

Every image a `CpkgMaterialData` texture path references is a `Texture2D` image with `ArraySize == 1`, and the image `CpkgSkyboxData::Image` references is a `Cube` image with `ArraySize == 6`. A path must reference an image of that shape, just as every asset path must reference an asset of its declared type. The color-space rules in [Conventions](#conventions) apply to each entry.

`Encoding` and `Compression.Codec` describe separate layers. `Encoding` describes the encoded images and is opaque to CanvasPkg, which stores and returns those bytes untouched. `Compression.Codec` is transport compression applied over the whole payload, and `ReadAsset` always reverses it before returning (see [Compression](#compression)). Any `CpkgCodec` is valid for any `Encoding`.

Decoded, GPU-ready texture data (decoded pixels in a GPU tile layout) is a future design pass - see [Future Directions](#future-directions).

### CpkgSkyboxData (AssetType::Skybox)

A sky drawn behind all geometry, sampled by view direction. A [world](#worlds) lists the skyboxes it uses by path. The fields correspond to the skybox fields of `GfxBackgroundDesc`.

The payload is a `CpkgSkyboxData` header followed by a data area holding the `Image` path.

```cpp
struct CpkgSkyboxData
{
    CpkgSpan<char>  Image;               // asset path to a Cube Image; required
    float           Orientation[4];      // unit quaternion, XYZW: rotates cube-texture space into Canvas space
    float           Intensity  = 1.0f;   // linear multiplier on the sample
    uint32_t        Padding    = 0;
};
```

`Orientation` lets a cubemap authored in any basis appear upright in Canvas space; it is `GfxBackgroundDesc::Orientation`. The image is a color role (see [Conventions](#conventions)).

### CpkgNodeAnimClipData (AssetType::NodeAnimationClip)

Corresponds to `AnimationClipDesc`/`AnimationTrackDesc`. A node animation clip animates nodes of its own bundle, such as a door swinging on its hinge node or a windmill turning its blades. Each track targets one node by asset index. The clips that animate a bundle's nodes are the `NodeAnimationClip` assets that bundle holds. A skeleton's bones are animated by [bone animation clips](#cpkgboneanimclipdata-assettypeboneanimationclip).

Variable size. The payload's header is one `CpkgNodeAnimClipData`. Its `Tracks` span refers to an array of `CpkgNodeAnimTrack` in the data area, and each track's `Keyframes` span refers to that track's keyframes:

```cpp
struct CpkgKeyframe
{
    float          Time;       // seconds from clip start
    CpkgTransform  Transform;  // the node's or bone's local transform at Time
};

struct CpkgNodeAnimTrack
{
    int32_t                 NodeIndex = -1;  // asset index of the node this track animates
    uint32_t                Padding   = 0;
    CpkgSpan<CpkgKeyframe>  Keyframes;       // sorted ascending by Time
};

struct CpkgNodeAnimClipData
{
    float                       Duration;    // seconds
    uint32_t                    Padding = 0;
    CpkgSpan<CpkgNodeAnimTrack> Tracks;
};
```

### CpkgBoneAnimClipData (AssetType::BoneAnimationClip)

Corresponds to `AnimationClipDesc`/`AnimationTrackDesc`. A bone animation clip animates the bones of a shared [`Skeleton`](#cpkgskeletondata-assettypeskeleton), such as a walk cycle or an attack. It is a shared asset: it declares the skeleton it animates, and each track targets one bone by bone index, so every skeleton instance of that skeleton can play it. A skeleton's clips typically live in the skeleton's bundle.

Variable size. The payload's header is one `CpkgBoneAnimClipData`. Its `Skeleton` span refers to the skeleton path and its `Tracks` span to an array of `CpkgBoneAnimTrack` in the data area, and each track's `Keyframes` span refers to that track's keyframes:

```cpp
struct CpkgBoneAnimTrack
{
    uint32_t                BoneIndex = 0;  // bone index of the bone this track animates
    uint32_t                Padding   = 0;
    CpkgSpan<CpkgKeyframe>  Keyframes;      // sorted ascending by Time
};

struct CpkgBoneAnimClipData
{
    float                       Duration;    // seconds
    uint32_t                    Padding = 0;
    CpkgSpan<char>              Skeleton;    // asset path to the Skeleton this clip animates; required
    CpkgSpan<CpkgBoneAnimTrack> Tracks;
};
```

Each track's `BoneIndex` is less than the skeleton's bone count. A keyframe's transform is relative to the bone's parent bone, or for a root bone to the skeleton instance's node.

### CpkgMeshSkinData (AssetType::MeshSkin)

Corresponds to `MeshSkinDesc`. A skin deforms a skinned mesh by the bones of a [skeleton](#cpkgskeletondata-assettypeskeleton). It references the skeleton by asset path and its bones by bone index, so it holds no asset index and may live in any bundle, typically beside the mesh it deforms. A [mesh instance](#cpkgmeshinstancedata-assettypemeshinstance) selects the skin and binds it to a skeleton instance. Several meshes of one character may share one skin.

Variable size. The payload's header is one `CpkgMeshSkinData`, whose spans refer to the skeleton path and the two per-palette-entry arrays in the data area:

```cpp
// Row-major, Canvas row-vector convention: v' = v * M, translation in row 3.
struct CpkgInvBindPose { float M[16]; };

struct CpkgMeshSkinData
{
    CpkgSpan<char>              Skeleton;      // asset path to the Skeleton this skin is built for; required
    CpkgSpan<uint32_t>          Bones;         // palette: one bone index into that skeleton per entry; at least one
    CpkgSpan<CpkgInvBindPose>   InvBindPoses;  // one per palette entry, same order as Bones
};
```

The palette size is `Bones.Count`; `InvBindPoses.Count` must equal it. Every `Bones` entry is less than the skeleton's bone count.

### CpkgCameraData (AssetType::Camera)

Describes a camera attached to a node: its clip planes, field of view, aspect ratio, and exposure. Fixed size: the payload is one `CpkgCameraData`.

```cpp
struct CpkgCameraData
{
    int32_t NodeIndex     = -1;         // asset index of the node this camera attaches to; required
    float   NearClip      = 0.1f;
    float   FarClip       = 1000.0f;
    float   FovAngle      = 0.785398f;  // radians
    float   AspectRatio   = 1.7778f;
    float   ExposureStops = 0.0f;
};
```

### CpkgLightData (AssetType::Light)

Describes a light attached to a node: light type, color/intensity, point/spot attenuation, spot cone angles, and shadow parameters. `Flags` is a `LightFlags` bit set (`CastsShadows`, `Enabled`); the shadow fields take effect only when `CastsShadows` is set. Fixed size: the payload is one `CpkgLightData`.

```cpp
struct CpkgLightData
{
    int32_t   NodeIndex            = -1;     // asset index of the node this light attaches to; required
    LightType Type                 = LightType::Point;
    float     Color[4]             = { 1.0f, 1.0f, 1.0f, 1.0f };  // linear RGBA
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

    // Shadow parameters (meaningful only when Flags has CastsShadows set)
    uint32_t  ShadowResolution     = 0;      // 0 = engine default
    float     ShadowConstantBias   = 1e-4f;
    float     ShadowSlopeScaleBias = 2.0f;
    float     ShadowNormalOffset   = 0.5f;   // in shadow-map texels
};
```

The shadow fields are always present, even when `Flags` does not have `CastsShadows` set, so the struct has a single fixed layout.

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
| `Gem::Result::CorruptedData` | The file is a package (its `Magic` matches) but its structure is damaged: a table, name blob, name, or payload outside the file, an empty package or bundle name, an unknown `AssetType` or `CpkgCodec`, or a `CpkgCodec::None` asset whose `StoredSize` differs from its `RawSize` (see `CCpkgReader::Open`). From a payload helper: a payload too small for its header, or a span that breaks a [Payload Layout](#payload-layout) rule |
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

`CopyBundle` copies a sealed bundle from a package open in a `CCpkgReader` into the package being written. The copy is byte-for-byte: each asset keeps its index, type, name, and compression scheme, and its stored payload is copied without being decompressed or interpreted. Because asset indices are preserved, the bundle's asset indices remain valid in the new package. Its path references are copied unchanged (see [Paths](#paths)).

This makes whole-bundle operations cheap without the format supporting in-place modification:

- **Merging packages.** One writer can copy bundles from any number of open readers, producing a single package from several. A bundle can be given a new name as it is copied, so bundles with the same name in different source packages can coexist.
- **Append-like updates.** Copy every bundle of an existing package into a new file, add, replace, or drop bundles along the way, then replace the original file with the new one.

Bundle indices are not preserved: a copied bundle's index is its position in the new package. Nothing in the format stores a bundle index; bundle paths reference bundles by name.

### Writer argument rules

The writer rejects any argument that would break a format rule or produce a file `CCpkgReader::Open` rejects, so every package `CCpkgWriter` produces is one its reader accepts. Each rule is checked before anything is written, and a violation returns `Gem::Result::InvalidArg` and leaves the writer unchanged (see [Writer failure closes the writer](#writer-failure-closes-the-writer)):

- **Call order**: `Open` while the writer is already open; `BeginBundle` or `CopyBundle` while a bundle is open; `AppendAsset` or `EndBundle` with no bundle open; `Close` while a bundle is open.
- **`CopyBundle` source**: `reader` must be open and `bundleIndex` in range.
- **Names** (`packageName`, `bundleName`, `name`, and the name a `CopyBundle` copy receives): must be valid UTF-8, must not contain a NUL byte or `/`, and must be at most `UINT32_MAX` bytes (the width of `NameLength`). A package name must be non-empty. A bundle name must be non-empty and unique within the package. An asset name must be non-empty when `CpkgIsSharedAssetType(assetType)` is true and may be empty otherwise, and a non-empty one must be unique within its bundle (see [Assets](#assets)).
- **`assetType`**: must be a named `AssetType` enumerator other than `AssetType::Unknown`.
- **`codec`**: must be a named `CpkgCodec` enumerator.
- **`data`**: must be non-empty. Every payload begins with its header struct (see [Package Data Types](#package-data-types)), so no asset type has an empty payload.
- **Counts**: a `BeginBundle` that would make the package's bundle count exceed `UINT32_MAX`, or an `AppendAsset` that would make the open bundle's asset count exceed it (the widths of `BundleCount` and `AssetCount`).

The reader does not recheck the name rules: a name that breaks them cannot cause an out-of-bounds read.

### Methods

**CCpkgWriter::Open**

Creates `fileName`, opens it for write, and writes the file header and `packageName`. `packageName` identifies the package in path references (see [Paths](#paths)) and follows the same rules as a bundle name. Fails with `Gem::Result::AlreadyExists` if `fileName` already exists, leaving it untouched.

`Gem::Result CCpkgWriter::Open(const char *fileName, std::string_view packageName)`

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

`name` may be empty for a local asset referenced only by index, and is required for a shared asset (see [Shared and Local Assets](#shared-and-local-assets)); a non-empty `name` must be unique within the bundle. `data` is the raw, uncompressed payload, and `codec` is the codec to store it with (`CpkgCodec::None` for no compression). `AppendAsset` compresses the payload and writes the result to the file before returning, retaining nothing beyond the in-memory bundle and asset tables and the open bundle's name blob, so a caller stages and appends one asset at a time while building a large bundle.

Fails with `Gem::Result::InvalidArg` if no bundle is open or any argument breaks a [writer argument rule](#writer-argument-rules). All of these are checked before anything is written; any failure after that closes the writer (see [Writer failure closes the writer](#writer-failure-closes-the-writer)).

`Gem::Result CCpkgWriter::AppendAsset(AssetType assetType, std::string_view name, CpkgCodec codec, std::span<const std::byte> data, uint32_t *pAssetIndex = nullptr)`

**CCpkgWriter::CopyBundle**

Copies the bundle at `bundleIndex` in `reader` into the package as a new, sealed bundle, and writes its index in this package to `pBundleIndex`, which may be null (see [Copying Bundles](#copying-bundles)). The copy is named `newName`, or the source bundle's name if `newName` is empty. Payload bytes are copied as stored and never inspected, so a payload that is corrupt in the source is equally corrupt in the copy.

Fails with `Gem::Result::InvalidArg` if a bundle is open, `reader` is not open, `bundleIndex` is out of range, or the copy's name breaks a [writer argument rule](#writer-argument-rules), including one already used in this package. All of these are checked before anything is written. A failed read from `reader` returns `Gem::Result::IoError` and, like any failure after validation, closes the writer; `reader` is unaffected.

`Gem::Result CCpkgWriter::CopyBundle(const CCpkgReader &reader, uint32_t bundleIndex, std::string_view newName = {}, uint32_t *pBundleIndex = nullptr)`

## CCpkgReader

CanvasPkg reads are random-access. The file-level bundle table and bundle name blob provide bundle names and the location of each bundle's asset table, and each bundle's asset table and asset name blob provide asset locations, names, types, and compression info without needing to read payload bytes.

### Name Directories

Name lookups are opt-in: a caller builds a directory once (e.g. a loader resolving path references) and searches it directly:

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

`Open` then loads the package's structure into memory: the package name, the file-level bundle table and bundle name blob, and every bundle's asset table and asset name blob. Each is one contiguous read. Payloads are not read.

The file is untrusted input, so `Open` validates everything it loads before accepting it. With `fileSize` the size of the file, and every sum and product evaluated without overflow:

- **File header**: `fileSize >= sizeof(CpkgFileHeader)`, `Magic` matches, and `Version` is supported.
- **Package name**: `PackageNameLength > 0`, `PackageNameOffset >= sizeof(CpkgFileHeader)`, and `PackageNameOffset + PackageNameLength <= fileSize`.
- **Bundle table and bundle name blob**: `BundleTableOffset >= sizeof(CpkgFileHeader)` and `BundleTableOffset + BundleCount * sizeof(CpkgBundleTableEntry) <= fileSize`; `BundleNameBlobOffset >= sizeof(CpkgFileHeader)` and `BundleNameBlobOffset + BundleNameBlobSize <= fileSize`.
- **Bundle table entries**: for each bundle, `NameLength > 0` and `NameOffset + NameLength <= BundleNameBlobSize`; `AssetTableOffset >= sizeof(CpkgFileHeader)` and `AssetTableOffset + AssetCount * sizeof(CpkgAssetTableEntry) <= fileSize`; `AssetNameBlobOffset >= sizeof(CpkgFileHeader)` and `AssetNameBlobOffset + AssetNameBlobSize <= fileSize`.
- **Asset table entries**: for each asset, `NameOffset + NameLength <= AssetNameBlobSize` for its bundle, and `NameOffset == 0` when `NameLength == 0`; `Type` is a named `AssetType` enumerator other than `Unknown`; `Compression.Codec` is a named `CpkgCodec` enumerator; `Compression.StoredSize == Compression.RawSize` when `Codec` is `CpkgCodec::None`; `PayloadOffset >= sizeof(CpkgFileHeader)` and `PayloadOffset + Compression.StoredSize <= fileSize`.

Each table's and name blob's extent is checked against `fileSize` before it is allocated, so a corrupt count or size cannot cause an allocation larger than the file. Every bound is checked here, so no later call can find a table entry, name, or payload outside the file.

`Open` does not check that tables, name blobs, and payloads are disjoint, that names are unique, or anything inside a payload: none of these can cause an out-of-bounds read. `CCpkgWriter` never produces duplicate names; in a file that contains them anyway, a [name directory](#name-directories) maps the name to one of the duplicates, unspecified which. Payload layout is checked by the [payload helpers](#payload-encoding-helpers).

Fails with `Gem::Result::NotFound` if `fileName` does not exist, `Gem::Result::IoError` if the file cannot be read, `Gem::Result::DataNotRecognized` if the file is too short for a `CpkgFileHeader` or its `Magic` does not match, `Gem::Result::CorruptedData` if any later check fails other than `Version`, `Gem::Result::OutOfRange` if `Version` is outside the supported range, and `Gem::Result::OutOfMemory` if the in-memory tables cannot be allocated. On failure the reader is not open and holds nothing.

`Gem::Result CCpkgReader::Open(const char *fileName)`

**CCpkgReader::Close**

Closes a CCpkgReader instance. Returns `Gem::Result::NotOpen` if the reader is not open.

`Gem::Result CCpkgReader::Close()`

**CCpkgReader::GetPackageName**

Returns the package name declared at `CCpkgWriter::Open`, or an empty view if the reader is not open. Package names are never empty, so an empty view always means the reader is not open. The package name is loaded by `Open`, so this reads no file data. The view refers to storage owned by the reader and is valid until `Close()`.

`std::string_view CCpkgReader::GetPackageName() const`

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
| `src/Inc/CanvasPkg.h` | Public header: `CCpkgWriter`, `CCpkgReader`, `CCpkgNameDirectory`, `AssetType` and `CpkgIsSharedAssetType`, `CpkgCodec`/`CpkgCompressionScheme`, `CpkgSpan`, the header-only payload helpers (`CCpkgPayloadBuilder`, `CpkgGetPayloadHeader`, `CpkgResolveSpan`), and every `Cpkg*Data` schema |
| `src/CanvasPkg/*.cpp` | Implementation |
| `src/CanvasPkg/*.h` | Internal-only headers (file layout structs, fixup bookkeeping); not part of the public surface |
| `src/CanvasPkg/project.schema.json` | JSON Schema for the [project manifest](#projects) |

This follows the same split as the rest of Canvas: public, consumable headers live under `src/Inc` alongside `CanvasCore.h`/`CanvasGfx.h`, while a component's own subdirectory under `src/` holds its implementation and any headers private to it.

## Future Directions

Directions the current design leaves room for. None are committed plans:

- **Compression**: `CpkgCompressionScheme` carries a `Codec` field and both a stored and a raw size, but only `CpkgCodec::None` is implemented. Adding a codec is additive; existing `None`-coded assets keep working.
- **Per-instance overrides**: every nested instance a placement declares is an unmodified instance of the placed bundle. Overrides such as material swaps or script parameters would extend `CpkgPlacementData` with an override list.
- **Bundle-level settings**: there is no asset type for the model-level metadata `XModel` carries (the active-camera-node designation, overall bounds). It would need its own `AssetType` value and schema.
- **World attributes**: `CpkgWorldSettingsData` holds the world-wide settings the engine has today. Settings such as gravity, spawn points, or global lighting would extend it as the engine grows them.
- **World variants**: content subsets of one world toggled on and off at runtime, such as a town before and after its destruction.
- **Local sky overrides**: volumes that override the active skybox within a bounded area and blend by camera position.
- **Equirectangular skies**: many HDR skies ship as latitude-longitude images. A skybox could accept one through a mapping field, or a writer could convert it to a `Cube` image at build time.
- **Streaming / zero-copy reads**: `ReadAsset` always copies into a caller buffer. Large texture payloads may eventually want a memory-mapped access path alongside the copying one.
- **Decoded, GPU-ready texture data**: a schema for pre-decoded pixel data in a GPU tile-region layout, including any mip packing, for direct-to-GPU-tile streaming. `CpkgImageData` already carries the texture shape and mip chain; this would add a decoded alternative to its encoded images.
- **Proportional skeleton variants**: characters of different proportions sharing one skeleton and its clips, each mesh with its own rest pose, with per-bone translation retargeting.
- **Project world list**: a project manifest could declare the world bundles a project offers, so a launcher or editor can list them without opening every package.

## Review Concerns (Open)

Design-review findings that block or complicate implementation as currently specified. Each is removed from this list once the design above is updated to resolve it.

1. **Minor: the decompression scratch pool is designed ahead of any codec.** v1 implements only `CpkgCodec::None`, which bypasses the pool, and at 64 KiB per buffer most compressed texture payloads would spill to one-off allocations anyway. Consider deferring [Decompression Scratch Buffer](#decompression-scratch-buffer) to the design pass that picks a codec.
