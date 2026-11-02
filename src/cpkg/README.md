# cpkg

`cpkg` is the command-line tool for [package sources](../CanvasAuthoring/PackageSource.md) and [projects](../CanvasProject/README.md). It builds `.cpkg` files and projects, validates sources, lists a build's inputs, inspects `.cpkg` files, and edits sources and project manifests. Every command is an operation of the [CanvasAuthoring](../CanvasAuthoring/README.md) library, so a package or project that `cpkg` builds is byte-identical to one an editor builds from the same files.

`cpkg` targets Windows and Linux, and depends on CanvasAuthoring, on CanvasPkg for reading `.cpkg` files, and on [InCommand](../../deps/InCommand/readme.md) for its command line. It links no engine library.

## Command Line

`cpkg` takes a command, its arguments, and options:

```
cpkg <command> [<subcommand>] [<arguments>] [<options>]
```

| Command | Does |
|---|---|
| [`build`](#build) | Builds a package or a project |
| [`validate`](#validate) | Reports the diagnostics a build would report, without writing anything |
| [`inputs`](#inputs) | Writes the [input list](../CanvasAuthoring/README.md#input-lists) of a package or a project, without building |
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
| `--version` | Prints the version of `cpkg` and of the CanvasAuthoring library it uses |

### build

```
cpkg build <package.cpkg.json> [-o <file>] [--project <project.cproj.json>] [--depfile <file>]
cpkg build <project.cproj.json> [-o <directory>] [-j <count>] [--depfile <file>]
```

Builds a package or a project, as CanvasAuthoring's [Output](../CanvasAuthoring/README.md#output) and [Project Builds](../CanvasAuthoring/README.md#project-builds) define.

| Option | Meaning |
|---|---|
| `-o`, `--output` | For a package, the `.cpkg` file to write; by default the package file's name without `.json`, beside it. For a project, the directory to write in place of the manifest's `output`. |
| `--project` | For a package, the authored project to [validate it against](../CanvasAuthoring/README.md#project-context). Without it, the package's paths into other packages are written unchecked. |
| `-j`, `--jobs` | For a project, the most packages built at once; by default the number of hardware threads. |
| `--depfile` | Writes a [dependency file](../CanvasAuthoring/README.md#dependency-files) to this path, its target the built `.cpkg` file or built project. |

A project build rebuilds only the packages whose inputs changed, so running `cpkg build` on a project after a change rebuilds what the change affects, and running it again with nothing changed writes nothing.

### validate

```
cpkg validate <package.cpkg.json> [--project <project.cproj.json>]
cpkg validate <project.cproj.json>
```

Loads and validates a package, or every package a project lists by source together with the manifest, and reports the diagnostics a build would report. It writes no file.

### inputs

```
cpkg inputs <package.cpkg.json> [--project <project.cproj.json>] [-o <file>]
cpkg inputs <project.cproj.json> [-o <file>]
```

Writes the input list of a package or a project: one absolute path per line, naming the same files a build's dependency file would name. It writes to standard output, or to the file `-o` names. A build system that declares a step's inputs before running it uses this list (see [Build Systems](#build-systems)).

### dump

```
cpkg dump <package.cpkg> [--bundle <name>] [--payloads]
```

Prints a `.cpkg` file's header, including its declared package name, and each bundle with its assets: index, type, name, and payload size. `--bundle` limits the listing to one bundle, and `--payloads` adds each payload's fields as CanvasPkg's payload helpers decode them. `dump` reads `.cpkg` files built by any tool, so it serves to inspect prebuilt packages.

### import

```
cpkg import <package.cpkg.json> <file.fbx> --bundle <name> [--bundle-file <file.cbundle.json>]
```

Adds an [import](../CanvasAuthoring/PackageSource.md#imports) of the FBX file to the named bundle, with every import setting at its default, and saves the package source. When the package has no bundle of that name, `import` adds one: inline in the package file, or in a new bundle file that `--bundle-file` names, which the package file then includes. The import's file path is written relative to the JSON file that holds it. `import` validates the package after the change and reports what the import would build, such as name collisions with existing content, so the import's settings can then be adjusted in the source.

### rename, move, and refs

```
cpkg rename <path> <new-name> (--package <package.cpkg.json> | --project <project.cproj.json>)
cpkg move <asset-path> <bundle-path> (--package <package.cpkg.json> | --project <project.cproj.json>)
cpkg refs <path> (--package <package.cpkg.json> | --project <project.cproj.json>)
```

Apply the [refactors](../CanvasAuthoring/README.md#refactors) CanvasAuthoring defines. With `--package`, `<path>` is a path within that package, such as `Village/Crate`, and the refactor updates that package. With `--project`, `<path>` begins with a project name, such as `Levels/Village/Crate`, and the refactor updates every package the project lists by source. A node or placement is named by its bundle path and its name, as an asset is.

`rename` and `move` save every file whose content changed, and report each path in a package listed by `.cpkg` file that still names the old name or location. `refs` lists each reference by file, line, and column, and changes nothing.

### project

```
cpkg project add <project.cproj.json> (--source <package.cpkg.json> | --package <package.cpkg>) [--name <name>] [--reference <name>=<project-name>]...
cpkg project remove <project.cproj.json> <name>
```

`project add` adds a package entry to an authored project, by its source or, for a prebuilt package, by its `.cpkg` file. Its project name is `--name`, by default the package's declared name: the source's `packageName`, or the name in the `.cpkg` file's header. Each `--reference` adds an entry to the package's `references`. The entry's file path is written relative to the manifest.

`project remove` removes the entry with that project name. When a path in another listed package still names the package, `project remove` reports each such path as an error and leaves the manifest unchanged.

Both commands validate the project after the change, and save the manifest when it is valid.

## Results

`cpkg` reports a command's [result](../CanvasAuthoring/README.md#diagnostics) as its exit status:

| Exit status | Meaning |
|---|---|
| `0` | The command succeeded: it reported no error. |
| `1` | The command reported an error. A failed project build leaves no built project in its output directory (see [Project Builds](../CanvasAuthoring/README.md#project-builds)), and a failed command that edits files saves none. |
| `2` | The command line is invalid, such as an unknown command or option or a missing argument. Nothing ran. |

A script or build system that stops on a nonzero exit status therefore stops before it uses any output of a failed command.

## Diagnostics

`cpkg` writes diagnostics to standard error, one per line, and everything else, such as `dump` and `refs` output and an input list without `-o`, to standard output:

```
<file>:<line>:<column>: <severity>: <message> [<location>]
```

`<file>` is the JSON file the diagnostic concerns, as a path relative to the current directory, and `<line>` and `<column>` count from 1. `<severity>` is `error`, `warning`, or `info`. `<location>` is the JSON Pointer to the value. A diagnostic about an FBX file is reported at the import that names the file, so its `<file>`, `<line>`, and `<column>` locate the import, and its message names the FBX file and the object in it. A diagnostic about an image file names the image file and the field that names it. Editors and build tools that recognize `file:line:column:` locations link each diagnostic to its source.

## Build Systems

A build system builds a project with one `cpkg build` step: its output is the built project, and its inputs come from the dependency file the step writes. The project build decides which packages to rebuild, so the build system's files name no package, and a package added to the project changes the manifest, which the dependency file names. The step runs in a build tree of its own through `-o`.

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

## Source Layout

| Path | Description |
|---|---|
| `src/cpkg/README.md` | This spec |
| `src/cpkg/*.cpp` | Implementation |

## Future Directions

Directions the current design leaves room for. None are committed plans:

- **CMake module**: a `canvas_add_project()` function that declares the build step above for a project manifest.
- **Watch mode**: rebuilding a project as its source files change.
- **New files**: commands that create an empty package source or project manifest.
- **Machine-readable diagnostics**: a JSON form of the diagnostics, for tools that present them.
- **Sources from packages**: a command for CanvasAuthoring's [sources from packages](../CanvasAuthoring/README.md#future-directions).
