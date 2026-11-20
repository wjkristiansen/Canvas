# CanvasProject

A **project** is a set of [CanvasPkg](../CanvasPkg/README.md) packages used together. A `.cpkg` file holds exactly one package, and a [path](../CanvasPkg/README.md#paths) in one package may name a bundle or asset in another, so a project declares which packages exist together and which package each such name refers to. A project is declared by its **project manifest**: a JSON file that names the project, lists its packages, each with a name and a file, and lists the worlds the project offers.

A manifest takes one of two forms. An **authored project** is the one people and editors work on: it lists each package by its [package source](../cpkg/PackageSource.md), or by its `.cpkg` file for a package that has no source. Building an authored project produces a **built project**, which lists every package by its built `.cpkg` file, and is the form the engine opens.

CanvasProject is a static library that reads and validates project manifests and resolves the package names in paths. Every program that resolves package names through it resolves them identically.

## Project Manifest

The manifest's shape, meaning the keys of each object and the types and forms of their values, is defined by [project.schema.json](project.schema.json), and any validator that supports JSON Schema 2020-12 can check a manifest or a built project against it. This document defines what each field means, and the rules a schema cannot express, such as unique project names and the package names that references resolve. A manifest's file name ends in `.cproj.json`, such as `game.cproj.json`: the `.json` ending lets text editors treat it as JSON, and the part before it lets tools recognize it as a project manifest.

```json
{
  "formatVersion": 1,
  "name": "Game",
  "output": "build",
  "packages": [
    { "name": "Levels",         "source":  "levels/levels.cpkg.json" },
    { "name": "Characters",     "source":  "../Shared/characters/characters.cpkg.json" },
    { "name": "Textures",       "source":  "D:/Assets/library/library.cpkg.json" },
    { "name": "VendorTextures", "package": "vendor/textures.cpkg" },
    { "name": "VendorProps",    "package": "vendor/props.cpkg",
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
| `output` | Optional; default `build`. The directory a build of the project writes to (see [Building a Project](#building-a-project)). |
| `packages` | The project's packages, one entry per package. |
| `packages[].name` | The package's **project name**: the name the project's packages use for it in paths (see [Package Names](#package-names)). |
| `packages[].source` | The package's source: the file path of its `.cpkg.json` package file. |
| `packages[].package` | The package's `.cpkg` file. |
| `packages[].references` | Optional. Maps package names used in this package's paths to project names (see [Package Names](#package-names)). |
| `worlds` | Optional. The world bundles the project offers, each written `package/bundle`, where `package` is a project name (see [Worlds](#worlds)). |
| `defaultWorld` | Optional. One of the `worlds` entries: the world the project declares for startup. |

Each package entry holds exactly one of `source` and `package`. A package listed by `package` is a **prebuilt package**: one built elsewhere, such as a package from a vendor or from another project, which this project uses as it is. File paths use `/` as their separator, and are absolute or relative to the directory holding the manifest. Every project name and `references` key follows CanvasPkg's [name rules](../CanvasPkg/README.md#names).

## Rules

- **A project has one manifest, and the manifest is self-contained.** A project's packages are exactly those its manifest lists.
- **Project names are unique within a project.** No two entries of a manifest share a name.
- **Paths resolve within the project.** The `package` name in a path names a package of the project, as described in [Package Names](#package-names). A path that names no package, such as `bundle/asset`, resolves within its own package.
- **A project lists every package its packages reference.** Every package named by a path in any listed package resolves to a listed package. This includes packages that only other packages reference: a project that lists a shared characters package also lists every package the characters package's paths name.
- **Every non-null path in a listed package resolves** to a bundle or asset that exists, as CanvasPkg's [path rules](../CanvasPkg/README.md#paths) require. For a package listed by its source, the path resolves against the content its source declares.
- **Projects may share packages.** A texture library may be listed by any number of projects, under any project name. Each project resolves the shared package's paths among its own packages, using the shared package's `references` wherever its names differ from the project's.
- **Each `references` value is a project name** of a package the manifest lists.

## Building a Project

A build of an authored project builds each package listed by its source, and writes a built project beside the packages it builds. The build's caller may name a directory that takes the place of `output`, and the build writes there instead. A build writes:

- **Packages.** Each `source` package builds to a `.cpkg` file in `output`, named after its package file without the `.json` ending, so `levels/levels.cpkg.json` builds `build/levels.cpkg`.
- **Prebuilt packages.** Each prebuilt package is copied into `output` under its file name.
- **Built project.** The built project is written to `output` under the authored manifest's file name. It holds the authored manifest's content without `output`, with each entry's `source` or `package` replaced by a `package` naming the package's file in `output` by its file name.

The file names a build writes in `output` are unique, so two files that would share a name in `output` are a build error. The built project and every package it lists are in `output`, so the directory holds everything the engine opens, and can be moved or shipped as a whole.

A built project is written only when every package builds. A build that fails writes no built project and removes any built project an earlier build left in `output`, so a built project in `output` always lists packages that one successful build produced together.

For the example above, the built project is `build/game.cproj.json`:

```json
{
  "formatVersion": 1,
  "name": "Game",
  "packages": [
    { "name": "Levels", "package": "levels.cpkg" },
    { "name": "Characters", "package": "characters.cpkg" },
    { "name": "Textures", "package": "library.cpkg" },
    { "name": "VendorTextures", "package": "textures.cpkg" },
    {
      "name": "VendorProps",
      "package": "props.cpkg",
      "references": { "Textures": "VendorTextures" }
    }
  ],
  "worlds": ["Levels/MainMenu", "Levels/Level1", "Levels/Level2"],
  "defaultWorld": "Levels/MainMenu"
}
```

The `Textures` package declares the name `Textures` in its source, and builds to `library.cpkg`, after its package file: a package's file name and its names are independent.

The engine opens a built project: a manifest whose every entry is a `package`. How it opens the project, which world it activates first, and how it handles a package file that is missing, is defined by the [Canvas engine spec](../CanvasCore/README.md).

## Package Names

A package has two names. Its **declared name** is its own identity, and the name its author uses for it: the `packageName` of its package source, which the build stores in its file header, and which `CCpkgReader::GetPackageName` returns. Its **project name** is the `name` of its manifest entry. The two may differ, so a project can list two packages that both declare `Textures`, under different project names.

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

## Library API

CanvasProject's public header is `src/Inc/CanvasProject.h`, and its declarations are in namespace `Canvas`. Calls that can fail return `Gem::Result`, and no call throws. It links CanvasPkg, for the [name rules](../CanvasPkg/README.md#names), and reads JSON with [nlohmann/json](https://github.com/nlohmann/json).

A `CProjectManifest` holds one manifest. Its `const` methods may be called from any number of threads at once.

### Loading a Manifest

```cpp
struct ProjectError
{
    std::string Message;
    std::string Pointer;    // JSON Pointer to the value the error concerns
    uint32_t Line = 0;
    uint32_t Column = 0;
};

class CProjectManifest
{
public:
    Gem::Result Load(const char *path, std::vector<ProjectError> *pErrors);
    Gem::Result Parse(std::string_view text, const char *path, std::vector<ProjectError> *pErrors);
    // ...
};
```

`Load` reads the manifest file at `path`, a UTF-8 file path. `Parse` reads manifest text already in memory, and takes `path` as the manifest's file path. Each replaces the manifest the object held, and on failure leaves it holding none.

| Result | Meaning |
|---|---|
| `Gem::Result::Success` | The manifest is valid. |
| `Gem::Result::NotFound` | `Load` found no file at `path`. |
| `Gem::Result::IoError` | `Load` could not read the file. |
| `Gem::Result::CorruptedData` | The text is not JSON, or the manifest breaks a rule that loading checks. |
| `Gem::Result::OutOfMemory` | An allocation failed. |

For `CorruptedData`, each error found is appended to `*pErrors`, in document order, when `pErrors` is non-null. Loading checks:

- **JSON.** The text is JSON as [RFC 8259](https://www.rfc-editor.org/rfc/rfc8259) defines it, encoded in UTF-8, optionally beginning with a byte order mark. The members of an object have distinct keys. A syntax error ends reading, so it is the only error reported.
- **Shape.** Every field has the type and form [Project Manifest](#project-manifest) and [project.schema.json](project.schema.json) give it, every required field is present, and each object holds only the keys defined for it.
- **Names.** Every project name, every `references` key and value, and both parts of each `worlds` entry follow CanvasPkg's name rules.
- **Manifest rules.** The [rules](#rules) and [world rules](#worlds) that the manifest decides alone: project names are unique, each `references` value is a project name, each `worlds` entry names a project name and appears once, and `defaultWorld` is a `worlds` entry.

The rules that depend on the packages' content, such as every path resolving and each `worlds` entry naming a world bundle, are checked by reading the packages.

An error's `Line` and `Column` locate the value the error concerns, at its first character, or, for an error about a key, the key's opening quote. A syntax error is located where reading stopped, and has an empty `Pointer`. Both count from 1. A line ends at a line feed, and a carriage return before a line feed is part of the line ending. A column counts code points, so a tab counts as one column, and a byte order mark takes none.

### Reading a Manifest

```cpp
struct ProjectReference
{
    std::string Name;           // the package name used in the package's paths
    std::string ProjectName;    // the project name it maps to
};

struct ProjectPackage
{
    std::string Name;           // project name
    std::string Source;         // as written; empty for a prebuilt package
    std::string Package;        // as written; empty for a package listed by source
    std::string FilePath;       // Source or Package, resolved
    std::vector<ProjectReference> References;
};

struct ProjectWorld
{
    size_t Package;             // index into GetPackages()
    std::string Bundle;
};

constexpr size_t kNoWorld = SIZE_MAX;

class CProjectManifest
{
public:
    const std::string &GetPath() const;
    const std::string &GetName() const;
    const std::string &GetOutput() const;
    std::span<const ProjectPackage> GetPackages() const;
    std::span<const ProjectWorld> GetWorlds() const;
    size_t GetDefaultWorld() const;
    Gem::Result FindPackage(std::string_view name, size_t *pIndex) const;
    // ...
};
```

Packages, references, and worlds are in manifest order. `GetPath` returns the path given to `Load` or `Parse`. `GetDefaultWorld` returns the index of the `defaultWorld` entry in `GetWorlds()`, or `kNoWorld` for a manifest without one. `FindPackage` writes the index of the package with that project name, or returns `Gem::Result::NotFound`.

`FilePath` and `GetOutput` are file paths resolved against the manifest: an absolute path is kept, and a relative path is joined to the directory of the manifest's path. The result uses `/` as its separator, with `.` segments removed and each `..` segment removed with the segment before it, without reading the file system. `GetOutput` resolves `output`, or `build` for a manifest without it.

### Resolving Package Names

```cpp
Gem::Result ResolvePackageName(size_t package, std::string_view declaredName,
                               std::string_view name, size_t *pIndex) const;
```

Resolves `name`, a package name used in the paths of the package at index `package`, by the rules of [Package Names](#package-names), and writes the index of the package it names. `declaredName` is that package's declared name, which rule 1 uses, so resolving reads no package. It returns `Gem::Result::NotFound` when `name` names no package of the project, and `Gem::Result::InvalidArg` when `package` is not an index of `GetPackages()`.

### Writing a Built Project

```cpp
std::string_view GetBuiltFileName(size_t package) const;
Gem::Result FormatBuiltProject(std::string *pText, std::vector<ProjectError> *pErrors) const;
```

`GetBuiltFileName` returns the file name the package at index `package` has in `output`, as [Building a Project](#building-a-project) defines: its package file's name without the `.json` ending, or its `.cpkg` file's name.

`FormatBuiltProject` writes the built project's text to `*pText`, as [Building a Project](#building-a-project) defines its content. The text is UTF-8 with line feeds and no byte order mark, laid out by the rules of `src/Common/JsonText.h`, so the same manifest always produces the same bytes.

| Result | Meaning |
|---|---|
| `Gem::Result::Success` | `*pText` holds the built project. |
| `Gem::Result::CorruptedData` | Two packages have the same built file name. Each package whose built file name an earlier package has is reported in `*pErrors`, at its `source` or `package` value. |
| `Gem::Result::NotOpen` | The object holds no manifest. |
| `Gem::Result::OutOfMemory` | An allocation failed. |

Built file names are compared without regard to the case of ASCII letters, so names that differ only in that case are a build error on every file system.

## Source Layout

| Path | Description |
|---|---|
| `src/CanvasProject/README.md` | This spec |
| `src/CanvasProject/project.schema.json` | JSON Schema for the [project manifest](#project-manifest) |
| `src/Inc/CanvasProject.h` | Public header |
| `src/CanvasProject/ProjectManifest.cpp` | `CProjectManifest` |
| `src/Common/JsonText.h`, `src/Common/JsonText.cpp` | JSON helpers shared with `cpkg`: positions, duplicate and unknown keys, and the JSON layout |

## Future Directions

Directions the current design leaves room for. None are committed plans:

- **Package content hashes**: a built project's entry could record a hash of its package's content, so a package file that is damaged, or was rebuilt without the packages that depend on it, is detectable before its paths are resolved. The same hash supports verifying shipped content.
- **Project editor data**: editor-only organization of a project's bundles, such as display folders that span packages, kept in a separate file beside the manifest.
