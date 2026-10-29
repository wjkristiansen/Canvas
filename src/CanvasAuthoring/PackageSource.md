# Package Source

A **package source** is the text form of one [CanvasPkg](../CanvasPkg/README.md) package: JSON files describing the package's bundles and assets, together with the image and binary files they name. It is what an editor saves and what a person writes by hand, and the CanvasAuthoring library builds a `.cpkg` file from it. Its shape is defined by [package-source.schema.json](package-source.schema.json).

A package source expresses every `AssetType`, so any package can be built from one, including a package holding [world bundles](../CanvasPkg/README.md#worlds). It mirrors the package format closely, with conveniences for authoring: a nested node tree, references by name in place of asset indices, defaults for omitted fields, and alternative forms for values that are awkward to write by hand.

## Status

This revision specifies authored content. Imports, which bring FBX content into a bundle, are specified in the next revision (see [Imports](#imports)).

## Files

A package source is one **package file** and any number of **bundle files**:

```json
{
  "$schema": "package-source.schema.json",
  "formatVersion": 1,
  "packageName": "Levels",
  "bundles": [
    { "name": "Crate", "...": "..." },
    { "include": "village.bundle.json" }
  ]
}
```

| Field | Meaning |
|---|---|
| `$schema` | Optional. The schema the file follows, for editors that validate JSON as it is typed. |
| `formatVersion` | Package source format version. This spec defines version `1`. |
| `packageName` | The package's [declared name](../CanvasPkg/README.md#projects), written to its file header. |
| `bundles` | The package's bundles, each written inline or as `{ "include": file }`. The package's bundles are in this order. |

A bundle file holds one bundle:

```json
{
  "$schema": "package-source.schema.json",
  "formatVersion": 1,
  "bundle": { "name": "Village", "...": "..." }
}
```

A bundle file belongs to the package file that includes it, and is included once.

**File references.** A field that names a file on disk, such as an image, a bundle file, or a binary array, holds a file path using `/` as its separator: absolute, or relative to the directory of the JSON file that contains the field.

**Extension keys.** Every object may hold keys beginning with `x-`, such as `"x-note"` or `"x-editor"`. They carry editor data and comments, and the build ignores them.

## Names and References

Every bundle, asset, and node name follows CanvasPkg's [name rules](../CanvasPkg/README.md#names). Within a bundle, every named asset shares one namespace, whatever its type: the keys of the bundle's asset maps, and the names of its nodes, placements, and node clips, are all unique within the bundle.

A source refers to other content in one of two ways, matching the class of what it refers to:

| Refers to | Written as | Built as |
|---|---|---|
| A [shared asset](../CanvasPkg/README.md#shared-and-local-assets) or a bundle | A [path](../CanvasPkg/README.md#paths): `asset`, `bundle/asset`, or `package/bundle/asset`; `bundle` or `package/bundle` | The same path, unchanged |
| A local asset of the same bundle (a node, placement, or skeleton-bearing node) | Its name | Its asset index |

A path names other packages by their declared names, and the [project](../CanvasProject/README.md#package-names) that lists the package resolves them. A reference by name requires its target to be named.

**Bones** are referenced by name or by bone index. A name refers to the bone with that name in the relevant skeleton; an integer is a bone index.

## Values

| Value | Written as |
|---|---|
| Vector | An array of numbers: `[x, y, z]` |
| Color | Linear RGB `[r, g, b]` or RGBA `[r, g, b, a]`, as each field states |
| Rotation | A unit quaternion `[x, y, z, w]`, or `{ "degrees": [x, y, z] }` |
| Transform | An object with optional `translation`, `rotation`, and `scale`; omitted parts are the identity |
| Mobility | `"static"`, `"stationary"`, or `"movable"` |
| Large numeric array | An inline array, or `{ "file": file }` |

Values follow CanvasPkg's [conventions](../CanvasPkg/README.md#conventions): Canvas space, meters, radians, seconds, and linear color.

**Rotation in degrees.** `{ "degrees": [x, y, z] }` rotates about the X axis by `x` degrees, then about the Y axis by `y`, then about the Z axis by `z`, each axis fixed in the parent space. The build converts it to a unit quaternion.

**Binary arrays.** `{ "file": file }` names a file holding the array's elements tightly packed, little-endian, in the element layout of the CanvasPkg struct the array becomes (see [Payload Layout](../CanvasPkg/README.md#payload-layout)). The element count is the file size divided by the element size. An inline array holds the same elements flattened, so `positions` holds three numbers per vertex.

**Defaults.** An omitted field takes the default of the CanvasPkg struct field it fills, unless this spec gives another.

## Bundles

A bundle is an object with a `name` and any of these members:

| Member | Holds | Builds |
|---|---|---|
| `images` | Map of name to [image](#images) | `Image` |
| `materials` | Map of name to [material](#materials) | `Material` |
| `meshes` | Map of name to [mesh](#meshes) | `MeshData` |
| `skeletons` | Map of name to [skeleton](#skeletons) | `Skeleton` |
| `skins` | Map of name to [skin](#skins) | `MeshSkin` |
| `boneClips` | Map of name to [bone clip](#bone-clips) | `BoneAnimationClip` |
| `skyboxes` | Map of name to [skybox](#skyboxes) | `Skybox` |
| `nodes` | Array of root [nodes](#nodes) | `TransformNode` and its attachments |
| `placements` | Array of [placements](#placements) relative to the instance transform | `Placement` with `NodeIndex = -1` |
| `importNodes` | Array of [import nodes](#import-nodes) | `ImportNode` and its attachments |
| `nodeClips` | Map of name to [node clip](#node-clips) | `NodeAnimationClip` |
| `exports` | Array of node names | `ExportList` |
| `world` | A [world](#worlds) | `WorldSettings` |

Each map key is the name of the asset it builds. A bundle with a `world` member is a world bundle.

The build appends a bundle's assets in CanvasPkg's [canonical order](../CanvasPkg/README.md#append-order), so source order affects only the order of entries within an array whose order is meaningful, such as a skeleton's bones.

## Shared Assets

### Images

An image is one of three forms:

- **A file**, written as a string: a `Texture2D` image with one array slice and one mip.
- **A cubemap**, `{ "cubemap": { "+X": file, "-X": file, "+Y": file, "-Y": file, "+Z": file, "-Z": file } }`: a `Cube` image with `ArraySize` 6 and one mip. The six faces are square and share one size and one encoding.
- **A general image**, `{ "dimension", "arraySize", "mipLevels", "files" }`: any shape `CpkgImageData` holds. `dimension` is `"texture1D"`, `"texture2D"`, `"texture3D"`, or `"cube"`; `arraySize` and `mipLevels` default to 1; `files` lists one file per `Images` entry, in [CpkgImageData order](../CanvasPkg/README.md#cpkgimagedata-assettypeimage).

The build stores each file's bytes unchanged and reads each file's header for its size and encoding. Every file of one image shares one encoding.

### Materials

```json
"Brass": {
  "baseColor": [0.9, 0.7, 0.3, 1.0],
  "roughness": 0.35,
  "metallic": 1.0,
  "textures": { "albedo": "BrassAlbedo", "normal": "Shared/Metals/BrassNormal" }
}
```

| Field | Default | Builds |
|---|---|---|
| `baseColor` | `[1, 1, 1, 1]` | `BaseColorFactor`, linear RGBA |
| `emissive` | `[0, 0, 0]` | `EmissiveFactor`, linear RGB |
| `roughness` | `1` | `RoughMetalAOFactor` R |
| `metallic` | `0` | `RoughMetalAOFactor` G |
| `ambientOcclusion` | `1` | `RoughMetalAOFactor` B |
| `textures` | none | One asset path per role: `albedo`, `normal`, `roughness`, `metallic`, `ambientOcclusion`, `emissive` |
| `displacement` | none | `Displacement`, with `HasDisplacement = 1`: `map` (asset path, required), `scale`, `bias`, `minTessFactor`, `maxTessFactor`, `distanceLodScale`, `curvatureLodScale` |

### Meshes

```json
"CrateMesh": {
  "groups": [
    {
      "material": "Wood",
      "positions": [ 0,0,0,  1,0,0,  1,1,0 ],
      "normals":   [ 0,0,1,  0,0,1,  0,0,1 ],
      "uv0":       [ 0,0,    1,0,    1,1 ]
    }
  ]
}
```

| Field | Default | Builds |
|---|---|---|
| `topology` | `"triangleList"` | `Topology`: `"triangleList"` or `"patchList4CP"` |
| `bounds` | Computed from every group's positions | `LocalBounds`, as `{ "min", "max" }`, or `"none"` for the empty sentinel |
| `groups` | Required; at least one | `Groups` |

Each group has an optional `material` (asset path) and the vertex streams `positions`, `normals`, `uv0`, `tangents`, `boneIndices`, and `boneWeights`, each a large numeric array. The streams each topology requires, and the rules for skinned meshes, are CanvasPkg's (see [CpkgMeshData](../CanvasPkg/README.md#cpkgmeshdata-assettypemeshdata)).

### Skeletons

```json
"GoblinSkeleton": {
  "bones": [
    { "name": "Hips" },
    { "name": "Spine", "parent": "Hips", "rest": { "translation": [0, 0, 0.12] } },
    { "name": "Hand_R", "parent": "Spine", "rest": { "translation": [0.3, -0.1, 0.2] } }
  ]
}
```

`bones` lists the skeleton's bones in bone-index order, at least one. Each bone has an optional `name`, an optional `parent` (a bone reference to an earlier bone; omitted for a root), and an optional `rest` transform.

### Skins

| Field | Default | Builds |
|---|---|---|
| `skeleton` | Required | `Skeleton`, an asset path |
| `bones` | Required; at least one | `Bones`: the palette, as bone references into `skeleton` |
| `invBindPoses` | Computed from the skeleton's rest pose | `InvBindPoses`: one row-major matrix of 16 numbers per palette entry, as a large numeric array |

When `invBindPoses` is omitted, each palette entry's inverse bind pose is the inverse of its bone's rest pose relative to the skeleton instance's node: the bone's rest transform composed with those of its ancestor bones.

### Bone Clips

```json
"Walk": {
  "skeleton": "GoblinSkeleton",
  "duration": 1.2,
  "tracks": [
    { "bone": "Hips", "keyframes": [ { "time": 0, "translation": [0, 0, 0.9] }, { "time": 0.6, "translation": [0, 0, 0.95] } ] }
  ]
}
```

| Field | Default | Builds |
|---|---|---|
| `skeleton` | Required | `Skeleton`, an asset path |
| `duration` | The greatest keyframe time | `Duration` |
| `tracks` | Required | `Tracks`: each has a `bone` reference and `keyframes` |

A keyframe is a transform with a `time`. `keyframes` may instead be `{ "file": file }` holding `CpkgKeyframe` elements.

### Skyboxes

| Field | Default | Builds |
|---|---|---|
| `image` | Required | `Image`, an asset path to a cube image |
| `orientation` | Identity | `Orientation`, a rotation |
| `intensity` | `1` | `Intensity` |

## Bundle Structure

### Nodes

`nodes` is a tree: each node may hold `children`, and the tree is flattened into `TransformNode` assets with `ParentIndex` links. A root node has `ParentIndex = -1`.

```json
"nodes": [
  {
    "name": "Goblin",
    "mobility": "movable",
    "skeletonInstance": { "skeleton": "GoblinSkeleton" },
    "meshInstances": [ { "mesh": "GoblinBody", "skin": "GoblinSkin" } ],
    "children": [
      { "name": "Hand_R", "bone": "Hand_R" },
      { "name": "Eyes", "translation": [0.1, 0, 1.6], "camera": { "fovAngle": 1.2 } }
    ]
  }
]
```

| Field | Default | Builds |
|---|---|---|
| `name` | Unnamed | The node's asset name |
| `translation`, `rotation`, `scale` | Identity | `LocalTransform` |
| `mobility` | `"static"` | `Mobility` |
| `bone` | None | A bone of the parent node's skeleton instance: the node's `ParentIndex` is that `SkeletonInstance` and its `ParentBone` is the bone |
| `skeletonInstance` | None | A `SkeletonInstance` attached to the node: `{ "skeleton": asset path }` |
| `meshInstances` | None | One `MeshInstance` attached to the node per entry (see below) |
| `camera` | None | A `Camera` attached to the node (see below) |
| `light` | None | A `Light` attached to the node (see below) |
| `placements` | None | One [placement](#placements) attached to the node per entry |
| `children` | None | Child nodes |

A node with `bone` is a child of a node that has a `skeletonInstance`. A node holds at most one `skeletonInstance`, `camera`, and `light`; more of one kind attach to child nodes.

**Mesh instances.** Each entry has `mesh` (asset path, required) and `skin` (asset path, optional). A skinned entry binds to a skeleton instance: by default the one on the nearest node, starting with the entry's own node and moving toward the root, that has a `skeletonInstance`, or the one named by `skeletonInstance`, the name of a node of this bundle that has one.

**Cameras** have the optional fields `nearClip`, `farClip`, `fovAngle` (vertical), and `exposureStops`.

**Lights** have a required `type` (`"ambient"`, `"point"`, `"directional"`, `"spot"`, or `"area"`) and the optional fields `color` (linear RGB; stored with alpha 1), `intensity`, `enabled`, `castsShadows`, `range`, `attenuationConstant`, `attenuationLinear`, `attenuationQuadratic`, `spotInnerAngle`, `spotOuterAngle`, `shadowResolution`, `shadowConstantBias`, `shadowSlopeScaleBias`, and `shadowNormalOffset`. `enabled` defaults to `true` and `castsShadows` to `false`.

### Placements

A placement in a node's `placements` attaches to that node; one in the bundle's `placements` is relative to the instance transform.

```json
"placements": [
  { "bundle": "Characters/Goblin", "mobility": "movable", "translation": [12, 4, 0] },
  { "name": "Flowers", "bundle": "Flower", "transforms": [ { "translation": [1, 2, 0] }, { "translation": [3, 1, 0] } ] }
]
```

| Field | Default | Builds |
|---|---|---|
| `name` | Unnamed | The placement's asset name; required for an [import node](#import-nodes) to refer to it |
| `bundle` | Required | `Bundle`, a bundle path |
| `mobility` | `"static"` | `Mobility` |
| `transforms` | One entry, from the placement's own `translation`, `rotation`, and `scale` | `Transforms`: one transform per nested instance, at least one |

A placement holds either `transforms` or its own transform fields.

### Import Nodes

Each entry of `importNodes` is a node representing a node exported by a placed bundle:

```json
"importNodes": [
  {
    "placement": "Goblin1",
    "export": "Hand_R",
    "rotation": { "degrees": [0, 0, 90] },
    "placements": [ { "bundle": "Weapons/Sword" } ]
  }
]
```

`placement` names a placement of this bundle and `export` names an export of its placed bundle. The transform fields build `LocalTransform`, relative to the exported node. An import node may hold `placements`, `meshInstances`, `camera`, `light`, `skeletonInstance`, and `children`, as a node does, and may be named so other content refers to it.

### Node Clips

```json
"nodeClips": {
  "DoorSwing": {
    "duration": 1.5,
    "tracks": [ { "node": "Hinge", "keyframes": [ { "time": 0 }, { "time": 1.5, "rotation": { "degrees": [0, 0, 100] } } ] } ]
  }
}
```

A node clip has the same fields as a [bone clip](#bone-clips) without `skeleton`, and each track has a `node`, the name of a node of this bundle, in place of `bone`.

### Exports

`exports` lists the names of the nodes the bundle exports, each once.

## Worlds

A bundle's `world` member builds its `WorldSettings`:

```json
"world": {
  "regions": [ { "region": "Village", "bounds": { "min": [0, 0, -10], "max": [64, 64, 50] } } ],
  "skyboxes": [ "Day", "Skies/Sky/Night" ],
  "defaultSkybox": "Day",
  "activeCamera": "OverviewCam",
  "backgroundColor": [0, 0, 0, 1]
}
```

| Field | Default | Builds |
|---|---|---|
| `regions` | None | `Regions`: each a `region` bundle path and its streaming `bounds` |
| `skyboxes` | None | `Skyboxes`: asset paths to `Skybox` assets |
| `defaultSkybox` | None | `DefaultSkybox`: one of the `skyboxes` entries, stored as its position |
| `activeCamera` | None | `ActiveCameraIndex`: the name of a node of this bundle that has a `camera` |
| `backgroundColor` | `[0, 0, 0, 1]` | `BackgroundColor`, linear RGBA |

## Imports

An **import** brings the content of an FBX file into a bundle: named shared assets, and optionally the file's node hierarchy, with import settings such as unit and axis conversion, clip ranges, and material remapping. Content added to an imported hierarchy, such as a node parented to a bone or a changed mobility, is written against the imported nodes by name. Imports are specified in the next revision of this spec.

## Validation

The schema checks each file's shape. A package source is also valid only when it satisfies these rules, which the build checks and reports with the file and location of the content that breaks them:

- **Files.** Every named file exists and is readable. Each bundle file is included once.
- **Names.** Bundle names are unique within the package. Asset and node names are unique within their bundle (see [Names and References](#names-and-references)).
- **References by name.** Each refers to an existing node or placement of the same bundle, of the kind the field requires: an `activeCamera` node has a `camera`, a mesh instance's `skeletonInstance` node has a `skeletonInstance`, and a node with `bone` has a parent with a `skeletonInstance`.
- **Bone references.** Each names a bone of the relevant skeleton, or is less than its bone count. A skeleton's `parent` references refer to earlier bones.
- **Paths.** Each path is well formed for its kind. A path within the same package resolves to an existing asset of the declared type or to an existing bundle; a path into another package is checked when the build has a project.
- **Arrays.** Each inline array's length is a multiple of its element size, and each binary array file's size is a multiple of its element size. Parallel streams have equal counts.
- **Values.** Quaternions are non-zero; the build normalizes them. Bounds have `min <= max` on every axis.
- **Hierarchy.** An import node's placement attaches outside that import node's own subtree. Every other rule of the [package format](../CanvasPkg/README.md) holds for the built package.
