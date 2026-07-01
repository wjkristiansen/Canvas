# CanvasPackage Implementation Plan

Each session is designed to be completable in one sitting and to leave the codebase in a
clean, compiling, tested state. Sessions may be spread across multiple days. The "done when"
checklist is the authoritative definition of done — do not start the next session until
every item is checked.

Prerequisites for the whole project: read `README.md` before starting Session 1.

## Error-handling policy

Canvas is a game-style engine: **fail fast, no silent fallbacks.**

- Functions that can fail return `Gem::Result`, never `bool`. Reserve `void` for operations
  that genuinely cannot fail (e.g. appending to an in-memory buffer); do not return a
  perpetually-`Success` `Gem::Result` to fake uniformity.
- Pick the most precise `Gem::Result` the enum offers; `Fail` is the last resort, not the
  default. The cpkg read path maps failures as:
  - `BadPointer` — a null out-pointer or path argument (caller misuse)
  - `Uninitialized` — operating on an empty / unopened `CCpkgSource`
  - `NotFound` — the file does not exist (`CCpkgSource::OpenFile`)
  - `InvalidArg` — not a recognized / valid package, or an out-of-range request: bad magic,
    cleared little-endian flag; at the `CCpkgSource` layer, a read range outside the source
  - `CorruptedData` — a recognized package that is damaged or incomplete: header / chunk CRC
    mismatch, truncation (a header / chunk table / chunk that runs past the data — premature end
    of data), or a structural read that falls outside the file
  - `NotImplemented` — a recognized but unsupported variant (e.g. `VersionMajor` newer than
    `CPKG_VERSION_MAJOR`)
  - `Fail` — only a genuine I/O failure (e.g. a short read from a range already validated against
    the file size), where the data is not necessarily malformed
  The split between `InvalidArg` ("this isn't a valid package / wrong file") and `CorruptedData`
  ("this *is* a package, but damaged") is deliberate; magic / flag are checked before CRC so a
  non-cpkg file reports `InvalidArg`, not `CorruptedData`. The low-level `CCpkgSource` reports an
  out-of-range request as `InvalidArg`; the container translates that into `CorruptedData` when
  the range came from the file's own header / table (the file is too small for its structure),
  and otherwise propagates the source's code.
- `Gem::Result::End` is **not** an error — it is a terminal sentinel in the success range
  (`>= Success`), for a future iterator-style "next chunk" read that has reached the last entry.
  Never use it for truncation; a package that ends before its declared structure is `CorruptedData`.
- On failure, stop at the first problem and log *why* with enough specifics to diagnose it
  (offending value vs. expectation, offset, index, sizes). CanvasPackage logs through the
  caller-supplied `Canvas::PackageLogFn`; it must not depend on `CanvasCore` / `XLogger`. The
  hook carries a format string + `va_list`, never a finished string, so composition is **deferred
  to the sink** and skipped when the sink filters the record out by level (QLog checks the level
  before it formats). Never compose a log string inside CanvasPackage.
- Do not paper over malformed input with a default or a partial result. The unknown-FourCC
  *skip* in `ReadPackage` is the one deliberate exception, and only because the format spec
  defines it as forward-compatibility behavior, not error recovery.

## Runtime loading model (streaming)

A `.cpkg` may be multiple gigabytes. The runtime must never hold the whole package in CPU
memory, so the read path is built around a **seekable source** and a **metadata / bulk split**,
the same shape UE (export map + IoStore `.utoc`/`.ucas`, streamed bulk data) and Unity
(serialized-file object table + `StreamingInfo`) use.

- **`CCpkgSource`** (Session 3) is the seekable byte source: `Read(offset, dst, size)` +
  `Size()`. Every byte range — header, chunk table, individual chunk, individual texture
  subresource — is read through it. It is one concrete class, file-backed via
  `CCpkgSource::OpenFile(...)`, which streams ranges from disk. A DirectStorage backing can be
  folded in later without changing callers. There is deliberately no memory backing: no planned
  scenario holds an entire `.cpkg` heap-resident (per-blob caching reads a range into a caller
  buffer instead), and one can be added trivially if that changes.
- **`CCpkgSink`** (Session 3) is the symmetric write side: a buffered, seekable file writer.
  Bulk payloads stream straight to disk through a tunable flush cache, so the writer is never
  whole-package-resident either; only the bounded header + chunk table is composed up front and
  the table entries are back-patched at finalize via `PatchBytes`. Its append helpers latch the
  first failure into a sticky status (checked once at `Close()`), so a long run of small writes
  costs a `memcpy` each, not a stream call or a result check each.
- **Metadata vs. bulk.** The small chunks (NODE, MATL, LITE, CAMR, ANIM) parse fully into CPU
  structs. MESH and TXTR parse only their **descriptors** (counts, formats, bounds, mip /
  subresource tables) and record the **byte ranges** `{offset, size}` of each vertex stream and
  texture subresource — *not* the bytes.
- **Bulk streams disk -> GPU.** `BuildModel` walks descriptors, allocates GPU resources, and
  streams each vertex stream / texture subresource range from the `CCpkgSource` into a transient
  upload buffer, releasing CPU staging immediately. Bulk bytes never accumulate in RAM.

This gives a **two-tier read API**:

- `PackageData::ReadPackage` is the *convenience full-load* — it materializes the entire scene
  into `PackageData`. Fine for small / medium scenes, and it is what the bake / write side needs
  anyway. Not for multi-gig packages.
- `CpkgDocument` (Session 9) is the *streaming handle* — it owns a `CCpkgSource`, holds the
  parsed metadata plus the bulk descriptors (byte ranges), and feeds `BuildModel` on demand.

`BuildModel` consumes a payload-provider seam that **both** `PackageData` (memcpy from its
vectors) and `CpkgDocument` (read from the source) satisfy, so one builder serves the
FBX-in-memory and streamed-`.cpkg` paths identically.

---

## Session 1: `CanvasPackageData.h` package intermediate + library skeleton  ✅ DONE

**Goal:** Create `Canvas::PackageData` -- the CPU-side representation of a packaged scene --
and stand up `CanvasPackage` as a compilable static library exposing it through a single
public header. No I/O implementation yet -- just type definitions and the read/write member
declarations.

`CanvasPackageData.h` lives in `src/CanvasPackage/Inc/` and is the library's only public
header. It has **no dependency on `CanvasCore.h`** -- only `Gem.hpp`, `CanvasMath.hpp`,
`CanvasTypes.h`, and standard headers. Texture formats and topology reuse
`Canvas::GfxFormat` and `Canvas::GfxSurfaceDimension` (both in `CanvasTypes.h`), so there is
no package-specific format, color-space, or dimension enum.

