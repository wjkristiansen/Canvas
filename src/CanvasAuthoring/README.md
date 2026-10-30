# CanvasAuthoring

CanvasAuthoring is a static library that reads, edits, validates, and builds [package sources](PackageSource.md). A package source is the editable form of a [CanvasPkg](../CanvasPkg/README.md) package, and building it produces the package's `.cpkg` file. The `cpkg` command-line tool and editors use the library, so a package builds identically from each.

CanvasAuthoring depends on CanvasPkg for writing packages, on [CanvasProject](../CanvasProject/README.md) for resolving package names, and on CanvasPkgFbx for reading FBX files. It links no engine library, so tools that use it build and run without the engine. It targets Windows and Linux. Its strings and file paths are UTF-8, converted to the platform's form only where they reach the operating system.

## Sources and Packages

A package's **source** is what people and editors change, and its `.cpkg` file is **build output**, rebuilt from the source whenever the source changes. An editor opens a package source, and the project manifest that lists the package, and saves both. It builds `.cpkg` files to run or ship the content they describe.

A `.cpkg` file without a source, such as a package received from a vendor, is read-only input. A project lists it like any package, other packages refer to its bundles and shared assets by path, and tools read its contents with `CCpkgReader`, but its content changes only where its source is kept.

## Pipeline

A build runs these stages in order:

