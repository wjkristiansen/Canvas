# CanvasProject

A **project** is a set of [CanvasPkg](../CanvasPkg/README.md) packages used together. A `.cpkg` file holds exactly one package, and a [path](../CanvasPkg/README.md#paths) in one package may name a bundle or asset in another, so a project declares which packages exist together and which package each such name refers to. A project is declared by its **project manifest**: a JSON file that names the project, lists its packages, each with a name and file location, and lists the worlds the project offers.

CanvasProject is a static library that reads and validates a project manifest and resolves the package names in paths. The engine uses it to open a project, and authoring tools use it to validate packages against the projects that list them, so both resolve package names identically.

## Project Manifest

The manifest's shape is defined by [project.schema.json](project.schema.json). A manifest's file name ends in `.cproj.json`, such as `game.cproj.json`: the `.json` ending lets text editors treat it as JSON, and the part before it lets tools recognize it as a project manifest.

```json
{
  "formatVersion": 1,
  "name": "Game",
  "packages": [
    { "name": "Levels",         "location": "levels.cpkg" },
    { "name": "Characters",     "location": "../Shared/characters.cpkg" },
    { "name": "Textures",       "location": "D:/Assets/textures.cpkg" },
    { "name": "VendorTextures", "location": "vendor/textures.cpkg" },
    { "name": "VendorProps",    "location": "vendor/props.cpkg",
      "references": { "Textures": "VendorTextures" } }
  ],
  "worlds": [ "Levels/MainMenu", "Levels/Level1", "Levels/Level2" ],
  "defaultWorld": "Levels/MainMenu"
}
```

| Field | Meaning |
|---|---|
| `formatVersion` | Manifest format version. This spec defines version `1`. |
| `name` | The project's name; non-empty. |
| `packages` | The project's packages, one entry per package. |
| `packages[].name` | The package's **project name**: the name the project's packages use for it in paths (see [Package Names](#package-names)). |
| `packages[].location` | File path of the package's `.cpkg` file, using `/` as its separator: absolute, or relative to the directory holding the manifest. |
| `packages[].references` | Optional. Maps package names used in this package's paths to project names (see [Package Names](#package-names)). |
| `worlds` | Optional. The world bundles the project offers, each written `package/bundle`, where `package` is a project name (see [Worlds](#worlds)). |
| `defaultWorld` | Optional. One of the `worlds` entries: the world the project declares for startup. |

Every project name and `references` key follows CanvasPkg's [name rules](../CanvasPkg/README.md#names).

## Rules

- **A project has one manifest, and the manifest is self-contained.** A project's packages are exactly those its manifest lists.
- **Project names are unique within a project.** No two entries of a manifest share a name.
- **Paths resolve within the project.** The `package` name in a path names a package of the project, as described in [Package Names](#package-names). A path that names no package, such as `bundle/asset`, resolves within its own package.
- **A project lists every package its packages reference.** Every package named by a path in any listed package resolves to a listed package. This includes packages that only other packages reference: a project that lists a shared characters package also lists every package the characters package's paths name.
- **Every non-null path in a listed package resolves** to a bundle or asset that exists, as CanvasPkg's [path rules](../CanvasPkg/README.md#paths) require.
- **Projects may share packages.** A texture library may be listed by any number of projects, under any project name. Each project resolves the shared package's paths among its own packages, using the shared package's `references` wherever its names differ from the project's.
- **Each `references` value is a project name** of a package the manifest lists.

How the engine opens a project, which world it activates first, and how it handles a package file that is missing, is defined by the [Canvas engine spec](../../README.md).

## Package Names

A package has two names. Its **declared name** is stored in its file header by `CCpkgWriter::Open` and returned by `CCpkgReader::GetPackageName`; it is the package's own identity, and the name its author uses for it. Its **project name** is the `name` of its manifest entry. The two may differ, so a project can list two packages that both declare `Textures`, under different project names.

A path names other packages by the names its package's author used. Each listed package resolves a package name in its own paths by the first of these rules that matches:

1. **Its own declared name** names the package itself.
2. **A key of its entry's `references`** names the package whose project name is that key's value.
3. **Any other name** names the package with that project name.

In the example above, the vendor's props package names its companion texture package `Textures`, which `references` maps to `VendorTextures`. A `Textures/...` path in the `Levels` package resolves to the project's own `Textures` package, and a `Textures/...` path in the vendor's texture package, which declares `Textures`, resolves to that package itself.

Rule 1 uses the referencing package's own declared name, and rules 2 and 3 use only the manifest, so a package name resolves without opening the package it names.

## Worlds

A project lists the [worlds](../CanvasPkg/README.md#worlds) it offers in `worlds`. A project may offer any number of worlds, such as a main menu, a loading screen, and each level of a game, and a project with a single world lists one.

- **Each `worlds` entry names a world bundle** of a listed package, by its project name, and appears once.
- **`defaultWorld` is one of the `worlds` entries.** It declares the world the project offers for startup.

## Source Layout

| Path | Description |
|---|---|
| `src/CanvasProject/README.md` | This spec |
| `src/CanvasProject/project.schema.json` | JSON Schema for the [project manifest](#project-manifest) |

## Future Directions

Directions the current design leaves room for. None are committed plans:

- **Library API**: the reader, validator, and package-name resolver described above, with the engine and authoring tools as consumers.
- **Package content hashes**: a manifest entry could record a hash of its package's content, so a package file that is damaged, or was rebuilt without the packages that depend on it, is detectable before its paths are resolved. The same hash supports verifying shipped content.
- **Project editor data**: editor-only organization of a project's bundles, such as display folders that span packages, kept in a separate file beside the manifest.