**Files created:**

- `src/CanvasPackage/Inc/CanvasPackageData.h`  -- package data types (pure data, no GPU types)
  - `PackageLogLevel` enum (`Info`, `Warning`, `Error`) and
    `PackageLogFn = std::function<void(PackageLogLevel, const char* format, va_list)>` -- the
    optional logging hook the caller supplies. It carries the format string + `va_list` (not a
    finished string) so the sink defers composition until after its level filter; a QLog sink
    forwards straight to `QLog::Logger::Log(level, format, args)`
  - `Canvas::PackageSubresource` struct -- Offset, Size, RowPitch (one mip / array / depth
    slice within a texture payload)
  - `Canvas::PackageTexture` struct -- `Name` (runtime lookup key, empty for unnamed),
    `Path` (.cpkg-relative), `Format` (`Canvas::GfxFormat`),
    `Dimension` (`Canvas::GfxSurfaceDimension`), `Width`, `Height`, `Depth`, `ArraySize`,
    `MipCount`, `Subresources` (per-slice byte ranges), `Bytes` (payload; empty for external)
  - `Canvas::PackageMaterial` struct -- PBR factors + 6 texture indices
    (Albedo, Normal, Emissive, Roughness, Metallic, AmbientOcclusion)
  - `Canvas::PackageSkinVertex` struct -- BoneIndices[4], BoneWeights[4]
  - `Canvas::PackageMeshPart` struct -- MaterialIndex, Positions, Normals, UV0, Tangents,
    SkinVertices (all `std::vector`)
  - `Canvas::PackageSkin` struct -- HasSkin, BoneNodeIndices, InvBindPoses
  - `Canvas::PackageMesh` struct -- Name, Parts, Bounds (`Math::AABB`), Skin
  - `Canvas::PackageLight` struct -- type, color, intensity, range, attenuation,
    inner/outer spot angle
  - `Canvas::PackageCamera` struct -- NearZ, FarZ, FovY (radians), AspectRatio
  - `Canvas::PackageNode` struct -- Name, ParentIndex (-1 = root), Translation, Scale,
    Rotation, MeshIndex (-1 = none), LightIndex, CameraIndex
  - `Canvas::PackageAnimKeyframe` struct -- Time, Translation, Rotation, Scale (full TRS sample)
  - `Canvas::PackageAnimTrack` struct -- NodeIndex, Keyframes
  - `Canvas::PackageAnimClip` struct -- Name, DurationSeconds, Tracks
  - `Canvas::PackageData` struct -- Meshes, Lights, Cameras, Materials, Textures, Nodes,
    AnimClips, Bounds, ActiveCameraNodeIndex, plus the native `ReadPackage` / `WritePackage`
    members (each returning `Gem::Result` and taking an optional `PackageLogFn`)

- `src/CanvasPackage/CMakeLists.txt`
  - Static library target `CanvasPackage`
  - Links `GEM` (for `Gem::Result`); **no link dependency on `CanvasCore`**
  - Include paths: `Inc/` (public), `../Inc/` (private, for `CanvasMath.hpp` /
    `CanvasTypes.h`), `.` (private)
  - `add_subdirectory(CanvasPackage)` added to the root `CMakeLists.txt`

- `src/CanvasPackage/pch.h`
  - Standard headers: `<vector>`, `<string>`, `<cstdint>`, `<functional>`
  - `CanvasPackageData.h`, `CanvasMath.hpp`
  - **No `CanvasCore.h`**

- `src/CanvasPackage/CanvasPackage.cpp`  -- translation unit for the (still unimplemented)
  `PackageData::ReadPackage` / `WritePackage` member bodies

**Done when:**
- [x] `cmake --build` succeeds with `CanvasPackage` in the solution
- [x] `CanvasPackageData.h` can be `#include`-d from a unit test without pulling in `CanvasCore.h`
- [x] All `Canvas::Package*` struct fields compile cleanly (no missing types)

---

## Session 2: `CCpkgReader` primitive

**Goal:** The low-level binary read cursor that all chunk readers sit on. Fully unit-tested
before any file format work builds on top of it.

> **Note (revised in Session 3):** this session originally also defined a `CpkgWriter` that
> buffered the whole package in a `std::vector`. That was replaced by the streaming, file-backed
> `CCpkgSink` (Session 3) so the writer is never whole-package-resident; the in-memory `CpkgWriter`
> no longer exists. `CCpkgReader` below is unchanged and still reads a flat byte block (a metadata
> block or a single chunk payload the caller pulled from a `CCpkgSource`).

> **Namespace convention (all internal cpkg headers):** every symbol below the public
> `Inc/` surface -- `CCpkgReader`, `CCpkgSink`, `CCpkgSource`, `MakeFourCC`, `CRC32`, the `CPKG_*`
> constants, the chunk serializers, etc. -- lives in `namespace Canvas::Cpkg`. Generic
> names like `CRC32` must never sit at global scope. The only public symbols are
> `Canvas::PackageData` and friends in `CanvasPackageData.h`.

**Files to create:**

- `src/CanvasPackage/CpkgStream.h` (internal, not in `Inc/`)
  - `CCpkgReader` class:
    - Constructed from `const uint8_t* data, size_t size`
    - `ReadBytes(void*, size_t) -> bool` — advances cursor, returns false on overrun
    - `ReadU8 / ReadU16 / ReadU32 / ReadU64 / ReadI32 / ReadFloat` — return value or throw on overrun
    - `ReadFloats(float*, size_t count) -> bool`
    - `ReadU32s(uint32_t*, size_t count) -> bool`
    - `ReadI32s(int32_t*, size_t count) -> bool`
    - `SkipBytes(size_t n) -> bool`
    - `AlignToOffset(size_t alignment)` — advance cursor to next multiple of alignment
    - `GetOffset() const -> size_t`
    - `SetOffset(size_t offset)` — random access (used by the chunk dispatcher)
    - `BytesRemaining() const -> size_t`
    - `IsAtEnd() const -> bool`

- `src/CanvasPackage/CpkgStream.cpp` — implementations

- `src/CanvasUnitTest/CpkgStreamTest.cpp` — new unit test file (add to CanvasUnitTest CMakeLists)

**Unit tests (`CpkgStreamTest.cpp`):** all read-cursor tests, building the input byte block
directly (the writer is exercised separately in `CpkgSinkTest.cpp`):
- Round-trip: build a block with U8, U16, U32, U64, I32, float; read them back in order; assert
  equality
