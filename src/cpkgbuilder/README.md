# cpkgbuilder

cpkgbuilder is a command-line, offline content-build tool that cooks a `.cpkg` package (see [CanvasPkg](../CanvasPkg/README.md)) from a JSON manifest. It targets both Windows and Linux, and uses a plain lowercase name rather than the `Canvas*` convention used by the GUI apps elsewhere in this repo (`CanvasModelViewer`, `CanvasTerrainViewer`, ...).

## Status

This is a starting design, written alongside [CanvasPkg](../CanvasPkg/README.md), which cpkgbuilder depends on. Both will keep evolving together.

## Command Line

```
cpkgbuilder --manifest <manifest.json> --output <package.cpkg>
```

Flags are illustrative; the final set is still being designed.

## Manifest to Package

The JSON manifest describes one or more bundles. Its full shape is defined in [manifest.schema.json](manifest.schema.json), a JSON Schema kept in sync with this document as the design settles; the sections below walk through it.

Each bundle is populated **either** by importing a whole FBX file, **or** by hand-authoring its contents directly in JSON - never both in the same bundle (see [Hand-Authored Bundles](#hand-authored-bundles)):

```json
{
  "bundles": [
    {
      "name": "Hero",
      "import": {
        "type": "fbx",
        "path": "art/hero.fbx",
        "excludeType": ["camera", "light"]
      }
    },
    {
      "name": "SkyBox",
      "images": {
        "Sky": {
          "cubemap": {
            "+X": "art/sky_px.png",
            "-X": "art/sky_nx.png",
            "+Y": "art/sky_py.png",
            "-Y": "art/sky_ny.png",
            "+Z": "art/sky_pz.png",
            "-Z": "art/sky_nz.png"
          }
        }
      }
    },
    {
      "name": "Props",
      "meshes": {
        "CrateMesh": {
          "positions": [ 0,0,0,  1,0,0,  1,1,0,  0,1,0 ],
          "normals":   [ 0,0,1,  0,0,1,  0,0,1,  0,0,1 ],
          "uv0":       [ 0,0,    1,0,    1,1,    0,1 ]
        }
      },
      "lights": {
        "TorchFlame": { "type": "point", "color": [1, 0.6, 0.3], "intensity": 4 }
      },
      "nodes": [
        { "name": "Crate1", "translation": [0, 0, 0], "mesh": "CrateMesh" },
        {
          "name": "Crate2",
          "translation": [2, 0, 0],
          "mesh": "CrateMesh",
          "children": [
            { "name": "Flame", "translation": [0, 1, 0], "light": "TorchFlame" }
          ]
        }
      ]
    }
  ]
}
```

For each bundle, cpkgbuilder calls `CCpkgWriter::BeginBundle`, calls `CCpkgWriter::AppendAsset` once per asset, then closes the bundle with `CCpkgWriter::EndBundle`. A hand-authored bundle's `"nodes"` tree is flattened into `CpkgSceneNodeData`'s `ParentIndex`-linked array in a pre-order traversal (parent written before its children) when the bundle is cooked; the same flattening applies to an FBX-imported hierarchy.

## FBX Sources

FBX import already exists and does most of the heavy lifting: `Canvas::Fbx::ImportScene` (see [CanvasFbx.h](../CanvasFbx/Inc/CanvasFbx.h)) parses an `.fbx` file into an in-memory `ImportedScene` - nodes, meshes, materials, texture references, animation clips, skins, lights, and cameras. cpkgbuilder's FBX path is a translation layer over this existing output: call `ImportScene`, then walk the resulting `ImportedScene` and emit one `Cpkg*Data` payload per entry. cpkgbuilder never writes raw `.fbx` bytes into a package - an FBX source is always fully converted into the bundle of `Cpkg*Data` assets it describes.

`ImportScene` takes a `const wchar_t *` path. cpkgbuilder's own APIs and manifest handling stay `const char *`/UTF-8 throughout, same as CanvasPkg; the UTF-8 path is converted to a wide string only at the `ImportScene` call site, never propagated further.

The mapping is close to 1:1:

| `ImportedScene` | CanvasPkg |
|---|---|
| `ImportedNode` | `CpkgSceneNodeData` |
| `ImportedMesh` / `ImportedMeshPart` | `CpkgMeshData` / `CpkgMeshDataGroup` |
| `ImportedMaterial` | `CpkgMaterialData` |
| `ImportedTextureRef` | `CpkgImageData` (`AssetType::Image`, with `CpkgImageEncoding` set from the source encoding) |
| `ImportedAnimationClip` / `ImportedAnimationTrack` | `CpkgAnimClipData` / `CpkgAnimTrack` |
| `ImportedSkin` | `CpkgMeshSkinData` |
| `ImportedCamera` | `CpkgCameraData` |
| `ImportedLight` | `CpkgLightData` |

`ImportedMaterial`'s fields (per-role texture indices, `BaseColorFactor`/`EmissiveFactor`/`RoughMetalAOFactor`) already line up with `CpkgMaterialData` almost field-for-field: both trace back to the same `XGfxMaterial` shape.

### Excluding element types

An `import` source's optional `"excludeType"` list drops element-attachment kinds from the imported hierarchy before conversion - `"mesh"`, `"camera"`, and/or `"light"` - matching the node attachment kinds below. This is meant for artifacts of the authoring tool, like a default camera or light an FBX exporter adds to every file regardless of scene content: `"excludeType": ["camera", "light"]` discards every `ImportedCamera`/`ImportedLight` in the file, wherever it sits in the hierarchy. (Skins aren't independently excludable - a skin always follows its mesh.) `"excludeType"` is named for this by-type filter specifically, leaving room for future `"exclude*"` siblings that filter by name, regex, or tag instead.

After exclusion, cpkgbuilder prunes bottom-up: any node left with no attached element and no remaining children is dropped, and this repeats until no further node qualifies. A transform node that existed only to carry an excluded camera disappears instead of surviving as a dangling empty node.

## Hand-Authored Bundles

A bundle not populated by `"import"` is hand-authored directly in the manifest: a `"nodes"` tree, plus optional bundle-level `"meshes"`/`"cameras"`/`"lights"`/`"images"` maps (name -> definition). This is meant for small, hand-placed content - simple shapes, hand-tuned lights, still cameras - not as a general modeling format; the vast majority of mesh/animation data is expected to come from FBX import.

**Node tree.** `"nodes"` is a real nested tree (`"children"`), not a flat list with parent-name references, matching `CSceneGraphNode`'s actual parent/child structure (see [SceneGraph.h](../CanvasCore/SceneGraph.h)). It is flattened into `CpkgSceneNodeData`'s `ParentIndex`-linked array in pre-order when cooked (see [Manifest to Package](#manifest-to-package)). An FBX-imported bundle and a hand-authored bundle never mix hierarchies within the same bundle.

**Node attachments.** A node's `"mesh"`/`"camera"`/`"light"` field accepts either a string naming an entry in the bundle's corresponding map (for reuse/instancing across nodes - e.g. one `"TorchFlame"` light definition placed at several nodes) or an inline definition object (for a one-off, with no map entry needed). Both forms produce the same `Cpkg*Data` asset; a named one is just shared by index across every node that references it.

**Inline mesh data.** A hand-authored mesh is raw vertex data - `"positions"`/`"normals"` (required), `"uv0"`/`"tangents"` (optional) - interleaved per vertex, one triple/quad per vertex, matching `CpkgMeshDataGroup`'s own layout. Triangle winding is CCW-front (right-hand rule), matching Canvas's existing rendering convention (see `CanvasMath.hpp`) - normals and winding are authored explicitly, never inferred. A hand-authored mesh currently produces a single `CpkgMeshDataGroup` with no material binding (`MaterialIndex` stays `-1`); see [Open Questions](#open-questions).

**External raw data files.** Any of these array fields (`positions`, `normals`, `uv0`, `tangents`) may be given as `{ "path": "file.bin" }` instead of an inline JSON array, for data too large to reasonably inline as JSON text. The file holds tightly packed, native-endian (little-endian) elements with no header, the same convention as an on-disk blob; the element count is derived from the file size, exactly as `CpkgMeshDataGroup`'s own fields derive their count from blob length (see CanvasPkg's [Variable-Length Fields](../CanvasPkg/README.md#variable-length-fields)).

**Images.** The bundle-level `"images"` map is independent of `"nodes"` - an image asset has no scene-graph position. An entry is either a plain path string (a single embedded image) or a `"cubemap"` object naming one file per face, as in the `SkyBox` example above.

## Texture Sources

`ImportedTextureRef` carries a resolved file path and, when the FBX embeds the image, the raw encoded bytes directly. Either way, cpkgbuilder copies those encoded bytes through unchanged into `CpkgImageData::ImageData`; CanvasPkg stores an image asset as an embedded encoded image and no decoding is involved (see [CpkgImageData](../CanvasPkg/README.md#cpkgimagedata-assettypeimage)). The asset's `CpkgImageEncoding` is determined from the file extension, or by sniffing the bytes' magic number when an FBX-embedded image has no path; the exact rule is still open (see [Open Questions](#open-questions)).

## Review Concerns (Open)

Design-review findings that block or complicate implementation as currently specified. Each is removed from this list once the design above (or [manifest.schema.json](manifest.schema.json), or CanvasPkg) is updated to resolve it.

1. **Cubemaps have no package representation.** The manifest's `"cubemap"` image form names six face files, but CanvasPkg has no cubemap asset type and no grouping metadata - `CpkgImageData` is one encoded image, full stop. Six separate image assets lose the fact that they form one cubemap (and which face is which), so a loader building a `GfxBackgroundDesc` skybox (`DimensionCube`, `ArraySize = 6`) has nothing to go on. Needs either a CanvasPkg-side answer (a cubemap asset type or face metadata) or a defined builder convention (e.g. face-suffixed asset names) that this document and the loader both commit to. Until then the `SkyBox` example cannot actually be cooked.

2. **Bottom-up pruning as specified deletes skeletons.** Bone nodes are plain transform nodes: no mesh/camera/light attachment, and leaf bones have no children. The rule "any node left with no attached element and no remaining children is dropped, repeated" therefore consumes an entire bone hierarchy from the leaves up whenever pruning runs - e.g. `"excludeType": ["light"]` on a skinned character strips its skeleton. Pruning must exempt nodes referenced by any skin (`CpkgMeshSkinData::pBoneNodeIndices` / `MeshNodeIndex`) and nodes targeted by any animation track. Relatedly, define what happens to animation tracks whose target node *is* legitimately excluded/pruned (drop the track, presumably) - the current text doesn't say.

3. **FBX names vs per-bundle unique asset names.** CanvasPkg requires asset names to be unique within a bundle, across all asset types. FBX files routinely contain duplicate or empty node names, and here nodes, meshes, materials, images, clips, and skins all share one bundle namespace (an `ImportedMesh` named "Hero" and an `ImportedNode` named "Hero" collide). A deterministic name-disambiguation rule is required - but renaming nodes is not free: `CpkgAnimTrack::NodeName` matches by node name at bind time, so whatever rename a node receives must be applied identically to every animation track that targets it (the builder converts `ImportedAnimationTrack::NodeIndex` to a name, so it must convert to the *final* name). The same rule must cover manifest-side collisions (a node and a `"meshes"` map entry with the same name - the schema cannot express this constraint) and generated names for inline/anonymous mesh/camera/light definitions and for skin assets, which have no manifest name at all.

4. **Path resolution base is unspecified.** `import.path`, image path strings, cubemap face paths, and `rawFloatArray` `{ "path": ... }` files are all relative paths in the examples, but nothing says relative to what - the manifest file's directory or the tool's working directory. Manifest-relative is the predictable choice; whichever is picked, state it once and apply it to every path in the manifest.

5. **Image encodings outside JPG/PNG/TIFF are unhandled.** `AssetType` covers exactly three encodings, but `ImportedTextureRef` can reference whatever the FBX pointed at - TGA, BMP, DDS, and EXR are common in FBX workflows. Define the behavior (hard error vs. warn-and-skip with the material slot left unbound), and likewise for a non-embedded `ImportedTextureRef` whose `AbsoluteFilePath` doesn't exist on disk at cook time. This is distinct from the encoding-*detection* open question below: even with detection settled, a correctly detected unsupported encoding needs a defined outcome.

6. **Light color: 3 components in the manifest, 4 in the package.** `lightDef.color` is `minItems: 3, maxItems: 3` in the schema, but `CpkgLightData::Color` is `float[4]` (RGBA), as is `ImportedLight::Color`. Decide whether alpha is authorable (schema becomes 3-or-4 with a stated default) or fixed at 1.0 (document that), and make the schema, this document, and the struct agree.

7. **Unreferenced map entries: emitted or not?** Bundle-level `"images"` entries are assets with no node reference by design, but the text never says whether a `"meshes"`/`"cameras"`/`"lights"` map entry that no node references is still cooked into the bundle as an asset, or silently dropped (or a warning). Specify - "always emitted" is the least surprising and matches how images behave.

8. **Builder-side validation needs an explicit list.** JSON Schema cannot express several invariants the cook must enforce: bundle names unique across the manifest, asset-name uniqueness per bundle (see concern 3), `positions`/`normals` lengths a multiple of 3 and equal vertex counts across all supplied streams of one mesh (`uv0` = 2/vertex, `tangents` = 4/vertex), external raw-data file sizes a multiple of the element stride, and `rotation` quaternions non-zero. These belong in a "manifest validation" section so the error behavior is designed rather than accidental.

9. **Minor: hand-authored meshes cannot select a topology.** `meshDef` has no `"topology"` field, so everything cooks as `TriangleList`; `GfxPrimitiveTopology::PatchList4CP` (the displaced-mesh path) is unreachable from a manifest. Fine to defer - displacement also requires the material binding that is already an open question - but worth an explicit note so it isn't mistaken for an oversight.

## Open Questions

- **Image encoding detection**: which `CpkgImageEncoding` a given image source maps to needs a concrete rule - file extension, embedded-bytes magic-number sniffing, or both.
- **Hand-authored material binding**: hand-authored meshes (see [Hand-Authored Bundles](#hand-authored-bundles)) have no way to reference a material yet - `MaterialIndex` is always `-1`. A `"material"` mesh field, likely following the same named-reference-or-inline pattern as mesh/camera/light, needs its own design pass once texture/material referencing from a hand-authored bundle is worked out.
- **World/region composition**: placing model instances (characters, furniture, ...), collision meshes, skyboxes, and dynamic lighting (e.g. a sun/moon cycle) across a level is a distinct concept from bundle asset authoring - most likely a separate manifest-level or runtime-level construct, not part of the bundle schema above. Deliberately out of scope for now.
- **Incremental cooking**: whether cpkgbuilder always rebuilds a whole package, or supports partial/incremental rebuilds keyed on source file timestamps/hashes, is undecided.
- **Error reporting**: `ImportedScene::Diagnostics` already carries import warnings/errors from CanvasFbx; how cpkgbuilder surfaces those plus its own cooking errors (exit codes, log format) is undecided.

## Future Considerations

- **Decoded, GPU-ready texture cooking**: today cpkgbuilder only copies embedded/source image bytes through as opaque `CpkgImageData` payloads. If CanvasPkg later grows a decoded-pixel texture schema (see its [Future Directions](../CanvasPkg/README.md#future-directions)), cpkgbuilder would need an actual image decoder to produce it: `CanvasPlatformWin32::LoadImageData` (WIC-backed) on Windows, and a `stb_image`-style dependency on Linux, where `CanvasPlatformWin32` isn't available.