| Stage | Does |
|---|---|
| **Load** | Reads the package file and the bundle files it includes into a [source model](#source-model), and checks each file against [package-source.schema.json](package-source.schema.json) |
| **Translate** | Reads each import's FBX file through CanvasPkgFbx and applies the import's settings (see [Imports](#imports)), and reads the header of each image file |
| **Validate** | Checks the [validation rules](PackageSource.md#validation) of the package source, and, with a [project](#project-context), the paths into other packages |
| **Resolve** | Turns references by name into asset indices and bone indices, fills defaults, and orders each bundle's assets in CanvasPkg's [canonical order](../CanvasPkg/README.md#append-order) |
| **Write** | Encodes each asset's payload and writes the package with `CCpkgWriter` |

**Validate** runs every stage except Write. It reads every file a build reads, including FBX files, because rules such as the keys of an import's `rename` depend on the file's content, and it reports what a build would report.

## Source Model

The **source model** is the in-memory form of a package source: the package, its bundles, and each member of each bundle, in the shape [PackageSource.md](PackageSource.md) defines. It is the surface editors change, and the library's validation, refactors, and build all operate on it.

| Operation | Does |
|---|---|
| Load | Reads a package file and its bundle files into a model |
| Save | Writes a model's files back, writing only the files whose content changed |
| Edit | Adds, removes, and changes bundles, assets, nodes, and imports, and any field of them |
| Validate | Reports the [diagnostics](#diagnostics) a build would report, without writing a package |
| Build | Writes the model's package to a `.cpkg` file |
| Refactor | Applies a [refactor](#refactors) across the model, and across a project's models when given |

**Round trip.** Saving a model that was loaded and not changed reproduces the files' JSON values, and a change rewrites only what it changes. The model keeps each file's structure (which bundles are inline and which are included), the order of keys and array entries, `x-` keys, and the form in which each value was written: a rotation in degrees stays in degrees, a binary array file stays a file, and an omitted field stays omitted rather than being written with its default. Save writes JSON in one fixed formatting, so a file already in that formatting saves byte for byte unchanged, and a change to a source appears in a text diff as just that change.

**Concurrency.** Separate models may be loaded, changed, and built concurrently on separate threads. One model is changed by one thread at a time, and a build reads its model without changing it.

## Refactors

A **refactor** changes a name or a location and updates everything that refers to it, so a source that was valid before a refactor is valid after it:

| Refactor | Updates |
|---|---|
| Rename an asset, node, or placement | References by name within its bundle, and paths to it |
| Rename a bundle | Paths to the bundle and to its shared assets |
| Move a shared asset to another bundle | Paths to the asset, and references by name that become paths |
| Find references | Reports every reference to a bundle, asset, or node, without changing anything |

Without a project, a refactor updates the package being refactored. With a project, it updates every package of the project whose source is loaded, through the project names and `references` each package uses, and reports paths in packages without loaded sources that still refer to the old name or location, since those packages change only where their sources are kept.

## Project Context

A package builds without a project. Paths are written unchanged, so a path into another package needs nothing from that package to build, and the packages of a project build independently, in any order.

With a project, validation also checks the paths into other packages. The caller supplies the project manifest and the package's project name, or the library finds the manifest entry whose location is the build's output file. Each path's package name resolves through the project's [package-name rules](../CanvasProject/README.md#package-names), and the path is checked against the package it names: against that package's source when the caller has loaded it, and otherwise against its `.cpkg` file, read through `CCpkgReader`'s name directories without reading any payload. An editor therefore validates content that refers to packages whose changes are not yet built.

## Imports

CanvasPkgFbx reads an FBX file and translates it to CanvasPkg data in memory, in CanvasPkg's [conventions](../CanvasPkg/README.md#conventions): it converts units and axes, applies an import's `scale` and `rotation`, triangulates, generates missing normals, identifies bones, and samples animation stacks. CanvasAuthoring applies everything else an [import](PackageSource.md#imports) declares: which content is built, its names, replacements, clip ranges, the node hierarchy's `parent`, a shared `skeleton`, and edits. The boundary keeps every detail of the FBX file format in CanvasPkgFbx, and every rule of the package source in CanvasAuthoring.

A texture an FBX file uses is either **embedded**, its encoded bytes held in the FBX file, or **external**, held in an image file the FBX file names. The bytes of an embedded texture become the built image's encoded image, as an external file's bytes do, with no file written. The build reads nothing from disk for an embedded texture; any file name the FBX file records for it serves only to [name the image](PackageSource.md#imported-names). An external texture is found at the relative path the file records, resolved against the FBX file's directory, and otherwise at the absolute path the file records. An external texture found at neither is reported as an error at the import, naming the texture and the material that uses it, unless the import replaces it.

## Images

The build stores image files' bytes unchanged. It identifies each file's encoding from the file's content, by the signature each encoding begins with, and reads the image's width and height from the encoding's header, decoding no pixels. A file whose extension names a different encoding than its content is reported as a warning. A file in an encoding CanvasPkg does not store is reported as an error.

## Diagnostics

Every stage reports its findings as **diagnostics**, each with:

| Field | Holds |
|---|---|
| Severity | `error`, `warning`, or `info` |
| Message | What was found, in terms of the source |
| File | The JSON file, FBX file, or image file the finding concerns |
| Location | For a JSON file, a JSON Pointer to the value, with its line and column; for an FBX file, the import's location and the name of the object in the file |

A build reports every finding it can before it stops: Load and Validate check the whole source, not only up to its first error. A source with any error builds no package. Warnings and info do not change what is built, and a build option treats warnings as errors. Diagnostics that CanvasPkgFbx reports while reading a file are reported at the import that names the file.

## Output

**Output file.** The build writes the `.cpkg` file the caller names, by default the package file's name with its `.json` ending removed (see [File extensions](PackageSource.md#files)).

**Atomic output.** The build writes its package to a temporary file beside the output and replaces the output with it only when the whole package is written. A failed build leaves the previous output as it was.

**Deterministic output.** The same source files, the same input file bytes, and the same version of the library build a byte-identical `.cpkg` file. Nothing about the machine, the time, or where the source lives enters the output: the build stores no timestamps or absolute paths, and the order of everything it writes follows from the source.

**Codecs.** The build chooses each asset's `CpkgCodec`. It stores every asset with `CpkgCodec::None`, the only codec CanvasPkg defines.

**Dependency file.** A build can write a dependency file: a Makefile-format rule naming the output and every file the build read. These are the package file, its bundle files, image and binary array files, FBX files and the external textures they use, and the project manifest and other packages' sources or `.cpkg` files read for validation. A build system that reads the dependency file rebuilds a package when any of them changes.

## Source Layout

| Path | Description |
|---|---|
| `src/CanvasAuthoring/README.md` | This spec |
| `src/CanvasAuthoring/PackageSource.md` | The package source spec |
| `src/CanvasAuthoring/package-source.schema.json` | JSON Schema for package source files |
| `src/CanvasAuthoring/Inc/CanvasAuthoring.h` | Public header |
| `src/CanvasAuthoring/*.cpp` | Implementation |

## Future Directions

Directions the current design leaves room for. None are committed plans:

- **Library API**: the classes and functions for the operations above, with the `cpkg` tool and editors as consumers.
- **Incremental builds**: reusing the payloads of assets whose inputs are unchanged since the last build, keyed by hashes of those inputs.
- **Image processing**: converting images in other encodings to stored ones, generating mip chains, and producing block-compressed encodings once CanvasPkg defines them.
- **Import filters**: leaving content of an FBX file out of an import by kind or by name, such as the default camera and light some exporters add to every file.
- **Naming policy**: project-configurable naming conventions that validation reports as warnings, kept separate from the [name rules](../CanvasPkg/README.md#names) every package follows.
- **Project editor data**: editor-only organization of a project's bundles, such as display folders (see CanvasProject's [Future Directions](../CanvasProject/README.md#future-directions)).
- **Sources from packages**: producing a package source from a `.cpkg` file, so a package received without its source can be brought under editing.