- Float array: read back 12 floats element-by-element
- Overrun: attempt to read past end, verify false/exception returned
- SetOffset: SetOffset(4) over an 8-byte block, ReadU32, assert correct value
- AlignToOffset: cursor at 5, AlignToOffset(4), assert GetOffset() == 8

**Done when:**
- [X] All `CpkgStreamTest` tests pass
- [X] No new warnings in `CpkgStream.cpp`

---

## Session 3: Header and chunk-table I/O + the streaming source/sink layer

**Goal:** The file container layer — a seekable byte source, a buffered streaming byte sink,
header CRC32 validation, and chunk-table write/read. Everything the chunk serializers sit on top
of. The container must read its metadata *through the source* and write *through the sink* so a
large package is never fully resident on either side (see "Runtime loading model" above).

**Files to create:**

- `src/CanvasPackage/CpkgSource.h` / `.cpp` (internal) — one concrete class, no abstract base
  (this is a static library with a small, closed backing set; no `I`-prefixed pure interfaces).
  - `class CCpkgSource` — `Gem::Result Read(uint64_t offset, void* dst, size_t size)` +
    `uint64_t Size() const`. The container, chunk, and (later) bulk-streaming layers read every
    byte range through it. Read fails fast on an out-of-range request or short read.
  - `static Gem::Result OpenFile(const wchar_t*, CCpkgSource* out, const PackageLogFn& = {})` —
    streams ranges from disk via seek + read. A platform / DirectStorage backing can be folded
    into `CCpkgSource` later without touching upper layers.

