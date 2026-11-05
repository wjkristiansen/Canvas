# Package Source

A **package source** is the text form of one [CanvasPkg](../CanvasPkg/README.md) package: JSON files describing the package's bundles and assets, together with the image and binary files they name. It is what an editor saves and what can be written by hand, and the [`cpkg`](README.md) tool builds a `.cpkg` file from it. Its shape is defined by [package-source.schema.json](package-source.schema.json).

A package source expresses every `AssetType`, so any package can be built from one, including a package holding [world bundles](../CanvasPkg/README.md#worlds). It mirrors the package format closely, with conveniences for authoring: a nested node tree, references by name in place of asset indices, defaults for omitted fields, alternative forms for values that are awkward to write by hand, and [imports](#imports) that bring the content of FBX files into a bundle.

## Files

A package source is one **package file** and any number of **bundle files**:

```json
{
  "$schema": "package-source.schema.json",
  "formatVersion": 1,
  "packageName": "Levels",
  "bundles": [
    { "name": "Crate", "...": "..." },
    { "include": "village.cbundle.json" }
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

**File extensions.** A package file's name ends in `.cpkg.json`, and a bundle file's in `.cbundle.json`. The `.json` ending lets text editors treat both as JSON, and the part before it lets tools recognize which file is which. The build's default output for `levels.cpkg.json` is `levels.cpkg`, beside it.

**File references.** A field that names a file on disk, such as an image, a bundle file, an FBX file, or a binary array, holds a file path using `/` as its separator: absolute, or relative to the directory of the JSON file that contains the field.

**Extension keys.** Every object may hold keys beginning with `x-`, such as `"x-note"` or `"x-editor"`. They carry editor data and comments, and the build ignores them.

## Names and References

Every bundle, asset, and node name follows CanvasPkg's [name rules](../CanvasPkg/README.md#names). Within a bundle, every named asset shares one namespace, whatever its type: the keys of the bundle's asset maps, the names of its nodes and placements, and the names of the assets its imports build, are all unique within the bundle. Authored content refers to imported content by its built name, as it refers to authored content.

A source refers to other content in one of two ways, matching the class of what it refers to:

| Refers to | Written as | Built as |
|---|---|---|
| A [shared asset](../CanvasPkg/README.md#shared-and-local-assets) or a bundle | A [path](../CanvasPkg/README.md#paths): `asset`, `bundle/asset`, or `package/bundle/asset`; `bundle` or `package/bundle` | The same path, unchanged |
| A local asset of the same bundle (a node, placement, or skeleton-bearing node) | Its name | Its asset index |

A path names another package by the name the source's author uses for it, and the [project](../CanvasProject/README.md#package-names) that lists the package resolves that name to one of its packages. A reference by name requires its target to be named.

**Joints** are referenced by name or by joint index. A name refers to the joint with that name in the relevant skeleton; an integer is a joint index.

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
| `jointAnimations` | Map of name to [joint animation](#joint-animations) | `JointAnimation` |
| `skyboxes` | Map of name to [skybox](#skyboxes) | `Skybox` |
| `nodes` | Array of root [nodes](#nodes) | `TransformNode` and its attachments |
| `placements` | Array of [placements](#placements) relative to the instance transform | `Placement` with `NodeIndex = -1` |
| `importNodes` | Array of [import nodes](#import-nodes) | `ImportNode` and its attachments |
| `clips` | Map of name to [animation clip](#animation-clips) | `AnimationClip` |
| `exports` | Array of node names | `ExportList` |
| `world` | A [world](#worlds) | `WorldSettings` |
| `imports` | Array of [imports](#imports) | The assets each import builds |

Each map key is the name of the asset it builds. A bundle with a `world` member is a world bundle.

The build appends a bundle's assets in CanvasPkg's [canonical order](../CanvasPkg/README.md#append-order), so source order affects only the order of entries within an array whose order is meaningful, such as a skeleton's joints.

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
  "joints": [
    { "name": "Hips" },
    { "name": "Spine", "parent": "Hips", "rest": { "translation": [0, 0, 0.12] } },
    { "name": "Hand_R", "parent": "Spine", "rest": { "translation": [0.3, -0.1, 0.2] } }
  ]
}
```

`joints` lists the skeleton's joints in joint-index order, at least one. Each joint has an optional `name`, an optional `parent` (a joint reference to an earlier joint; omitted for a root), and an optional `rest` transform.

### Skins

| Field | Default | Builds |
|---|---|---|
| `skeleton` | Required | `Skeleton`, an asset path |
| `joints` | Required; at least one | `JointIndices`: one joint reference into `skeleton` per bone of the skin |
| `invBindPoses` | Computed from the skeleton's rest pose | `InvBindPoses`: one row-major matrix of 16 numbers per bone, as a large numeric array |

When `invBindPoses` is omitted, each bone's inverse bind pose is the inverse of its joint's rest pose relative to the skeleton instance's node: the joint's rest transform composed with those of its ancestor joints.

### Joint Animations

```json
"Walk.joints": {
  "skeleton": "GoblinSkeleton",
  "tracks": [
    { "joint": "Hips", "property": "translation", "times": [0, 0.6, 1.2], "values": [0, 0, 0.9, 0, 0, 0.95, 0, 0, 0.9] }
  ]
}
```

| Field | Default | Builds |
|---|---|---|
| `skeleton` | Required | `Skeleton`, an asset path |
| `tracks` | Required | `Tracks`: each a [track](#tracks) with a `joint` reference |

### Tracks

A track in a joint animation or an [animation clip](#animation-clips) builds one CanvasPkg [animation track](../CanvasPkg/README.md#animation-tracks):

| Field | Default | Builds |
|---|---|---|
| `joint` or `node` | Required | `JointIndex`, from a joint reference, in a joint animation; `NodeIndex`, from the name of a node of this bundle, in a clip |
| `property` | Required | `Property`: `"translation"`, `"rotation"`, or `"scale"` |
| `times` | Required | `Times`, a large numeric array |
| `values` | Required | `Values`, a large numeric array: three numbers per key for `translation` and `scale`, and a unit quaternion of four for `rotation` |
| `interpolation` | `"linear"` | `Interpolations`: `"linear"`, `"step"`, or `"cubic"`, for every key, or an array of them, one per key |
| `tangents` | Empty | `Tangents`, a large numeric array: per key, its in tangent and then its out tangent, each as many numbers as a value; for a track with a `"cubic"` key, required |

```json
{ "node": "Lid", "property": "translation", "times": [0, 0.5, 1], "values": [0, 0, 0, 0, 0, 0.3, 0, 0, 0],
  "interpolation": ["cubic", "cubic", "step"],
  "tangents": [0, 0, 0, 0, 0, 1.2,   0, 0, 0, 0, 0, 0,   0, 0, -1.2, 0, 0, 0] }
```

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
      { "name": "Hand_R", "joint": "Hand_R" },
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
| `joint` | None | A joint of the parent node's skeleton instance, which makes the node a [socket](../CanvasPkg/README.md#cpkgtransformnodedata-assettypetransformnode): its `ParentIndex` is that `SkeletonInstance` and its `ParentJoint` is the joint |
| `skeletonInstance` | None | A `SkeletonInstance` attached to the node: `{ "skeleton": asset path }` |
| `meshInstances` | None | One `MeshInstance` attached to the node per entry (see below) |
| `camera` | None | A `Camera` attached to the node (see below) |
| `light` | None | A `Light` attached to the node (see below) |
| `placements` | None | One [placement](#placements) attached to the node per entry |
| `children` | None | Child nodes |

A node with `joint` is a child of a node that has a `skeletonInstance`. A node holds at most one `skeletonInstance`, `camera`, and `light`; more of one kind attach to child nodes.

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

### Animation Clips

```json
"clips": {
  "DoorSwing": {
    "tracks": [ { "node": "Hinge", "property": "rotation", "times": [0, 1.5], "values": [0, 0, 0, 1, 0, 0, 0.766, 0.6428] } ]
  },
  "Walk": {
    "jointAnimations": [ { "skeletonInstance": "Goblin", "animation": "Walk.joints" } ]
  }
}
```

| Field | Default | Builds |
|---|---|---|
| `duration` | The greatest key time of its tracks and of the joint animations it binds | `Duration` |
| `tracks` | None | `Tracks`: each a [track](#tracks) with a `node` |
| `jointAnimations` | None | `JointAnimations`: each binds `animation`, the asset path of a `JointAnimation`, to `skeletonInstance`, the name of a node of this bundle that has a `skeletonInstance` |

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

An **import** builds the content of an FBX file into the bundle that lists it: its images, materials, meshes, skeletons, skins, and animation, and its node hierarchy with the cameras, lights, and mesh instances attached to it. The build reads the file whenever it builds the package, and the file is one of the build's inputs, so a changed FBX file is picked up by the next build. The import's settings are kept with the import rather than in the file.

```json
{
  "name": "Goblin",
  "imports": [
    {
      "file": "art/goblin.fbx",
      "scale": 0.01,
      "rename": { "meshes": { "Cube.001": "GoblinBody" } },
      "replace": { "materials": { "Skin": "Shared/Materials/GoblinSkin" } },
      "clips": [
        { "name": "Idle", "frames": [0, 59] },
        { "name": "Walk", "frames": [60, 95] }
      ],
      "edits": {
        "Armature": {
          "mobility": "movable",
          "children": [ { "name": "Hand_R_Grip", "joint": "Hand_R" } ]
        }
      }
    },
    { "file": "art/goblin_attack.fbx", "nodes": false, "skeleton": "Hips.skeleton", "clips": [ { "name": "Attack" } ] }
  ],
  "clips": {
    "Attack": { "jointAnimations": [ { "skeletonInstance": "Armature", "animation": "Attack.joints" } ] }
  },
  "exports": [ "Hand_R_Grip" ]
}
```

| Field | Default | Meaning |
|---|---|---|
| `file` | Required | The FBX file |
| `scale` | `1` | A uniform scale applied to the file's content, after conversion to meters |
| `rotation` | Identity | A rotation applied to the file's content, after conversion to Canvas space |
| `nodes` | `true` | Whether the import builds the file's node hierarchy (see [Imported Nodes](#imported-nodes)) |
| `parent` | None | The name of a node of this bundle, not built by this import, that the file's root nodes attach to; without one they are root nodes of the bundle |
| `skeleton` | None | The asset path of an existing `Skeleton` that the file's skins and joint animations use in place of a skeleton built from the file (see [Skeletons and Skins](#skeletons-and-skins)) |
| `names` | See [Imported Names](#imported-names) | A name template per kind of built asset |
| `rename` | None | Per kind, a map from a name in the file to the built asset's name |
| `replace` | None | `materials` and `images`: a map from a name in the file to the asset path of an existing asset used in its place |
| `clips` | One clip per animation stack | The animation clips the import builds (see [Imported Animation](#imported-animation)) |
| `edits` | None | A map from the built name of an imported node to an [edit](#editing-imported-nodes) of that node |

A bundle may list any number of imports, of the same file or of different files. The build adds each import's content to the bundle as though it were authored there, so the bundle's assets follow CanvasPkg's [canonical order](../CanvasPkg/README.md#append-order) whatever mix of authored and imported content they come from.

### Conversion

The build converts the file's content to CanvasPkg's [conventions](../CanvasPkg/README.md#conventions), using the unit scale and axis system the file declares, and then applies the import's `rotation` and `scale` about the file's origin. Both are baked into the built assets: `rotation` into vertex positions, normals, and tangents, node and joint transforms, inverse bind poses, and animation values; `scale` into positions and translations, bounds, inverse bind poses, animated translations, and distances such as light ranges and camera clip planes. `rotation` corrects a file whose declared axis system does not match its content; `scale` corrects a file whose declared unit does not, or resizes content on purpose.

Polygons are triangulated, and normals are generated for geometry the file holds none for. This section defines what an import builds and how a source controls it; how the build reads an FBX file is described in `cpkg`'s [Imports](README.md#imports).

### What an Import Builds

| From the file | Builds | Kind |
|---|---|---|
| A texture used by a built material | `Image`, its bytes stored unchanged | `images` |
| A material | `Material`, with its factors and texture roles | `materials` |
| A mesh | `MeshData`, one group per material the mesh uses | `meshes` |
| A hierarchy of joints | `Skeleton` | `skeletons` |
| A skinned mesh's skin | `MeshSkin`, with the file's inverse bind poses | `skins` |
| A clip's joint tracks | `JointAnimation`, one per skeleton the clip animates | `jointAnimations` |
| A clip | `AnimationClip`, with its node tracks, binding its joint animations | `clips` |
| A node | `TransformNode` | `nodes` |
| A joint that carries content | A socket: a `TransformNode` on the joint | None: named after the joint with `_Socket` appended |
| A camera, light, or mesh on a node | `Camera`, `Light`, or `MeshInstance` attached to the node's `TransformNode` | None: these are unnamed local assets |

A texture file's encoding is one CanvasPkg stores, such as PNG, and its image is a `Texture2D` with one mip. A texture the file embeds is stored from the embedded bytes, and an external texture from the file the FBX file names (see `cpkg`'s [Imports](README.md#imports)). A texture in any other encoding is either replaced, with `replace`, or converted to a stored encoding before the build.

A mesh used by several nodes builds one `MeshData`, and each node that uses it gets a `MeshInstance`. A material replaced with `replace` is not built, and neither is an image that only replaced materials use.

### Imported Names

Each built asset's name comes from the file. A kind's **name template** forms it: the template's `{name}` is replaced by the name of the content in the file. A name in `rename` is used as written, without the template.

| Kind | Name in the file | Default template |
|---|---|---|
| `images` | The file name the file records for the texture, without its extension, or else the texture's own name in the file | `{name}.image` |
| `materials` | The material's name | `{name}.material` |
| `meshes` | The mesh's name | `{name}.mesh` |
| `skeletons` | The name of the skeleton's root joint | `{name}.skeleton` |
| `skins` | The skinned mesh's name | `{name}.skin` |
| `jointAnimations` | The clip's name | `{name}.joints` |
| `clips` | The clip's name | `{name}` |
| `nodes` | The node's name | `{name}` |

A `jointAnimations` template may also hold `{skeleton}`, replaced by the skeleton's name in the file. It distinguishes the joint animations of a clip that animates more than one skeleton.

The templates keep the names of different kinds from colliding where a file names them alike, as when a node and its mesh are both named `Crate`. Two imports of files with overlapping names give one of them templates such as `"Tree_{name}.mesh"`. Each `/` in a name in the file becomes `_`. An unnamed node builds an unnamed `TransformNode`. An unnamed mesh, material, clip, or texture takes the name `Mesh<n>`, `Material<n>`, `Clip<n>`, or `Image<n>`, and a skeleton whose root joint is unnamed takes the name `Skeleton<n>`, where `<n>` is its position among the file's content of that kind, counting from 0.

`rename` and `names` are keyed by kind, using the kind names above:

```json
"names":  { "meshes": "Tree_{name}.mesh", "nodes": "Tree_{name}" },
"rename": { "materials": { "lambert1": "Bark" } }
```

### Imported Nodes

With `nodes` true, each node of the file builds a `TransformNode`, except the nodes that are joints, and keeps its parent and its local transform; a node whose parent is a joint is built under the joint's [socket](#skeletons-and-skins). The file's cameras, lights, and meshes attach to the nodes that hold them. Imported nodes are `"static"`, except nodes that a built clip's node tracks animate, which are `"movable"`, and edits may declare any mobility.

With `nodes` false, the import builds only shared assets: images, materials, meshes, skeletons, skins, and joint animations. It builds no nodes, attachments, or clips; a clip authored in the bundle binds the joint animations it builds, as the example's `Attack` clip does. An import of this kind is the way to build a library of meshes or materials, or animation alone, from a file.

### Skeletons and Skins

A **joint** is a node that the file marks as a skeleton bone, or that a skin in the file deforms with. Each tree of connected joints builds one `Skeleton`. Its joints are in the file's order, with a parent before its children, and each joint's rest transform is its transform in the file's bind pose. The joint nodes themselves build no `TransformNode`: a `SkeletonInstance` of the skeleton attaches to the parent node of the skeleton's root joint, or, when the root joint has no parent node, to a node built to carry it, named after the root joint.

Content the file places on a joint hangs from a **socket**, a node with [`joint`](#nodes) whose `ParentIndex` is the skeleton instance and whose `ParentJoint` is that joint. The import builds one socket for each joint that carries content, with an identity transform, named after the joint with `_Socket` appended. A node that the file parents to a joint builds a child of that joint's socket, keeping its local transform, so its node tracks animate it relative to the joint. A camera, light, or mesh on a joint attaches to the joint's socket. A skinned mesh's `MeshInstance` binds to the instance of its skin's skeleton.

With `skeleton`, the import builds no skeleton from the file. Its skins and joint animations use the named skeleton, matching the file's joints to that skeleton's joints by name, and its skeleton instances instance that skeleton. A second import of a character's animation names the skeleton the first import built, as `goblin_attack.fbx` does above.

### Imported Animation

Each entry of `clips` builds one clip from a range of an animation stack in the file:

| Field | Default | Meaning |
|---|---|---|
| `name` | Required | The clip's name in the file, which its kind's template applies to |
| `stack` | The file's only stack | The name of the animation stack the clip takes its keys from |
| `start`, `end` | The stack's start and end | The clip's range in seconds |
| `frames` | None | The clip's range as `[first, last]` frames at the file's frame rate, in place of `start` and `end` |

The built clip starts at time 0, at the range's start. Each animated property of a node builds one track, holding the stack's keys within the range, plus keys sampled at the range's ends where the stack has none. Its `Duration` is the range's length. Tracks on joints build one joint animation per skeleton the clip animates, and the clip binds each to the skeleton's instance.

Tracks on every other node are **rigid animation**: the node moves as one body with everything attached to it, such as a door, a vehicle's wheels, or a prop animated in a character's hand. They are the clip's node tracks, one for each property of a node that the stack animates, and the nodes they animate are built `"movable"`. A node that the stack animates while `nodes` is false builds no track, since the import builds no nodes, and the build reports each one.

Without `clips`, the import builds one clip per animation stack, covering the whole stack and named after it. `"clips": []` builds no animation.

### Editing Imported Nodes

An **edit** changes an imported node, or adds content to it, without changing the file. Each key of `edits` is the built name of a node the import builds, and its value holds any of these fields:

| Field | Effect |
|---|---|
| `translation`, `rotation`, `scale` | Replaces that part of the node's imported local transform |
| `mobility` | Replaces the node's mobility |
| `camera` | Sets the listed camera fields of the node's camera, or attaches a camera with them when the node has none |
| `light` | Sets the listed light fields of the node's light, or attaches a light with them when the node has none; a light it attaches has a `type` |
| `meshInstances`, `placements`, `children` | Adds mesh instances, placements, or child nodes to the node, as a [node](#nodes) holds them |

An edit's `children` with `joint` parent to the skeleton instance on the edited node, as the example's `Hand_R_Grip` does. Content anywhere in the bundle refers to imported nodes by name: `exports`, a clip track's `node`, a clip binding's or mesh instance's `skeletonInstance`, and a world's `activeCamera` each accept an imported node.

## Validation

The schema checks each file's shape. A package source is also valid only when it satisfies these rules, which the build checks and reports with the file and location of the content that breaks them:

- **Files.** Every named file exists and is readable. Each bundle file is included once.
- **Names.** Bundle names are unique within the package. Asset and node names are unique within their bundle (see [Names and References](#names-and-references)).
- **References by name.** Each refers to an existing node or placement of the same bundle, of the kind the field requires: an `activeCamera` node has a `camera`, a mesh instance's or clip binding's `skeletonInstance` node has a `skeletonInstance`, a node with `joint` has a parent with a `skeletonInstance`, and an import's `parent` is a node that import does not build.
- **Joint references.** Each names a joint of the relevant skeleton, or is less than its joint count. A skeleton's `parent` references refer to earlier joints.
- **Tracks.** `times` is nondecreasing, and `values`, an `interpolation` array, and `tangents` hold the number of entries its keys and `property` call for (see [Animation Tracks](../CanvasPkg/README.md#animation-tracks)). A clip track's `node` has no `joint`, and its effective mobility is `"movable"`. A clip animates each node property, and binds each skeleton instance, at most once, and a joint animation animates each joint property at most once.
- **Paths.** Each path is well formed for its kind. A path within the same package resolves to an existing asset of the declared type or to an existing bundle; a path into another package is checked when the build has a project.
- **Arrays.** Each inline array's length is a multiple of its element size, and each binary array file's size is a multiple of its element size. Parallel streams have equal counts.
- **Values.** Quaternions are non-zero; the build normalizes them. Bounds have `min <= max` on every axis.
- **Imports.** Each `file` is an FBX file the build can read. Each clip's `stack` exists, and its range lies within the stack, with its start no later than its end. Each key of `rename` and `replace` names content of its kind in the file, and each key of `edits` names a node the import builds, so a misspelled key is reported. Each built name follows the name rules and is unique within the bundle. Each image the import builds has an encoding CanvasPkg stores. With `skeleton`, every joint a skin deforms with is a joint of that skeleton; a joint animation's tracks on joints the skeleton lacks are left out, and the build reports each one.
- **Hierarchy.** An import node's placement attaches outside that import node's own subtree. Every other rule of the [package format](../CanvasPkg/README.md) holds for the built package.
