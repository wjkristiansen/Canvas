//================================================================================================
// CpkgChunks - per-chunk-type serializers (NODE, MESH). Each takes an optional PackageLogFn and
// fails fast on the first malformed value. See README.md for the on-disk layouts.
//
// NODE is small and always parses fully. MESH carries the bulk vertex streams, so its read has
// two shapes: ReadMeshChunk materializes every stream into PackageData (convenience / bake path);
// ReadMeshDescriptors records each stream's absolute {offset, size} byte range into the streaming
// CpkgDocument without reading the bytes (the large-package path).
//================================================================================================
#pragma once

#include "CanvasPackageData.h" // Canvas::PackageData / PackageLogFn, Gem::Result
#include "CpkgSink.h"
#include "CpkgStream.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Canvas::Cpkg
{

//--------------------------------------------------------------------------------------------------
// NODE - the flat scene-graph node array plus the active camera node index.
//--------------------------------------------------------------------------------------------------

// NODE chunk format version, recorded in the chunk-table entry.
constexpr uint16_t CPKG_NODE_CHUNK_VERSION = 1;

// Append the NODE chunk for data.Nodes / data.ActiveCameraNodeIndex to the sink. Throws
// CpkgError(InvalidArg) on an out-of-range ParentIndex / ActiveCameraNodeIndex or a payload index
// below -1 (node-internal validation only; payload indices are not checked against the other
// chunks here).
void WriteNodeChunk(CCpkgSink& sink, const PackageData& data, const PackageLogFn& logFn = {});

// Parse a NODE chunk from the reader's current position into out->Nodes and
// out->ActiveCameraNodeIndex (replacing them), advancing the cursor past the chunk. Fails fast
// with CorruptedData on truncation or an index that violates the same validation the writer
// enforces.
Gem::Result ReadNodeChunk(CCpkgReader& reader, PackageData* out,
                          const PackageLogFn& logFn = {});

//--------------------------------------------------------------------------------------------------
// MESH - all meshes, their material parts, and the per-part vertex streams.
//
// Each vertex stream begins on a 16-byte boundary relative to the START OF THE FILE (not the
// chunk), so the offset space matters: WriteMeshChunk pads via CCpkgSink::Tell() (absolute),
// ReadMeshChunk requires a reader whose offset 0 is file offset 0 (e.g. a reader over the whole
// file image positioned at the chunk), and ReadMeshDescriptors takes a chunk-relative reader plus
// the chunk's file offset and rebases internally.
//--------------------------------------------------------------------------------------------------

// MESH chunk format version, recorded in the chunk-table entry.
constexpr uint16_t CPKG_MESH_CHUNK_VERSION = 1;

// Per-part StreamFlags bits: which optional vertex streams are present.
constexpr uint8_t CPKG_MESH_STREAM_UV0        = 0x1;
constexpr uint8_t CPKG_MESH_STREAM_TANGENTS   = 0x2;
constexpr uint8_t CPKG_MESH_STREAM_SKIN       = 0x4;
constexpr uint8_t CPKG_MESH_STREAM_VALID_MASK = CPKG_MESH_STREAM_UV0
                                              | CPKG_MESH_STREAM_TANGENTS
                                              | CPKG_MESH_STREAM_SKIN;

//--------------------------------------------------------------------------------------------------
// MeshStreamRange - the absolute file byte range of one vertex stream, ready for
// CCpkgSource::Read. Size 0 means the stream is absent from the part.
//--------------------------------------------------------------------------------------------------
struct MeshStreamRange
{
    uint64_t Offset = 0; // absolute byte offset from the start of the file
    uint64_t Size   = 0; // byte count; 0 = stream absent
};

//--------------------------------------------------------------------------------------------------
// MeshPartDescriptor - one material partition's metadata plus the byte ranges of its vertex
// streams (never the bytes themselves).
//--------------------------------------------------------------------------------------------------
struct MeshPartDescriptor
{
    int32_t         MaterialIndex = -1;
    uint32_t        VertexCount   = 0;
    uint8_t         StreamFlags   = 0; // CPKG_MESH_STREAM_* bits
    MeshStreamRange Positions;
    MeshStreamRange Normals;
    MeshStreamRange UV0;
    MeshStreamRange Tangents;
    MeshStreamRange SkinVertices;
};

//--------------------------------------------------------------------------------------------------
// MeshDescriptor - one mesh's small metadata, parsed fully (the skin block is bounded bone data,
// not bulk), plus the per-part stream ranges.
//--------------------------------------------------------------------------------------------------
struct MeshDescriptor
{
    std::string                     Name;
    Math::AABB                      Bounds;
    PackageSkin                     Skin;
    std::vector<MeshPartDescriptor> Parts;
};

struct MeshDescriptors
{
    std::vector<MeshDescriptor> Meshes;
};

// Append the MESH chunk for data.Meshes to the sink, 16-byte-aligning every vertex stream
// relative to the file start. Throws CpkgError(InvalidArg) when a part's stream sizes disagree
// with its Positions count, or a skin's bone index and inverse-bind-pose arrays disagree (or are
// non-empty with HasSkin false).
void WriteMeshChunk(CCpkgSink& sink, const PackageData& data, const PackageLogFn& logFn = {});

// Full read: parse a MESH chunk into out->Meshes (replacing it), materializing every vertex
// stream, and advance the cursor past the chunk. The reader's offset space must be file-absolute
// (offset 0 == file offset 0) so the 16-byte stream alignment resolves; position the cursor at
// the chunk's data offset before calling. Fails fast with CorruptedData on truncation, unknown
// StreamFlags bits, or a malformed skin block.
Gem::Result ReadMeshChunk(CCpkgReader& reader, PackageData* out,
                          const PackageLogFn& logFn = {});

// Descriptor read for the streaming path: same parse and validation as ReadMeshChunk, but records
// each vertex stream's absolute {offset, size} range instead of copying the bytes. The reader is
// chunk-relative (offset 0 == the chunk's first byte, spanning the whole chunk range);
// chunkFileOffset is the chunk's absolute data offset from the chunk table, used to rebase
// alignment and the recorded ranges.
Gem::Result ReadMeshDescriptors(CCpkgReader& reader, uint64_t chunkFileOffset,
                                MeshDescriptors* out, const PackageLogFn& logFn = {});

} // namespace Canvas::Cpkg