- `src/CanvasPackage/CpkgSink.h` / `.cpp` (internal) — the streaming write counterpart to
  `CCpkgSource`; one concrete, file-backed class.
  - `class CCpkgSink` — buffered append helpers (`WriteBytes`, `WriteU8/16/32/64`, `WriteI32`,
    `WriteFloat`, `WriteFloats`, `WriteU32s`, `WriteI32s`, `PadToAlignment`) that `memcpy` into a
    tunable flush cache and block-flush to disk when it fills; `Tell()` for the absolute append
    offset; `PatchBytes(offset, data, size)` to back-patch already-written bytes via seek (used for
    the chunk table at finalize); `Flush()` / `Close()`.
  - `static Gem::Result CreateFile(const wchar_t*, CCpkgSink* out, size_t flushBufferSize =
    kDefaultFlushBufferSize, const PackageLogFn& = {})`.
  - **Sticky-status error model:** the append helpers return `void` and latch the first failure
    into a sticky `Gem::Result` (like `std::ostream`'s failbit); once latched, further appends are
    no-ops. A long run of small writes therefore costs a `memcpy` each, not a result check each;
    the caller checks the outcome once via `Status()`, `Flush()`, or `Close()`. `PatchBytes` (the
    cold finalize path) returns its `Gem::Result` directly. A background drain thread can be folded
    in behind this interface later without changing callers.

- `src/CanvasPackage/CpkgLog.h` (internal) — `inline void LogF(const PackageLogFn&,
  PackageLogLevel, const char* fmt, ...)`, the printf-style call site that opens a `va_list` and
  **forwards it unformatted** to `PackageLogFn`. It does not compose the string; the sink does,
  after its level filter, so a filtered-out record costs only the call. Shared by all cpkg
  `.cpp` files.

- `src/CanvasPackage/CpkgBlobTypes.h` (internal)
  - `constexpr uint32_t MakeFourCC(char, char, char, char)` -- a project-defined,
    platform-agnostic constexpr that packs four ASCII characters low-byte-first, so the
    packed value serializes (little-endian) to the four characters in file order. This is
    Canvas's own helper, NOT the Win32 `MAKEFOURCC` macro -- the package library must not
    pull in `<windows.h>`.
  - `CPKG_MAGIC = MakeFourCC('C','P','K','G')` -- never hard-code the raw integer. Writing
    the helper's value little-endian guarantees ASCII `CPKG` lands at offset 0; the literal
    `0x43504B47u` would serialize byte-reversed (`GKPC`).
  - `CPKG_VERSION_MAJOR = 1`, `CPKG_VERSION_MINOR = 0`
  - Per-field offset constants for the header and chunk-table entry (`CPKG_HEADER_OFFSET_*`,
    `CPKG_CHUNK_OFFSET_*`), each derived from the preceding field's width, with `CPKG_HEADER_SIZE`
    / `CPKG_CHUNK_ENTRY_SIZE` / `CPKG_HEADER_CRC_COVERAGE` derived from them. No raw byte numbers in
    the serializer; `static_assert`s tie the sizes to the mirror structs so a field change cannot
    silently drift. (The on-disk byte layout itself is specified in README.md.)
  - `CPKG_FLAG_LITTLE_ENDIAN = 0x1u`
  - Chunk FourCC constants via the same helper -- `CPKG_FOURCC_NODE = MakeFourCC('N','O','D','E')`
    etc. for all seven chunk types (NODE, MESH, MATL, TXTR, LITE, CAMR, ANIM), plus the
    reserved `CPKG_FOURCC_DEPS`. No raw hex literals for any FourCC.
  - `CpkgHeaderData` struct (plain fields, mirrors binary layout exactly)
  - `ChunkEntryData` struct (plain fields, mirrors binary layout exactly)

- `src/CanvasPackage/CpkgIO.h` (internal). Per the error-handling policy above, the write
  helpers below append to a `CCpkgSink` (which latches the first failure into its sticky status),
  so they return `void`; the read helpers fail fast and return `Gem::Result`, logging the precise
  reason via an optional `Canvas::PackageLogFn`.
  - `uint32_t CRC32(const uint8_t* data, size_t size)` — CRC32 with poly 0xEDB88320 (pure
    computation; no error to report)
  - `void WriteCpkgHeader(CCpkgSink&, uint32_t chunkCount)` — writes the complete fixed-size header.
    The header CRC covers only the fixed 20-byte prefix (all known here), so it is computed
    inline; the header needs no back-patch. (There is no `PatchCpkgHeaderCRC`.)
  - `void WriteChunkTable(CCpkgSink&, uint32_t chunkCount)` — writes `chunkCount` zeroed
    placeholder entries. The caller records `sink.Tell()` immediately *before* this call as the
    table-start offset, then passes it to `PatchChunkEntry` for each entry.
  - `void PatchChunkEntry(CCpkgSink&, uint64_t tableOffset, uint32_t entryIndex,
      uint32_t fourcc, uint16_t version, uint64_t dataOffset,
      uint32_t sizeRaw)` — back-patches one entry in place (via `CCpkgSink::PatchBytes`) once its
    payload has streamed and its offset/size are known
  - `Gem::Result ReadCpkgHeader(CCpkgReader&, CpkgHeaderData* out, const PackageLogFn& = {})` —
    validates magic, flags bit 0, and CRC32; on the first mismatch it logs the offending value
    vs. the expectation and returns a failing `Gem::Result` (`BadPointer` for a null `out`,
    otherwise `Fail`)
  - `Gem::Result ReadChunkTable(CCpkgReader&, uint32_t count, std::vector<ChunkEntryData>* out,
      const PackageLogFn& = {})` — logs and fails fast on a null `out` or a buffer overrun
  - **Source overloads** (the streaming entry points the runtime loader uses):
    `Gem::Result ReadCpkgHeader(CCpkgSource&, CpkgHeaderData* out, const PackageLogFn& = {})` and
    `Gem::Result ReadChunkTable(CCpkgSource&, uint32_t count, std::vector<ChunkEntryData>* out,
    const PackageLogFn& = {})` — each reads only the small header / chunk-table byte range from
    the source and delegates to the `CCpkgReader` overload. They never read the whole file.

- `src/CanvasPackage/CpkgIO.cpp` — implementations

- `src/CanvasUnitTest/CpkgSinkTest.cpp`, `src/CanvasUnitTest/CpkgIOTest.cpp`,
  `src/CanvasUnitTest/CpkgSourceTest.cpp` (the last two write through a `CCpkgSink` to a temp file
  and read back through a `CCpkgSource`; a shared `CpkgTestUtil.h` provides the temp-file helpers)

**Streaming write pattern (used by `WritePackage` in Session 7):** payloads stream straight to
disk; only the bounded header + chunk table is composed up front, and the table entries are
back-patched at finalize.
```
1. CCpkgSink::CreateFile(path, &sink)
2. WriteCpkgHeader(sink, chunkCount)          <- header (CRC computed inline), at offset 0
3. uint64_t tableOffset = sink.Tell()
   WriteChunkTable(sink, chunkCount)          <- placeholder entries
4. For each chunk i:
   a. sink.PadToAlignment(4)
   b. uint64_t dataOffset = sink.Tell()
   c. WriteXxxChunk(sink, scene)              <- streamed to disk
   d. uint32_t size = (uint32_t)(sink.Tell() - dataOffset)
   e. PatchChunkEntry(sink, tableOffset, i, fourcc, version, dataOffset, size)  <- seek-patch
5. result = sink.Close()                       <- surfaces any latched I/O failure
```

**Unit tests (`CpkgSinkTest.cpp`):**
- Scalar round-trip: stream U8/U16/U32/U64/I32/float to a temp file, read the file back, assert
  equality and that `Tell()` tracked the byte count
- `PadToAlignment`: write 1 byte, pad to 4, assert 4 bytes with trailing zeros on disk
- `PatchBytes` patches earlier bytes and does not move the append position
- `PatchBytes` past the written end returns `InvalidArg` and latches it (fails fast at `Close()`)
- A write at least as large as the flush cache bypasses it (streams straight to disk) and lands
  contiguously after smaller cached writes
- `CreateFile` into a missing directory fails; `Tell()` stays 0
- A write to an unopened sink latches `Uninitialized`

**Unit tests (`CpkgIOTest.cpp`):**
- CRC32: known input "123456789" should produce 0xCBF43926 (standard test vector)
- Magic bytes: stream a header to a temp file; assert the file begins with ASCII `C`,`P`,`K`,`G`
- Stream header + 2-entry table with PatchChunkEntry; read back through a `CCpkgSource`; assert all
  fields match
- ReadCpkgHeader on a block with wrong magic returns `Gem::Result::InvalidArg`
- ReadCpkgHeader on a block with bad CRC returns `Gem::Result::CorruptedData` *and* logs a
  message containing "CRC mismatch" through the `PackageLogFn`
- ReadCpkgHeader on a block with Flags bit 0 clear returns `Gem::Result::InvalidArg`
- ReadCpkgHeader with a null `out` returns `Gem::Result::BadPointer`
- ReadCpkgHeader on a block smaller than the header returns `Gem::Result::CorruptedData`
  (premature end of data)
- Streaming round-trip: stream a tiny file (header + table + 4-byte dummy chunk) through a
  `CCpkgSink`, read header + table through a `CCpkgSource`, verify the chunk offset and size and
  re-read the payload by its recorded range

**Unit tests (`CpkgSourceTest.cpp`):**
- File-backed `CCpkgSource`: in-range read succeeds; out-of-range / at-end reads return
  `InvalidArg`; zero-size read at end succeeds; an empty (default) source returns `Uninitialized`
- Header + chunk table round-trip read through a file-backed `CCpkgSource`
- A `CCpkgSource` smaller than the header makes `ReadCpkgHeader(source, ...)` return
  `CorruptedData` (the container translates the source's out-of-range `InvalidArg`)
- End-to-end streaming: stream a tiny container to a temp file, open it, read header + table,
  then read *only* one chunk's payload range by offset — demonstrating no whole-file load
- `CCpkgSource::OpenFile` on a missing file returns a failing `Gem::Result` and leaves the source
  empty (`Size() == 0`)

**Done when:**
- [ ] All `CpkgSinkTest`, `CpkgIOTest`, and `CpkgSourceTest` tests pass
- [ ] CRC32 produces the standard test vector
- [ ] The container header / chunk table can be read through a `CCpkgSource` and written through a
  `CCpkgSink` without holding the whole file in memory

---

## Session 4: `NODE` and `MESH` chunk serializers

**Goal:** The two required chunk types, fully round-trippable via unit tests. MESH includes
the 16-byte vertex stream alignment.

**Files to create:**

All chunk serializers return `Gem::Result` (not `bool`) and take an optional `PackageLogFn`,
per the error-handling policy. NODE is small and always parses fully. MESH carries the bulk
vertex streams, so its read has **two shapes** (see "Runtime loading model"):

- a full read that materializes every stream into `PackageData` (convenience / bake path), and
- a descriptor read that records each vertex stream's `{offset, size}` byte range into the
  streaming `CpkgDocument` *without* reading the bytes (the large-package path).

- `src/CanvasPackage/Chunks/NodeChunk.h` / `.cpp`
  - `Gem::Result WriteNodeChunk(CCpkgSink&, const PackageData&, const PackageLogFn& = {})`
  - `Gem::Result ReadNodeChunk(CCpkgReader&, PackageData*, const PackageLogFn& = {})`

- `src/CanvasPackage/Chunks/MeshChunk.h` / `.cpp`
  - `Gem::Result WriteMeshChunk(CCpkgSink&, const PackageData&, const PackageLogFn& = {})`
  - `Gem::Result ReadMeshChunk(CCpkgReader&, PackageData*, const PackageLogFn& = {})` — full read
  - `Gem::Result ReadMeshDescriptors(CCpkgReader&, uint64_t chunkFileOffset, MeshDescriptors*,
    const PackageLogFn& = {})` — descriptor-only read recording stream byte ranges for streaming

**Alignment in `WriteMeshChunk`:**  Before writing each vertex stream array, call
`sink.PadToAlignment(16)`.  Before reading each vertex stream in `ReadMeshChunk`, call
`reader.AlignToOffset(16)`.  The 16-byte alignment is relative to the file start, so the write
side uses `CCpkgSink::Tell()` and the read side `CCpkgReader::GetOffset()` (both absolute file
position) rather than a local offset.

> A chunk writer takes `CCpkgSink&` so it streams straight to disk; the `Gem::Result` it returns
> reflects validation of the `PackageData` it is given (the sink's own I/O failures latch on the
> sink and surface at `Close()`).

**Unit tests (`CpkgChunkTest.cpp`):**

NODE round-trip:
- Build a PackageData with 4 nodes: one root, two children of root, one grandchild
- Include a node with all three payload indices set (-1 for some, valid indices for others)
- WriteNodeChunk, ReadNodeChunk; assert all node fields equal

MESH round-trip (test A — full streams):
- One mesh, one part: 6 vertices, all streams present (UV0 + Tangents + Skin)
- Fill Positions, Normals, UV0, Tangents, SkinVertices with distinct recognisable values
- WriteMeshChunk, ReadMeshChunk; assert vertex data matches element-by-element

MESH round-trip (test B — minimal streams):
- One mesh, one part: 3 vertices, Positions + Normals only (StreamFlags = 0)
- Assert UV0, Tangents, SkinVertices are empty after read

MESH round-trip (test C — skinned mesh):
- Skin with 3 bones, BoneNodeIndices and InvBindPoses filled with recognisable values
- Assert HasSkin == true, bone indices and inv bind poses match after round-trip

MESH alignment test:
- After WriteMeshChunk, iterate the written buffer and verify each stream starts at a
  file offset that is a multiple of 16

**Done when:**
- [ ] All NODE and MESH chunk tests pass
- [ ] Alignment test confirms 16-byte stream boundaries

---

## Session 5: `MATL`, `TXTR`, `LITE`, `CAMR` chunk serializers

**Goal:** The four secondary chunk types, each round-trippable. TXTR includes the
Format (`Canvas::GfxFormat`) / Width / Height / MipCount metadata fields.

**Files to create:**

- `src/CanvasPackage/Chunks/MatlChunk.h` / `.cpp`
- `src/CanvasPackage/Chunks/TxtrChunk.h` / `.cpp`
- `src/CanvasPackage/Chunks/LiteChunk.h` / `.cpp`
- `src/CanvasPackage/Chunks/CamrChunk.h` / `.cpp`

**Unit tests (add to `CpkgChunkTest.cpp`):**

MATL round-trip:
- 2 materials: one with all 6 texture indices set to valid values; one fully unbound (-1)
- Assert all factor floats and texture indices match

TXTR round-trip:
- Entry A: named external texture — `Name = "sky_px"`, non-empty `.cpkg`-relative path,
  empty `Bytes`, no subresources, `Format = GfxFormat::Unknown`, `Dimension = Dimension2D`
- Entry B: embedded encoded-source image — `Name = ""`, empty path, 16 fake bytes,
  `Format = GfxFormat::Unknown`, one subresource covering the whole blob (`RowPitch = 0`)
- Entry C: embedded mip-mapped 2D — `Format = GfxFormat::BC7_UNorm_SRGB`, `Width = 512`,
  `Height = 512`, `MipCount = 10`, `ArraySize = 1`; 10 subresources with distinct
  Offset/Size/RowPitch and recognisable per-mip bytes
- Entry D: embedded cube map — `Dimension = DimensionCube`, `ArraySize = 6`, `MipCount = 3`;
  18 subresources (`mip + face * MipCount`); assert payload bytes for a couple of specific
  (face, mip) slices survive
- Assert Name, Path, Bytes, the full subresource table, and all metadata fields (Format,
  Dimension, Width, Height, Depth, ArraySize, MipCount) match after round-trip

LITE round-trip:
- One of each `LightType` (Ambient, Directional, Point, Spot)
- Spot light: verify SpotInnerAngle and SpotOuterAngle survive round-trip

CAMR round-trip:
- 2 cameras with distinct FOV and AspectRatio values

**Done when:**
- [ ] All MATL, TXTR, LITE, CAMR chunk tests pass

---

## Session 6: `ANIM` chunk serializer

**Goal:** Animation data round-trip including multi-clip, multi-track, variable keyframes.

**Files to create:**

- `src/CanvasPackage/Chunks/AnimChunk.h` / `.cpp`
  - `Gem::Result WriteAnimChunk(CCpkgSink&, const PackageData&, const PackageLogFn& = {})`
  - `Gem::Result ReadAnimChunk(CCpkgReader&, PackageData*, const PackageLogFn& = {})`

**Unit tests (add to `CpkgChunkTest.cpp`):**

ANIM round-trip (basic):
- 1 clip, 2 tracks, 5 keyframes each
- Fill Time, Translation, Rotation, Scale with recognisable values
- Assert all fields match after round-trip

ANIM round-trip (multi-clip):
- 3 clips, variable track counts (0, 1, 4 tracks respectively)
- Assert clip names, durations, and per-track keyframe data all match

ANIM empty (zero clips):
- WriteAnimChunk with no clips; ReadAnimChunk; assert empty AnimClips vector

**Done when:**
- [ ] All ANIM chunk tests pass

---

## Session 7: Bake infrastructure — `BakeManifest`, `SerializeScene`, `WritePackage`

**Goal:** The outward-facing bake API: parse and validate a manifest, resolve all paths,
merge FBX-sourced and manifest-declared textures into a deduplicated TXTR list, produce
a `.cpkg` file on disk. No new executable yet — just the library functions.

**Files to create:**

- `src/CanvasPackage/BakeManifest.h` / `.cpp` (internal)

  Structures:
  ```cpp
  struct ManifestTexture {
      std::string name;            // runtime lookup key; empty = unnamed
      std::string path;            // manifest-relative; resolved to absolute on load
      bool embed = false;          // embed decoded bytes; falls back to defaultEmbed
  };

  struct ManifestSource {
      std::string type;            // "fbx" for v1
      std::string path;            // manifest-relative
      std::vector<std::string> textureSearchPaths;  // manifest-relative directories
  };

  struct BakeManifest {
      std::string name;
      std::string outputPath;      // manifest-relative
      std::vector<ManifestSource> sources;
      std::vector<ManifestTexture> textures;
      bool defaultEmbed = false;
  };
  ```

  Functions:
  - `Gem::Result LoadBakeManifest(const wchar_t* pManifestPath, BakeManifest* out)`
    - Parses the JSON; sets all path fields as stored (relative or absolute strings)
    - Does not resolve paths yet

- `src/CanvasPackage/BakePaths.h` / `.cpp` (internal)
  - `struct ResolvedPaths` — holds `manifestDir` (absolute), `outputAbsPath`,
    `outputDir` (absolute directory of the .cpkg)
  - `Gem::Result ResolveBakePaths(const BakeManifest&, const wchar_t* manifestPath, ResolvedPaths* out)`
    — resolves relative paths against the manifest directory; absolute paths are used as-is
  - `std::wstring ResolveManifestRelative(const ResolvedPaths&, const std::string& relPath)`
    — resolves one manifest-relative path to absolute
  - `std::string MakePkgRelative(const ResolvedPaths&, const std::wstring& absPath)`
    — re-expresses an absolute path as a path relative to the output `.cpkg` directory;
    used for storing texture paths in TXTR

- `src/CanvasPackage/TextureTable.h` / `.cpp` (internal)

  Manages the deduplicated texture list built during bake:
  ```cpp
  class TextureTable {
  public:
      // Add a manifest-declared texture. Returns its TXTR index.
      // Sets Name from manifest entry; records resolved absolute path for dedup.
      int32_t AddManifestTexture(const ManifestTexture&, const ResolvedPaths&);

      // Add an FBX-sourced texture by resolved absolute path.
      // Deduplicates: if the path already exists, returns the existing index.
      // Name is empty; sets Path as .cpkg-relative.
      int32_t AddFbxTexture(const std::wstring& absPath, const ResolvedPaths&);

      // Resolve an FBX texture path that may be broken/absolute:
      // 1. Try absPathFromFbx as-is
      // 2. Try textureSearchDirs (each absolute) by appending the filename component
      // Returns empty string if unresolved.
      static std::wstring ResolveFbxTexturePath(
          const std::wstring& absPathFromFbx,
          const std::vector<std::wstring>& textureSearchDirs);

      // After all sources are processed, call this to get the final PackageTexture list.
      std::vector<PackageTexture> Finalize() const;
  };
  ```

- `src/CanvasPackage/SerializeScene.h` / `.cpp` (internal)
  - `void SerializeScene(const Canvas::Fbx::ImportedScene&, const TextureTable&, PackageData* out)`
  - Maps every `ImportedScene` field to its `PackageData` counterpart
  - For each `ImportedTextureRef`: looks up its resolved index from `TextureTable` and
    remaps the texture index on each `PackageMaterial` accordingly
  - Leaves Format = `GfxFormat::Unknown` and Width/Height/MipCount = 0 (v1 does not
    pre-decode during bake)
  - Copies skin vertices, animation clips, keyframes directly

- `src/CanvasPackage/WritePackage.cpp` (internal) -- implements the public
  `PackageData::WritePackage(const wchar_t* pOutputPath, const PackageLogFn&) const` member
  - Streaming write (Session 3 pattern): `CCpkgSink::CreateFile` -> header -> placeholder table
    -> stream each chunk -> back-patch the table entries -> `Close()`
  - Skips chunks with no data (no MESH if Meshes is empty, etc.)
  - Writes chunks in order: NODE, MESH, MATL, TXTR, LITE, CAMR, ANIM
  - Returns the `CCpkgSink::Close()` result so any latched I/O failure surfaces

- Update `CanvasPackage`'s CMakeLists to link `CanvasFbx` and a JSON parser
  (use a header-only library such as `nlohmann/json` or a minimal hand-rolled parser)

**Unit tests (add to a new `CpkgBakeTest.cpp`):**

`LoadBakeManifest`:
- Parse a valid JSON manifest string with relative paths; assert all fields populated correctly
- Parse a manifest with absolute paths in `textures[].path` and `sources[].path`; assert
  they are accepted and stored as-is
- Manifest with a missing required field (`output`) returns an error

`ResolveBakePaths`:
- Given a manifest at `C:/proj/content/scene.cpkg.json` with `output = "../build/out.cpkg"`,
  assert `outputAbsPath == "C:/proj/build/out.cpkg"` and `outputDir == "C:/proj/build"`

`MakePkgRelative`:
- Given `outputDir = "C:/proj/build"` and `absPath = "C:/proj/content/textures/foo.png"`,
  assert result is `"../content/textures/foo.png"`

`TextureTable` deduplication:
- Add the same absolute path twice (once via manifest, once via FBX source)
- Assert `Finalize()` contains only one entry
- Assert the first-added name is preserved (manifest entry wins)

`TextureTable` FBX search path fallback:
- `absPathFromFbx = "C:/old-machine/textures/rock.png"` (does not exist)
- `textureSearchDirs = ["C:/proj/content/textures"]` (contains `rock.png`)
- Assert `ResolveFbxTexturePath` returns the correct absolute path

`SerializeScene` field mapping:
- Build a synthetic `Fbx::ImportedScene` with two materials each referencing one texture
- Build a `TextureTable` with both textures pre-added; call `SerializeScene`
- Assert material texture indices in the output PackageData match the TextureTable indices

`WritePackage` minimal / full (same as before, now using updated PackageTexture with Name):

**Done when:**
- [ ] All `CpkgBakeTest` tests pass
- [ ] `WritePackage` produces a file whose CRC32 validates correctly
- [ ] Absolute-path rejection returns an error, not a crash

---

## Session 8: `CanvasBake` executable

**Goal:** A command-line tool that bakes a manifest (or a quick FBX shorthand) into a
`.cpkg` file, with full path resolution, texture deduplication, and optional embedding.

**Files to create:**

- `src/CanvasBake/CMakeLists.txt`
  - Executable target `CanvasBake`
  - Links `CanvasPackage`, `CanvasFbx`, `CanvasPlatformWin32` (for WIC texture decode)
  - Add `add_subdirectory(CanvasBake)` to root CMakeLists.txt

- `src/CanvasBake/CanvasBake.cpp`

  **Two CLI modes:**

  Manifest mode (primary):
  ```
  CanvasBake --manifest <path/to/scene.cpkg.json> [--log-level <level>]
  ```
  - Load and validate the manifest via `LoadBakeManifest`
  - Resolve all paths via `ResolveBakePaths` (relative paths resolved against manifest
    directory; absolute paths used as-is)
  - Build `TextureTable`: add manifest `textures[]` entries first
  - For each FBX source:
    - Resolve `textureSearchPaths` to absolute directories
    - Call `Fbx::ImportScene`; print diagnostics; abort on errors
    - For each `ImportedTextureRef` in the scene: call
      `TextureTable::ResolveFbxTexturePath` with the search dirs, then
      `TextureTable::AddFbxTexture`; warn and skip if unresolved
    - Call `SerializeScene(imported, textureTable, &pkgScene)`
  - Finalize `TextureTable` into `pkgScene.Textures`
  - For each texture with `embed = true` (or `defaultEmbed`): decode via WIC, fill
    `Bytes` + `Subresources` (one entry per mip / array slice), `Format`, `Dimension`,
    `Width`, `Height`, `Depth`, `ArraySize`, `MipCount`
  - Call `pkgScene.WritePackage(resolvedOutputPath)`
  - Print chunk count, texture count, and output file size on success

  Quick-bake shorthand (no manifest file required):
  ```
  CanvasBake --fbx <path> --out <path> [--embed-textures] [--log-level <level>]
  ```
  - Synthesizes a `BakeManifest` in memory (no JSON file needed)
  - All paths are taken as given; the FBX file's directory is used as the manifest dir
  - Runs the same bake pipeline as manifest mode
  - `--embed-textures` sets `defaultEmbed = true`

**Manual integration test:**
- Create a minimal manifest referencing a test FBX and two standalone textures
  (one `embed: true`, one `embed: false`) with paths relative to the manifest
- Run `CanvasBake --manifest test.cpkg.json`
- Verify: no errors; output file exists; magic `CPKG` at offset 0; CRC valid
- Confirm the embedded texture is larger in the file than the path-only entry

**Done when:**
- [ ] `CanvasBake` builds and links in both manifest and quick-bake modes
- [ ] Manifest mode with relative paths produces a valid `.cpkg`
- [ ] Manifest mode with absolute paths also produces a valid `.cpkg`
- [ ] Quick-bake `--fbx` mode still works (no regression from prior design)
- [ ] Embedded texture produces a larger file than path-only

---

## Session 9: streaming loader — `CpkgDocument` + `PackageData::ReadPackage`

**Goal:** The runtime read path. Two entry points over a shared `CCpkgSource` (Session 3):
`CpkgDocument` (streaming handle for large packages) and `PackageData::ReadPackage` (convenience
full-load), both with full validation and forward-compatible unknown-chunk skipping.

**Files to create:**

- `src/CanvasPackage/CpkgDocument.h` / `.cpp` (internal + a thin public handle)
  - Owns a `CCpkgSource` by value (keeps the file open), the parsed header + chunk table, the
    fully parsed *small* chunks (NODE, MATL, LITE, CAMR, ANIM), and the MESH / TXTR
    **descriptors** with bulk byte ranges -- never the bulk bytes.
  - `static Gem::Result Open(CCpkgSource&&, CpkgDocument*, const PackageLogFn& = {})`
  - `Gem::Result StreamMeshStream(meshIdx, partIdx, StreamKind, void* dst, size_t size)`
  - `Gem::Result StreamTextureSubresource(texIdx, subresIdx, void* dst, size_t size)`
  - These resolve the descriptor's `{offset, size}` and `CCpkgSource::Read` it straight into the
    caller's (GPU upload) buffer.

- `src/CanvasPackage/ReadPackage.cpp` -- implements the declared `PackageData::ReadPackage()`
  member as the *convenience full-load*: open a file-backed `CCpkgSource`, build a `CpkgDocument`,
  then
  pull every descriptor's bytes into `PackageData`'s vectors. This is the small / medium-scene
  path; large packages use `CpkgDocument` + `BuildModel` directly and never materialize here.

**Shared loader flow (`CpkgDocument::Open`):**
1. `ReadCpkgHeader(source, &header)` -- fail fast on bad magic / wrong endianness / bad CRC
2. Check `VersionMajor` -- fail if > `CPKG_VERSION_MAJOR`
3. `ReadChunkTable(source, header.ChunkCount, &entries)`
4. For each entry, by FourCC:
   - Small chunk: read the chunk's byte range from the source into a temp buffer and parse it
     fully with the `CCpkgReader`-based `ReadXxxChunk`.
   - MESH / TXTR: read only the descriptor region; record bulk `{offset, size}` ranges. Do not
     read bulk bytes.
   - Unknown FourCC: skip (Info-level message via `PackageLogFn`). The one sanctioned
     non-failure case -- the format spec defines unknown-chunk skipping as forward compatibility.
   - Per-chunk read failure on a *known* FourCC: fail fast. Log an Error with the FourCC and
     entry offset, return the failing `Gem::Result` -- no partial load.
5. Compute `Bounds` as union of all mesh bounds (from descriptors).

**Unit tests (add to `CpkgBakeTest.cpp`):**

Full round-trip:
- Build a rich `Canvas::PackageData` (meshes + materials + textures + nodes + lights +
  cameras + one animation clip)
- `WritePackage` to a temp file path
- `ReadPackage` from that path
- Assert all mesh vertex data, node hierarchy, material factors, texture metadata,
  light parameters, camera parameters, animation keyframes match the original

Unknown-chunk skip:
- Manually write a file with a `UNKN` FourCC chunk between NODE and MESH
- `ReadPackage` should succeed, emit one Info-level message via `PackageLogFn`, and have
  correct NODE and MESH data

Version rejection:
- Write a valid file, patch `VersionMajor` to 99, call `ReadPackage`, assert it returns
  `Gem::Result::NotImplemented` (recognized header, unsupported version). Note: patching
  `VersionMajor` invalidates the header CRC, so the test must re-patch the CRC (or the version
  check must run after a successful CRC check) for `NotImplemented` to surface rather than
  `CorruptedData`.

**Done when:**
- [ ] All round-trip and edge-case tests pass
- [ ] `ReadPackage` on the `CanvasBake` output from Session 8 succeeds without errors

---

## Session 10: `Canvas::BuildModel` in `CanvasCore` + `CanvasFbx` refactor

**Goal:** Move scene construction to `CanvasCore` as a single `Canvas::BuildModel` shared
by both the FBX and .cpkg load paths. Refactor `CanvasFbx` so `ImportScene` returns a
`Canvas::PackageData` and `Fbx::BuildModel` is removed. After this session there is one
build path regardless of source format.

**Files to create:**

- `src/CanvasCore/BuildModel.cpp` -- new file in CanvasCore
  - Implements `Canvas::BuildModel(XCanvas*, XGfxDevice*, const PackageData&,
    const BuildModelOptions&, XModel**)`
  - Logic identical to the current `Fbx::BuildModel` body, operating on `PackageData` types

  **Payload seam (so one builder serves both load paths):** express the bulk-fetching part of
  `BuildModel` against a small callback seam (a `std::function`, in the same spirit as the
  existing `PackageTextureLoadFn` -- not a pure virtual interface): "fill this buffer with the
  bytes for mesh stream X / texture subresource Y". `PackageData` satisfies it by `memcpy` from
  its vectors; `CpkgDocument` satisfies it by `CCpkgSource::Read` from disk. Provide a `BuildModel`
  overload taking the seam (used by the streaming `.cpkg` path) alongside the `const PackageData&`
  overload (used by the FBX-in-memory path). The streaming overload allocates each GPU vertex
  stream / texture subresource and streams its bytes straight into the upload buffer, so multi-gig
  packages never accumulate bulk data in CPU RAM.

**Files to modify:**

- `src/Inc/CanvasCore.h`
  - Add `#include "CanvasPackageData.h"` near the top. NOTE: `CanvasPackageData.h` now lives
    in `src/CanvasPackage/Inc/`, so this makes `CanvasCore` depend on a `CanvasPackage`
    header. Resolve the dependency direction before this session -- e.g. move `PackageData`
    to a location both can share, or have `CanvasCore` take the package types via a lighter
    seam -- rather than having the core engine reach into the package library's includes.
  - Add after the existing interface declarations:
    ```cpp
    using PackageTextureLoadFn = std::function<bool(
        XGfxDevice*, const Canvas::PackageTexture&, XGfxSurface**)>;

    struct BuildModelOptions {
        PackageTextureLoadFn  TextureLoader;
        const char*         pModelName = nullptr;
        QLog::Logger*       pLogger    = nullptr;
    };

    CANVAS_API Gem::Result BuildModel(
        XCanvas*,
        XGfxDevice*,
        const Canvas::PackageData&,
        const BuildModelOptions&,
        XModel**);
    ```

- `src/CanvasFbx/Inc/CanvasFbx.h`
  - Change `ImportScene` return type from `Fbx::ImportedScene*` to `Canvas::PackageData*`
    (or out-parameter `Canvas::PackageData* pScene`)
  - Remove `Fbx::BuildModel` declaration
  - Remove `Fbx::TextureLoadFn`, `Fbx::BuildModelOptions`, `Fbx::ImportedScene` and all
    `Imported*` types (they are replaced by `Canvas::PackageData` and `Canvas::Package*`)

- `src/CanvasFbx/CanvasFbx.cpp`
  - Update `ImportScene` to populate `Canvas::PackageData` instead of `ImportedScene`
  - Delete `BuildModel` implementation (moved to `CanvasCore/BuildModel.cpp`)

- `src/CanvasModelViewer/CanvasModelViewer.cpp`
  - Replace `Fbx::BuildModel(...)` call with `Canvas::BuildModel(...)`
  - `TryLoadImportedScene` now calls `Canvas::BuildModel` -- the texture-loader lambda
    type changes from `Fbx::TextureLoadFn` to `Canvas::PackageTextureLoadFn`

**Done when:**
- [ ] `CanvasCore` builds with `BuildModel.cpp` added to the target
- [ ] `CanvasFbx` builds with `ImportScene` producing `Canvas::PackageData`
- [ ] `CanvasModelViewer --fbx` still loads and renders correctly (no regression)
- [ ] Code review: confirm `Canvas::BuildModel` has feature parity with the old
  `Fbx::BuildModel` (textures, materials, meshes, lights, cameras, skins, animations)

---

## Session 11: `CanvasModelViewer --pkg` integration

**Goal:** End-to-end validation -- load a `.cpkg` in the viewer and confirm it renders
identically to the same scene loaded from FBX.

**Files to modify:**

- `src/CanvasModelViewer/CanvasModelViewer.cpp`

**Changes:**
1. Add `#include "CanvasPackageData.h"` alongside `CanvasFbx.h`
2. Add `std::filesystem::path m_PkgPath` member alongside `m_ModelPath`
3. Add `--pkg` option to the `InCommand` parser alongside `--fbx`
4. The WIC texture loader lambda in `TryLoadImportedScene` already uses
   `Canvas::PackageTextureLoadFn` (updated in Session 10) -- extract it into a named helper
   `MakeTextureLoader()` so it can be reused for both code paths
5. Add `TryReadPackageData(XCanvas*, XScene*, XCamera*, XSceneGraphNode*)`:
   - Calls `scene.ReadPackage(m_PkgPath.c_str())`
   - Calls `Canvas::BuildModel(pCanvas, pDevice, scene, opts, &pModel)` -- same call as
     the FBX path (both take `Canvas::PackageData`)
   - Camera framing + animation controller wiring is identical to `TryLoadImportedScene`
6. In `Initialize`, choose the FBX or PKG path depending on which option was given
   (error if both or neither are given)
7. Update `CanvasModelViewer`'s CMakeLists to link `CanvasPackage`

**Manual test procedure:**
- `CanvasBake --fbx SomeScene.fbx --out SomeScene.cpkg`
- `CanvasModelViewer --fbx SomeScene.fbx`  -- note mesh count, camera position, lights
- `CanvasModelViewer --pkg SomeScene.cpkg` -- visually confirm identical render
- Cycle animations (Enter key) -- confirm same clips are present and play correctly
- Resize window -- confirm camera aspect ratio updates correctly

**Done when:**
- [ ] `CanvasModelViewer --fbx` still works (no regression)
- [ ] `CanvasModelViewer --pkg` renders the same scene as the FBX path
- [ ] Animation cycling works via .cpkg
- [ ] HUD shows correct mesh/light/camera counts (or at least does not crash)
- [ ] No D3D12 validation errors in the debug layer output
