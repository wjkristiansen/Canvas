# cpkg

`cpkg` is the command-line tool that builds [package sources](PackageSource.md) into [CanvasPkg](../CanvasPkg/README.md) `.cpkg` files, and builds [projects](../CanvasProject/README.md). It also validates sources, inspects `.cpkg` files, imports exchange files (such as FBX) into package sources, applies refactors, and edits project manifests. Editors, scripts, and build systems all build content by running `cpkg`, so content builds identically from each.

`cpkg` depends on CanvasPkg for writing and reading packages, on CanvasProject for resolving package names, on [ufbx](https://github.com/bqqbarbhg/ufbx) for reading FBX files, and on [InCommand](../../deps/InCommand/readme.md) for its command line. It links no engine library. Its strings and file paths are UTF-8, converted to the platform's form only where they reach the operating system.

## Sources and Packages

A package's **source** is what people and editors change, and its `.cpkg` file is **build output**, rebuilt from the source whenever the source changes. The package source format is defined by [PackageSource.md](PackageSource.md) and [package-source.schema.json](package-source.schema.json). An [authored project](../CanvasProject/README.md) lists a project's package sources, and building it produces the built project the engine opens.

A [prebuilt package](../CanvasProject/README.md#project-manifest), a `.cpkg` file a project uses without its source, such as a package from a vendor or from another project, is read-only input. A project lists it by its `.cpkg` file, other packages refer to its bundles and shared assets by path, and `cpkg` reads its contents with `CCpkgReader`, but its content changes only where its source is kept. [`unpack`](#unpack) writes a package source that builds a given `.cpkg` file, so a package received without its source can be brought under editing.

## Command Line

`cpkg` takes a command, its arguments, and options:

```
cpkg <command> [<subcommand>] [<arguments>] [<options>]
```

| Command | Does |
|---|---|
| [`build`](#build) | Builds a package or a project |
| [`validate`](#validate) | Reports the diagnostics a build would report, without writing anything |
| [`dump`](#dump) | Prints the contents of a `.cpkg` file |
| [`unpack`](#unpack) | Writes a package source that builds a `.cpkg` file |
| [`format`](#format) | Rewrites a package file or bundle file in the standard form |
| [`import`](#import) | Adds an import of an exchange file to a bundle of a package source |
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
| `-j`, `--jobs` | For a project, the most packages built at once, at least 1; by default the number of hardware threads. |
| `--depfile` | Writes a [dependency file](#dependency-files) to this path, its target the built `.cpkg` file or built project. |

A build reports an error when `-o` or `--depfile` names a file the build reads or another file it writes, and leaves that file as it was.

A project build rebuilds only the packages whose inputs changed, so running `cpkg build` on a project after a change rebuilds what the change affects, and running it again with nothing changed writes nothing.

### validate

```
cpkg validate <package.cpkg.json> [--project <project.cproj.json>]
cpkg validate <project.cproj.json>
```

Loads and validates a package, or every package a project lists by source together with the manifest, and reports the diagnostics a build would report. For a project, it also opens each prebuilt package, and checks that each `worlds` entry names a world bundle and that the built file names are unique. It runs every stage of the [pipeline](#pipeline) except Write, so it reads every file a build reads, including exchange files, because rules such as the keys of an import's `rename` depend on the file's content. It writes no file.

### dump

```
cpkg dump <package.cpkg> [--bundle <name>] [--payloads]
```

Prints a `.cpkg` file's header, including its declared package name, and each bundle with its assets: index, type, name, and payload size. `--bundle` limits the listing to one bundle, and `--payloads` adds each payload's fields as CanvasPkg's payload helpers decode them. `dump` reads `.cpkg` files built by any tool, so it serves to inspect prebuilt packages.

Names are written as JSON string literals, so each name reads unambiguously, whatever characters it holds. With `--payloads`, each field of a payload other than padding follows its asset, one per line under the name `CanvasPkg.h` gives it, with nested structures and array elements indented beneath it. An array of strings or structures lists its elements, an array of asset indices lists its values, and an array of vertex, key, transform, or image data shows its element count. A payload that breaks the [Payload Layout](../CanvasPkg/README.md#payload-layout) rules is reported as an error, and the listing goes on with the next asset.

### unpack

```
cpkg unpack <package.cpkg> [-o <package.cpkg.json>] [--force]
```

Writes a package source that builds the package: building the source `unpack` writes produces a `.cpkg` file byte for byte identical to the one it read. `unpack` reads the package with `CCpkgReader` and needs no project, since the source keeps paths into other packages as the package holds them.

| Option | Meaning |
|---|---|
| `-o`, `--output` | The package file to write; by default the `.cpkg` file's name with `.json` added, beside it |
| `--force` | Replaces files that already exist. Without it, `unpack` writes nothing when any file it would write exists, and reports each one. |

**Layout.** `unpack` writes the package file and, beside it, one directory per bundle, named for the bundle, holding the bundle's bundle file, `<bundle>.cbundle.json`, and the bundle's binary array and image files. The package file includes each bundle file, in the package's bundle order.

```
levels.cpkg.json
Looks/
  Looks.cbundle.json
  Tri.mesh.bin
  Sky.png
Rig/
  Rig.cbundle.json
  Walk.anim.bin
```

**Arrays.** A mesh, a skeletal animation, or a clip whose large numeric arrays hold more than 256 numbers in all writes them to one [binary array](PackageSource.md#values) file, `<asset>.mesh.bin`, `<asset>.anim.bin`, or `<asset>.clip.bin`, each array a range of the file, in the order the source lists them. The others are written inline.

**Images.** Each encoded image is written verbatim to a file with its encoding's extension, `.png`, `.jpg`, or `.tif`. An image of one encoded image is written in the file form, as `<asset>.png`; a cube image of 6 slices and one mip level in the cubemap form, its faces `<asset>.+X.png` through `<asset>.-Z.png`; and any other image in the general form, its files `<asset>.0.png`, `<asset>.1.png`, and so on, in the image's order.

**Values.** The source writes rotations as quaternions and each file in the [standard form](#editing-sources), so `cpkg format` leaves it unchanged.

**File names.** A bundle's directory and an asset's files are named for the bundle or asset. Each character a file name cannot hold on common file systems, a directory name's final `.` or space, and each `%`, is written as `%` and the character's two hexadecimal digits; a name a file system reserves, such as `CON`, has its first character written that way. A file name already used in its directory, as the output's file system compares names, takes the smallest suffix `~2`, `~3`, and so on, before its extension, that makes it unique.

**Packages it cannot unpack.** Some packages have no source that builds them, such as one whose assets are not in the order a build appends them, one with an import node whose placement is unnamed, or one with a light whose color has an alpha other than 1. `unpack` writes its files to a temporary directory beside the output, builds them, and moves them into place only when the build reproduces the package. Otherwise it writes nothing and reports each asset the source cannot reproduce, naming what it holds that no source expresses. Every package `cpkg` builds unpacks.

### format

```
cpkg format <file> [--check]
```

Rewrites `file`, a package file (`.cpkg.json`) or a bundle file (`.cbundle.json`), in the [standard form](#editing-sources), and saves it only when its text changes. The files a package file includes are left as they are. `format` loads the file as `build` loads it, and saves nothing when loading reports an error.

| Option | Meaning |
|---|---|
| `--check` | Saves nothing, and reports the file as an error when it is not in the standard form |

### import

```
cpkg import <package.cpkg.json> <file> --bundle <name> [--bundle-file <file.cbundle.json>]
```

Adds an [import](PackageSource.md#imports) of the [exchange file](PackageSource.md#exchange-formats) to the named bundle, with every import setting at its default, and saves the package source. When the package has no bundle of that name, `import` adds one: inline in the package file, or in a new bundle file that `--bundle-file` names, which the package file then includes. The import's file path is written relative to the JSON file that holds it. `import` validates the package after the change and reports what the import would build, such as name collisions with existing content, so the import's settings can then be adjusted in the source.

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
| **Expand** | Reads each import's exchange file and applies the import's settings (see [Imports](#imports)), and reads the header of each image file |
| **Validate** | Checks the [validation rules](PackageSource.md#validation) of the package source, and, with a [project](#project-context), the paths into other packages |
| **Resolve** | Turns references by name into asset indices, fills defaults, and orders each bundle's assets in CanvasPkg's [canonical order](../CanvasPkg/README.md#append-order) |
| **Write** | Encodes each asset's payload and writes the package with `CCpkgWriter` |

### Project Context

A package builds without a project. Paths are written unchanged, so a path into another package needs nothing from that package to build, and the packages of a project build independently, in any order.

With an authored project, validation also checks the paths into other packages. The package's own entry is the one whose `source` is its package file, and a project with no such entry is reported as an error at the package file. Each path's package name resolves through the project's [package-name rules](../CanvasProject/README.md#package-names), and the path is checked against the package it names: against its source for a package listed by source, and against its `.cpkg` file, read through `CCpkgReader`'s name directories without reading any payload, for a package listed by `.cpkg` file. Validation therefore needs no package of the project to be built, and checks content against changes that are not yet built.

A path into another package is checked for what the package's names and asset types decide:

- **The package.** The path's package name names a package the project lists.
- **The bundle.** The package has the bundle the path names. A bundle path in a placement or a world's `regions` names a bundle other than a world bundle, one that holds no `WorldSettings` asset.
- **The asset.** An asset path names an asset of the type its field requires. An asset that an import of the named bundle builds is found once the build reads imports (see [Imports](#imports)); until then, a path into a bundle with imports that names no declared asset is accepted.

The rules that depend on an asset's content, such as an image's shape, a skeleton's bones, a bundle's exports, and mobility, are checked within the package. A package listed by source is read through its Load stage alone. When that package's source has an error, the paths into it are not checked, and the first such path is reported as an error that names the package, so the package's own build or validation reports the cause.

### Imports

`cpkg` reads an import's exchange file, an FBX file (see [Exchange Formats](PackageSource.md#exchange-formats)), with ufbx, and converts its content to CanvasPkg data, in CanvasPkg's [conventions](../CanvasPkg/README.md#conventions): it converts units and axes, applies an import's `scale` and `rotation`, triangulates, generates missing normals, identifies bones, and samples clips. It then applies everything else the [import](PackageSource.md#imports) declares: which content is built, its names, replacements, the node hierarchy's `parent`, a shared `skeleton`, and edits.

An FBX camera builds a `Camera` with the camera's near and far planes, converted to meters, and its vertical field of view, in radians. An FBX light builds a `Light` with the light's type, color, intensity, and whether it casts shadows, and, for a spot light, its inner and outer angles, in radians. Its decay builds the attenuation: none is constant, linear decay is linear, and quadratic decay is quadratic.

A texture an FBX file uses is either **embedded**, its encoded bytes held in the FBX file, or **external**, held in an image file the FBX file names. The bytes of an embedded texture become the built image's encoded image, as an external file's bytes do, with no file written. The build reads nothing from disk for an embedded texture; any file name the FBX file records for it serves only to [name the image](PackageSource.md#imported-names). An external texture is found at the relative path the file records, resolved against the FBX file's directory, and otherwise at the absolute path the file records. An external texture found at neither is reported as an error at the import, naming the texture and the material that uses it, unless the import replaces it.

### Images

The build stores image files' bytes unchanged. It identifies each file's encoding from the file's content, by the signature each encoding begins with, and reads the image's width and height from the encoding's header, decoding no pixels: a PNG file's `IHDR` chunk, a JPEG file's start-of-frame segment, and the first image file directory of a TIFF file. A file whose extension names a different encoding than its content, among `.png`, `.jpg`, `.jpeg`, `.tif`, and `.tiff`, is reported as a warning. A file in an encoding CanvasPkg does not store is reported as an error, naming the encoding when the build recognizes it, such as GIF or DDS.

### Output

**Output file.** A build of one package writes the `.cpkg` file `-o` names, by default the package file's name with its `.json` ending removed, beside the package file (see [File extensions](PackageSource.md#files)). A [project build](#project-builds) writes each package to the project's `output` directory.

**Atomic output.** The build writes its package to a temporary file beside the output and replaces the output with it only when the whole package is written. A failed build leaves the previous output as it was. When the new file's bytes equal the existing output's, the build leaves the existing file untouched, with its modification time, so steps that depend on the output have nothing new to act on.

**Deterministic output.** The same source files, the same input file bytes, and the same version of `cpkg` build a byte-identical `.cpkg` file. Nothing about the machine, the time, or where the source lives enters the output: the build stores no timestamps or absolute paths, and the order of everything it writes follows from the source.

**Codecs.** The build chooses each asset's `CpkgCodec`. It stores every asset with `CpkgCodec::None`, the only codec CanvasPkg defines.

### Dependency Files

A build can write a **dependency file** naming its output and every file the build read: the package file, its bundle files, image and binary array files, exchange files and the external textures they use, and the project manifest and other packages' sources or `.cpkg` files read for validation. A build system that reads the dependency file runs the build again when any of those files changes. A build reads a new file only after a change to a file it already read, such as a package file that now includes a new bundle file, so each build's dependency file names every change that decides whether the next build runs. The exception is an [external texture](#imports) found at its absolute path: a file that later appears at its relative path changes no file the build read.

A dependency file is a Makefile-format rule, written in the subset that build systems reading such files share:

- **One rule.** The file holds one rule, `target: inputs`, whose target is the build's output: the `.cpkg` file for a package build, or the built project for a [project build](#project-builds).
- **Paths.** Every path is absolute, uses `/` as its separator, and is encoded in UTF-8.
- **Escapes.** A space in a path is written `\ `, and a `$` is written `$$`.
- **Lines.** The inputs may continue onto following lines; each line that continues ends in ` \`.

A path that holds a `#`, a `\`, a line break, or a `:` other than a drive letter's has no form in this subset. A build asked for a dependency file reports reading such a file as an error, naming the file.

A build that fails leaves the file at the path `--depfile` names as it was. Its output is then out of date, so a build system runs the build again, and the dependency file still names the inputs of the last build that succeeded.

## Project Builds

A **project build** builds an authored project as CanvasProject's [Building a Project](../CanvasProject/README.md#building-a-project) defines: each package listed by source to its `.cpkg` file in the project's `output` directory, each prebuilt package copied there, and the built project beside them. The packages build independently, so a project build builds them concurrently, as many at once as `-j` allows, and validates each against the authored project. `-o` names a directory that takes the place of `output`, such as one inside a build system's build tree.

A project build keeps a record of each package's inputs in `output` beside its `.cpkg` file, and rebuilds only the packages whose recorded inputs changed since the package's last build. A package whose inputs are unchanged keeps its `.cpkg` file. Likewise, a prebuilt package is copied only when its copy in `output` differs from the file the authored project lists. Because output is [deterministic](#output), rebuilding it would produce the same bytes.

**Input records.** A package's record is the file `<name>.inputs` beside its `.cpkg` file in `output`, such as `build/levels.cpkg.inputs`. It is UTF-8 text with one entry per line. Its first line is `cpkg <version>`, the version of `cpkg` that built the package. Each following line is `<size> <time> <path>`: the size in bytes and the modification time, as a decimal count of nanoseconds, of an input, followed by its absolute path with `/` as its separator. The inputs are those a [dependency file](#dependency-files) of the package's build names, including the authored project's manifest, then the `.cpkg` file itself, last. The record ends with the package's warnings, each on a line `warning <diagnostic>` in the [diagnostic](#diagnostics) form, with its file path absolute.

A package is **up to date** when its record exists, names the version of `cpkg` running, and every file it lists has the size and modification time the record gives. A package that is up to date does not build: the project build reports the warnings its record holds, as its build reported them, so a build with nothing changed reports the same diagnostics as the one before it. A package that is not up to date builds, and the build replaces its record. A package with an error has its record removed, so it builds again next time.

**Concurrent builds.** Each package's diagnostics are reported together, the packages in manifest order, so a project build reports the same diagnostics in the same order for every `-j`.

**Output names.** The file names a project build writes in `output` are unique, as CanvasProject's [Building a Project](../CanvasProject/README.md#building-a-project) requires. Beyond the comparison CanvasProject makes, the build claims each name by creating a file of that name in a new temporary directory in `output`, failing when the file already exists, so two names that the file system holding `output` treats as one, such as names that differ only in the case of non-ASCII letters, are reported as an error at the second package's entry. The directory is removed before the build ends.

A project build runs in stages, and each stage runs only when the stage before it succeeds:

1. **Packages.** Every package listed by source builds, and the build reports the diagnostics of every package. Each `.cpkg` file is written [atomically](#output): a package with an error builds no `.cpkg` file, and its earlier file stays as it was.
2. **Built project.** When every package built, the build copies the prebuilt packages into `output`, each atomically, and writes the built project, and, with `--depfile`, its [dependency file](#dependency-files). When any package failed, the build writes neither, and removes any built project an earlier build left in `output`.

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

A step that generates a file a package reads, such as a tool that writes an image, is declared as a dependency of the `cpkg build` step, such as with `DEPENDS` in CMake, so it runs first.

Because `cpkg` leaves unchanged output files untouched, steps that depend on the built project run only when the build changed it.

A build system can also run `cpkg build` on every build, without a dependency file, such as through a CMake custom target, which always runs. The project build then decides from its input records what to rebuild, and with nothing changed it writes nothing.

## Editing Sources

The commands that change files, `import`, `rename`, `move`, and `project`, load the files they change, apply the change, validate the result, and save only the files whose content changed. A command that reports an error saves no file.

**Round trip.** A save keeps each file's structure (which bundles are inline and which are included), the order of keys and array entries, `$comment` and `$extras` keys and their values as written, and the form in which each value was written: a rotation in degrees stays in degrees, and a binary array file stays a file. A change rewrites only what it changes, and each file is saved in the standard form.

**Standard form.** A file in the standard form is written in `cpkg`'s one fixed formatting. Each number written with a fraction or an exponent is written as an integer when its value is one, and otherwise in the shortest form that reads back as the same float. Each field whose default is a fixed value is omitted when it equals that default after conversion to the stored type, such as a transform's `[1, 1, 1]` scale or a rotation that is the identity in either form; a field whose default is computed, such as a mesh's `bounds`, and a field of an [edit](PackageSource.md#editing-imported-nodes), which replaces what it lists, are kept. A file already in the standard form saves byte for byte unchanged, so a change appears in a text diff as just that change.

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
| File | The JSON file, exchange file, or image file the finding concerns |
| Location | For a JSON file, a JSON Pointer to the value, with its line and column; for an exchange file, the import's location and the name of the object in the file |

A build reports every finding it can before it stops: Load and Validate check the whole source, not only up to its first error. A source with any error builds no package. Warnings and info do not change what is built, and `--werror` treats warnings as errors.

`cpkg` writes diagnostics to standard error, one per line, and everything else, such as `dump` and `refs` output, to standard output:

```
<file>:<line>:<column>: <severity>: <message> [<location>]
```

`<file>` is the JSON file the diagnostic concerns, as a path relative to the current directory, and `<line>` and `<column>` count from 1. `<location>` is the JSON Pointer to the value. A diagnostic about an exchange file is reported at the import that names the file, so its `<file>`, `<line>`, and `<column>` locate the import, and its message names the exchange file and the object in it. A diagnostic about an image file names the image file and the field that names it. Editors and build tools that recognize `file:line:column:` locations link each diagnostic to its source.

A diagnostic about a file as a whole, such as a `.cpkg` file that `dump` cannot read, has no line, column, or location:

```
<file>: <severity>: <message>
```

A diagnostic about a place in a JSON file that holds no value, such as a syntax error, has no location:

```
<file>:<line>:<column>: <severity>: <message>
```

An invalid command line is reported as `cpkg: error: <message>`, with exit status `2`.

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
- **Input lists**: a command that names the files a build would read without building, for build systems that declare a step's inputs before running it. It would name the files that list further inputs, such as bundle and exchange files, apart from the others, and name files that a generating step has yet to write.
- **Watch mode**: rebuilding a project as its source files change.
- **New files**: commands that create an empty package source or project manifest.
- **Incremental builds**: reusing the payloads of assets whose inputs are unchanged since the last build, keyed by hashes of those inputs.
- **Image processing**: converting images in other encodings to stored ones, generating mip chains, and producing block-compressed encodings once CanvasPkg defines them.
- **Import filters**: leaving content of an exchange file out of an import by kind or by name, such as the default camera and light some FBX exporters add to every file.
- **Naming policy**: project-configurable naming conventions that validation reports as warnings, kept separate from the [name rules](../CanvasPkg/README.md#names) every package follows.
- **Project editor data**: editor-only organization of a project's bundles, such as display folders (see CanvasProject's [Future Directions](../CanvasProject/README.md#future-directions)).
