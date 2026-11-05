# cpkg

`cpkg` is the command-line tool that builds [package sources](PackageSource.md) into [CanvasPkg](../CanvasPkg/README.md) `.cpkg` files, and builds [projects](../CanvasProject/README.md). It also validates sources, lists a build's inputs, inspects `.cpkg` files, imports FBX files into package sources, applies refactors, and edits project manifests. Editors, scripts, and build systems all build content by running `cpkg`, so content builds identically from each.

`cpkg` depends on CanvasPkg for writing and reading packages, on CanvasProject for resolving package names, on [ufbx](https://github.com/bqqbarbhg/ufbx) for reading FBX files, and on [InCommand](../../deps/InCommand/readme.md) for its command line. It links no engine library. Its strings and file paths are UTF-8, converted to the platform's form only where they reach the operating system.

## Sources and Packages

A package's **source** is what people and editors change, and its `.cpkg` file is **build output**, rebuilt from the source whenever the source changes. The package source format is defined by [PackageSource.md](PackageSource.md) and [package-source.schema.json](package-source.schema.json). An [authored project](../CanvasProject/README.md) lists a project's package sources, and building it produces the built project the engine opens.

A [prebuilt package](../CanvasProject/README.md#project-manifest), a `.cpkg` file a project uses without its source, such as a package from a vendor or from another project, is read-only input. A project lists it by its `.cpkg` file, other packages refer to its bundles and shared assets by path, and `cpkg` reads its contents with `CCpkgReader`, but its content changes only where its source is kept.

## Command Line

`cpkg` takes a command, its arguments, and options:

```
cpkg <command> [<subcommand>] [<arguments>] [<options>]
```

| Command | Does |
|---|---|
| [`build`](#build) | Builds a package or a project |
| [`validate`](#validate) | Reports the diagnostics a build would report, without writing anything |
| [`inputs`](#inputs) | Writes the [input list](#input-lists) of a package or a project, without building |
| [`dump`](#dump) | Prints the contents of a `.cpkg` file |
| [`import`](#import) | Adds an FBX import to a bundle of a package source |
| [`rename`](#rename-move-and-refs) | Renames an asset, node, placement, or bundle, and updates everything that refers to it |
| [`move`](#rename-move-and-refs) | Moves a shared asset to another bundle, and updates everything that refers to it |
| [`refs`](#rename-move-and-refs) | Lists every reference to a bundle, asset, or node |
| [`project`](#project) | Adds packages to a project manifest and removes them |

Each command takes the file it works on as its first argument and recognizes it by its extension: `.cpkg.json` for a package source, `.cproj.json` for a project manifest, and `.cpkg` for a built package. File paths on the command line are absolute or relative to the current directory.

These options apply to every command:

| Option | Meaning |
|---|---|
| `-h`, `--help` | Prints help for the command and exits |
| `--werror` | Treats warnings as errors |
| `-q`, `--quiet` | Reports errors only |
| `--version` | Prints the version of `cpkg` |

### build

```
cpkg build <package.cpkg.json> [-o <file>] [--project <project.cproj.json>] [--depfile <file>]
cpkg build <project.cproj.json> [-o <directory>] [-j <count>] [--depfile <file>]
```

Builds a package, as [Building a Package](#building-a-package) defines, or a project, as [Project Builds](#project-builds) defines.

| Option | Meaning |
|---|---|
| `-o`, `--output` | For a package, the `.cpkg` file to write; by default the package file's name without `.json`, beside it. For a project, the directory to write in place of the manifest's `output`. |
| `--project` | For a package, the authored project to [validate it against](#project-context). Without it, the package's paths into other packages are written unchecked. |
| `-j`, `--jobs` | For a project, the most packages built at once; by default the number of hardware threads. |
| `--depfile` | Writes a [dependency file](#dependency-files) to this path, its target the built `.cpkg` file or built project. |

A project build rebuilds only the packages whose inputs changed, so running `cpkg build` on a project after a change rebuilds what the change affects, and running it again with nothing changed writes nothing.

### validate

```
cpkg validate <package.cpkg.json> [--project <project.cproj.json>]
cpkg validate <project.cproj.json>
```

Loads and validates a package, or every package a project lists by source together with the manifest, and reports the diagnostics a build would report. It runs every stage of the [pipeline](#pipeline) except Write, so it reads every file a build reads, including FBX files, because rules such as the keys of an import's `rename` depend on the file's content. It writes no file.

### inputs

```
cpkg inputs <package.cpkg.json> [--project <project.cproj.json>] [-o <file>]
cpkg inputs <project.cproj.json> [-o <file>]
```

Writes the [input list](#input-lists) of a package or a project to standard output, or to the file `-o` names.

### dump

```
cpkg dump <package.cpkg> [--bundle <name>] [--payloads]
```

Prints a `.cpkg` file's header, including its declared package name, and each bundle with its assets: index, type, name, and payload size. `--bundle` limits the listing to one bundle, and `--payloads` adds each payload's fields as CanvasPkg's payload helpers decode them. `dump` reads `.cpkg` files built by any tool, so it serves to inspect prebuilt packages.

### import

```
cpkg import <package.cpkg.json> <file.fbx> --bundle <name> [--bundle-file <file.cbundle.json>]
```

Adds an [import](PackageSource.md#imports) of the FBX file to the named bundle, with every import setting at its default, and saves the package source. When the package has no bundle of that name, `import` adds one: inline in the package file, or in a new bundle file that `--bundle-file` names, which the package file then includes. The import's file path is written relative to the JSON file that holds it. `import` validates the package after the change and reports what the import would build, such as name collisions with existing content, so the import's settings can then be adjusted in the source.

### rename, move, and refs

```
cpkg rename <path> <new-name> (--package <package.cpkg.json> | --project <project.cproj.json>)
cpkg move <asset-path> <bundle-path> (--package <package.cpkg.json> | --project <project.cproj.json>)
cpkg refs <path> (--package <package.cpkg.json> | --project <project.cproj.json>)
```

Apply the [refactors](#refactors). With `--package`, `<path>` is a path within that package, such as `Village/Crate`, and the refactor updates that package. With `--project`, `<path>` begins with a project name, such as `Levels/Village/Crate`, and the refactor updates every package the project lists by source. A node or placement is named by its bundle path and its name, as an asset is. `refs` lists each reference by file, line, and column, and changes nothing.

### project

```
cpkg project add <project.cproj.json> (--source <package.cpkg.json> | --package <package.cpkg>) [--name <name>] [--reference <name>=<project-name>]...
cpkg project remove <project.cproj.json> <name>
```

`project add` adds a package entry to an authored project, by its source or, for a prebuilt package, by its `.cpkg` file. Its project name is `--name`, by default the package's declared name: the source's `packageName`, or the name in the `.cpkg` file's header. Each `--reference` adds an entry to the package's `references`. The entry's file path is written relative to the manifest.

`project remove` removes the entry with that project name. When a path in another listed package still names the package, `project remove` reports each such path as an error and leaves the manifest unchanged.

Both commands validate the project after the change, and save the manifest when it is valid.

## Building a Package

### Pipeline

A build runs these stages in order:

| Stage | Does |
|---|---|
| **Load** | Reads the package file and the bundle files it includes, and checks each file against [package-source.schema.json](package-source.schema.json) |
| **Translate** | Reads each import's FBX file and applies the import's settings (see [Imports](#imports)), and reads the header of each image file |
| **Validate** | Checks the [validation rules](PackageSource.md#validation) of the package source, and, with a [project](#project-context), the paths into other packages |
| **Resolve** | Turns references by name into asset indices and joint indices, fills defaults, and orders each bundle's assets in CanvasPkg's [canonical order](../CanvasPkg/README.md#append-order) |
| **Write** | Encodes each asset's payload and writes the package with `CCpkgWriter` |

### Project Context

A package builds without a project. Paths are written unchanged, so a path into another package needs nothing from that package to build, and the packages of a project build independently, in any order.

With an authored project, validation also checks the paths into other packages. The package's own entry is the one whose `source` is its package file. Each path's package name resolves through the project's [package-name rules](../CanvasProject/README.md#package-names), and the path is checked against the package it names: against its source for a package listed by source, and against its `.cpkg` file, read through `CCpkgReader`'s name directories without reading any payload, for a package listed by `.cpkg` file. Validation therefore needs no package of the project to be built, and checks content against changes that are not yet built.

### Imports

`cpkg` reads an FBX file with ufbx and translates its content to CanvasPkg data, in CanvasPkg's [conventions](../CanvasPkg/README.md#conventions): it converts units and axes, applies an import's `scale` and `rotation`, triangulates, generates missing normals, identifies joints, and samples animation stacks. It then applies everything else the [import](PackageSource.md#imports) declares: which content is built, its names, replacements, clip ranges, the node hierarchy's `parent`, a shared `skeleton`, and edits. `cpkg`'s FBX reading is its own, separate from CanvasFbx, which builds engine model data from FBX files for tools such as the model viewer.

A texture an FBX file uses is either **embedded**, its encoded bytes held in the FBX file, or **external**, held in an image file the FBX file names. The bytes of an embedded texture become the built image's encoded image, as an external file's bytes do, with no file written. The build reads nothing from disk for an embedded texture; any file name the FBX file records for it serves only to [name the image](PackageSource.md#imported-names). An external texture is found at the relative path the file records, resolved against the FBX file's directory, and otherwise at the absolute path the file records. An external texture found at neither is reported as an error at the import, naming the texture and the material that uses it, unless the import replaces it.

### Images

The build stores image files' bytes unchanged. It identifies each file's encoding from the file's content, by the signature each encoding begins with, and reads the image's width and height from the encoding's header, decoding no pixels. A file whose extension names a different encoding than its content is reported as a warning. A file in an encoding CanvasPkg does not store is reported as an error.

### Output

**Output file.** A build of one package writes the `.cpkg` file `-o` names, by default the package file's name with its `.json` ending removed, beside the package file (see [File extensions](PackageSource.md#files)). A [project build](#project-builds) writes each package to the project's `output` directory.

**Atomic output.** The build writes its package to a temporary file beside the output and replaces the output with it only when the whole package is written. A failed build leaves the previous output as it was. When the new file's bytes equal the existing output's, the build leaves the existing file untouched, with its modification time, so steps that depend on the output have nothing new to act on.

**Deterministic output.** The same source files, the same input file bytes, and the same version of `cpkg` build a byte-identical `.cpkg` file. Nothing about the machine, the time, or where the source lives enters the output: the build stores no timestamps or absolute paths, and the order of everything it writes follows from the source.

**Codecs.** The build chooses each asset's `CpkgCodec`. It stores every asset with `CpkgCodec::None`, the only codec CanvasPkg defines.

### Dependency Files

A build can write a **dependency file** naming its output and every file the build read: the package file, its bundle files, image and binary array files, FBX files and the external textures they use, and the project manifest and other packages' sources or `.cpkg` files read for validation. A build system that reads the dependency file runs the build again when any of those files changes. A build reads a new file only after a change to a file it already read, such as a package file that now includes a new bundle file, so each build's dependency file names every change that decides whether the next build runs. The exception is an [external texture](#imports) found at its absolute path: a file that later appears at its relative path changes no file the build read.

A dependency file is a Makefile-format rule, written in the subset that build systems reading such files share:

- **One rule.** The file holds one rule, `target: inputs`, whose target is the build's output: the `.cpkg` file for a package build, or the built project for a [project build](#project-builds).
- **Paths.** Every path is absolute, uses `/` as its separator, and is encoded in UTF-8.
- **Escapes.** A space in a path is written `\ `, and a `$` is written `$$`.
- **Lines.** The inputs may continue onto following lines; each line that continues ends in ` \`.

A path that holds a `#`, a `\`, a line break, or a `:` other than a drive letter's has no form in this subset. A build asked for a dependency file reports reading such a file as an error, naming the file.

### Input Lists

An **input list** names the files a build would read, without building. To produce it, `cpkg` loads the sources and reads each FBX file far enough to find the external textures it uses, and writes no output. A build system that declares a step's inputs before running it uses the input list where others use the dependency file.

An input list holds one absolute path per line, with `/` as its separator, encoded in UTF-8. It names the same files a build of the same sources records in its dependency file, so the two agree. An input list is available for a package and for a project; a project's input list names the inputs of every package and the files of its prebuilt packages.

## Project Builds

A **project build** builds an authored project as CanvasProject's [Building a Project](../CanvasProject/README.md#building-a-project) defines: each package listed by source to its `.cpkg` file in the project's `output` directory, each prebuilt package copied there, and the built project beside them. The packages build independently, so a project build builds them concurrently, as many at once as `-j` allows, and validates each against the authored project. `-o` names a directory that takes the place of `output`, such as one inside a build system's build tree.

A project build keeps a record of each package's inputs in `output` beside its `.cpkg` file, and rebuilds only the packages whose recorded inputs changed since the package's last build. A package whose inputs are unchanged keeps its `.cpkg` file. Likewise, a prebuilt package is copied only when its copy in `output` differs from the file the authored project lists. Because output is [deterministic](#output), rebuilding it would produce the same bytes.

A project build runs in stages, and each stage runs only when the stage before it succeeds:

1. **Packages.** Every package listed by source builds, and the build reports the diagnostics of every package. Each `.cpkg` file is written [atomically](#output): a package with an error builds no `.cpkg` file, and its earlier file stays as it was.
2. **Built project.** When every package built, the build copies the prebuilt packages into `output`, each atomically, and writes the built project, and, with `--depfile`, its [dependency file](#dependency-files). When any package failed, the build writes neither, and removes any built project an earlier build left in `output` and the dependency file `--depfile` names.

A failed build may leave package files from earlier builds in `output`, but no built project lists them, so the engine never opens a mix of packages that no single build produced together.

**Ready to open.** A successful project build leaves `output` ready for the engine as it stands: the built project and every package it lists are in `output`, and the built project names each package by its file name, so the engine opens the built project in `output` and moves no file. `output` is complete in itself, so it can be moved or shipped as a whole, and the built project opens wherever the directory is.

### Build Systems

A build system builds a project with one `cpkg build` step: its output is the built project, and its inputs are those the step's dependency file names. The project build decides which packages to rebuild, so the build system's files name no package, and a package added to the project changes the manifest, which the dependency file names. The step writes to a build tree of its own through `-o`.

With CMake:

```cmake
set(CONTENT_DIR ${CMAKE_BINARY_DIR}/content)
add_custom_command(
  OUTPUT  ${CONTENT_DIR}/game.cproj.json
  COMMAND cpkg build ${CMAKE_CURRENT_SOURCE_DIR}/content/game.cproj.json
          -o ${CONTENT_DIR} --depfile ${CONTENT_DIR}/game.cproj.d
  DEPFILE ${CONTENT_DIR}/game.cproj.d
  VERBATIM)
add_custom_target(GameContent ALL DEPENDS ${CONTENT_DIR}/game.cproj.json)
```

With Meson:

```meson
content = custom_target('GameContent',
  input:   'content/game.cproj.json',
  output:  'game.cproj.json',
  depfile: 'game.cproj.d',
  command: ['cpkg', 'build', '@INPUT@', '-o', '@OUTDIR@', '--depfile', '@DEPFILE@'],
  build_by_default: true)
```

A step that generates a file a package reads, such as a tool that writes an image, is declared as a dependency of the `cpkg build` step, such as with `DEPENDS` in CMake, so it runs first. A build system that declares a step's inputs before running it, rather than reading a dependency file, takes them from `cpkg inputs`.

Because `cpkg` leaves unchanged output files untouched, steps that depend on the built project run only when the build changed it.

## Editing Sources

The commands that change files, `import`, `rename`, `move`, and `project`, load the files they change, apply the change, validate the result, and save only the files whose content changed. A command that reports an error saves no file.

**Round trip.** A save reproduces each file's JSON values, and a change rewrites only what it changes. It keeps each file's structure (which bundles are inline and which are included), the order of keys and array entries, `x-` keys, and the form in which each value was written: a rotation in degrees stays in degrees, a binary array file stays a file, and an omitted field stays omitted rather than being written with its default. `cpkg` writes JSON in one fixed formatting, so a file already in that formatting saves byte for byte unchanged, and a change appears in a text diff as just that change.

### Refactors

A **refactor** changes a name or a location and updates everything that refers to it, so a source that was valid before a refactor is valid after it:

| Refactor | Updates |
|---|---|
| Rename an asset, node, or placement | References by name within its bundle, and paths to it |
| Rename a bundle | Paths to the bundle and to its shared assets |
| Move a shared asset to another bundle | Paths to the asset, and references by name that become paths |
| Find references | Reports every reference to a bundle, asset, or node, without changing anything |

Without a project, a refactor updates the package being refactored. With a project, it updates every package the project lists by source, through the project names and `references` each package uses, and reports paths in packages the project lists by `.cpkg` file that still refer to the old name or location, since those packages change only where their sources are kept.

## Results

Every command returns a result, separate from its diagnostics, that states whether it succeeded: it succeeds exactly when it reports no error. `cpkg` reports the result as its exit status:

| Exit status | Meaning |
|---|---|
| `0` | The command succeeded: it reported no error. |
| `1` | The command reported an error. A failed project build leaves no built project in its output directory (see [Project Builds](#project-builds)), and a failed command that edits files saves none. |
| `2` | The command line is invalid, such as an unknown command or option or a missing argument. Nothing ran. |

A caller such as a script, an editor, or a build system therefore detects a failure from the exit status alone, and stops before it uses any output of the failed command.

## Diagnostics

Every stage reports its findings as **diagnostics**, each with:

| Field | Holds |
|---|---|
| Severity | `error`, `warning`, or `info` |
| Message | What was found, in terms of the source |
| File | The JSON file, FBX file, or image file the finding concerns |
| Location | For a JSON file, a JSON Pointer to the value, with its line and column; for an FBX file, the import's location and the name of the object in the file |

A build reports every finding it can before it stops: Load and Validate check the whole source, not only up to its first error. A source with any error builds no package. Warnings and info do not change what is built, and `--werror` treats warnings as errors.

`cpkg` writes diagnostics to standard error, one per line, and everything else, such as `dump` and `refs` output and an input list without `-o`, to standard output:

```
<file>:<line>:<column>: <severity>: <message> [<location>]
```

`<file>` is the JSON file the diagnostic concerns, as a path relative to the current directory, and `<line>` and `<column>` count from 1. `<location>` is the JSON Pointer to the value. A diagnostic about an FBX file is reported at the import that names the file, so its `<file>`, `<line>`, and `<column>` locate the import, and its message names the FBX file and the object in it. A diagnostic about an image file names the image file and the field that names it. Editors and build tools that recognize `file:line:column:` locations link each diagnostic to its source.

## Source Layout

| Path | Description |
|---|---|
| `src/cpkg/README.md` | This spec |
| `src/cpkg/PackageSource.md` | The package source spec |
| `src/cpkg/package-source.schema.json` | JSON Schema for package source files |
| `src/cpkg/*.cpp` | Implementation |

## Future Directions

Directions the current design leaves room for. None are committed plans:

- **Editor**: a GUI editor that opens an authored project, edits its package sources and manifest, and runs `cpkg` to validate and build them, so creators define and build a project without a text editor or a build tool.
- **Authoring library**: `cpkg`'s loading, validation, and refactors offered as a library, for an editor that needs them in-process, such as to validate as content changes.
- **Machine-readable diagnostics**: a JSON form of the diagnostics, for editors and other tools that present them.
- **CMake module**: a `canvas_add_project()` function that declares the [build step](#build-systems) for a project manifest.
- **Watch mode**: rebuilding a project as its source files change.
- **New files**: commands that create an empty package source or project manifest.
- **Incremental builds**: reusing the payloads of assets whose inputs are unchanged since the last build, keyed by hashes of those inputs.
- **Image processing**: converting images in other encodings to stored ones, generating mip chains, and producing block-compressed encodings once CanvasPkg defines them.
- **Import filters**: leaving content of an FBX file out of an import by kind or by name, such as the default camera and light some exporters add to every file.
- **Naming policy**: project-configurable naming conventions that validation reports as warnings, kept separate from the [name rules](../CanvasPkg/README.md#names) every package follows.
- **Project editor data**: editor-only organization of a project's bundles, such as display folders (see CanvasProject's [Future Directions](../CanvasProject/README.md#future-directions)).
- **Sources from packages**: producing a package source from a `.cpkg` file, so a package received without its source can be brought under editing.
