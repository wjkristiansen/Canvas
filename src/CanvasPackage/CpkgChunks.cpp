#include "pch.h"
#include "CpkgChunks.h"
#include "CpkgLog.h"

#include <vector>

namespace Canvas::Cpkg
{

namespace
{
    //--------------------------------------------------------------------------------------------
    // Shared name helpers - every chunk stores names as a uint32 length prefix (terminator
    // included, so a stored name is never zero-length) followed by the UTF-8 characters.
    //--------------------------------------------------------------------------------------------

    bool NameLenFits(const std::string& name)
    {
        return name.size() < UINT32_MAX;
    }

    // Append a length-prefixed name: NameLen (terminator included), then the characters and the
    // terminator. The caller validates NameLenFits first.
    void WriteName(CCpkgSink& sink, const std::string& name)
    {
        const uint32_t len = static_cast<uint32_t>(name.size() + 1);
        sink.WriteU32(len);
        sink.WriteBytes(name.c_str(), len);
    }

    // Read a length-prefixed name written by WriteName. what + index identify the record in log
    // records (e.g. "ReadNodeChunk node" 3); they are passed straight to LogF so the sink composes
    // the message only if it accepts the level. Fails with CorruptedData on truncation, a zero
    // length, or a missing terminator.
    Gem::Result ReadName(CCpkgReader& reader, std::string* out, const char* what, uint32_t index,
                         const PackageLogFn& logFn)
    {
        if (reader.BytesRemaining() < sizeof(uint32_t))
        {
            LogF(logFn, PackageLogLevel::Error,
                 "%s %u: truncated reading name length; have %zu bytes", what, index,
                 reader.BytesRemaining());
            return Gem::Result::CorruptedData;
        }

        const uint32_t len = reader.ReadU32();
        if (len == 0)
        {
            LogF(logFn, PackageLogLevel::Error,
                 "%s %u: name length 0 (the terminator is included, so the minimum is 1)",
                 what, index);
            return Gem::Result::CorruptedData;
        }
        if (len > reader.BytesRemaining())
        {
            LogF(logFn, PackageLogLevel::Error,
                 "%s %u: truncated name; length %u exceeds the %zu bytes remaining",
                 what, index, len, reader.BytesRemaining());
            return Gem::Result::CorruptedData;
        }

        std::string name(len, '\0');
        reader.ReadBytes(name.data(), len); // in range per the check above
        if (name.back() != '\0')
        {
            LogF(logFn, PackageLogLevel::Error,
                 "%s %u: name of length %u is not null-terminated", what, index, len);
            return Gem::Result::CorruptedData;
        }

        name.pop_back(); // drop the stored terminator
        *out = std::move(name);
        return Gem::Result::Success;
    }

    // Writers convert a failing shared validator (which already logged the details) into a throw;
    // readers return the validator's result directly.
    void ThrowIfFailed(Gem::Result result, const char* what)
    {
        if (Gem::Failed(result))
            throw CpkgError(result, what);
    }

    //--------------------------------------------------------------------------------------------
    // NODE
    //--------------------------------------------------------------------------------------------

    // Shared by write and read so both sides reject the same malformed node. nodeCount is the full
    // array size; index identifies the node in log records.
    Gem::Result ValidateNode(const PackageNode& node, size_t index, size_t nodeCount,
                             Gem::Result failCode, const char* context, const PackageLogFn& logFn)
    {
        const int64_t count = static_cast<int64_t>(nodeCount);
        if (node.ParentIndex < -1 || node.ParentIndex >= count)
        {
            LogF(logFn, PackageLogLevel::Error,
                 "%s: node %zu ParentIndex %d out of range [-1, %lld)",
                 context, index, node.ParentIndex, static_cast<long long>(count));
            return failCode;
        }
        if (node.ParentIndex == static_cast<int64_t>(index))
        {
            LogF(logFn, PackageLogLevel::Error,
                 "%s: node %zu is its own parent", context, index);
            return failCode;
        }
        if (node.MeshIndex < -1 || node.LightIndex < -1 || node.CameraIndex < -1)
        {
            LogF(logFn, PackageLogLevel::Error,
                 "%s: node %zu has a payload index below -1 (Mesh %d, Light %d, Camera %d)",
                 context, index, node.MeshIndex, node.LightIndex, node.CameraIndex);
            return failCode;
        }
        return Gem::Result::Success;
    }

    //--------------------------------------------------------------------------------------------
    // MESH
    //--------------------------------------------------------------------------------------------

    // Vertex streams and inverse bind poses are read and written as raw bytes; the element-size
    // contracts they rely on are asserted on the POD types in CanvasPackageData.h. Every stream
    // starts on a 16-byte boundary relative to the file start.
    constexpr size_t kStreamAlignment = 16;

    // Per-bone skin payload: one node index plus one inverse bind pose. Used to bounds-check a
    // declared bone count before the bone arrays are sized.
    constexpr size_t kBoneDiskBytes = sizeof(int32_t) + sizeof(PackageMatrix4x4);

    // Validate one mesh's skin the same way on write and read: the two bone arrays agree, and a
    // skinless mesh carries no bones.
    Gem::Result ValidateSkin(bool hasSkin, size_t boneCount, size_t invBindPoseCount,
                             size_t meshIndex, Gem::Result failCode, const char* context,
                             const PackageLogFn& logFn)
    {
        if (boneCount != invBindPoseCount)
        {
            LogF(logFn, PackageLogLevel::Error,
                 "%s: mesh %zu skin has %zu bone node indices but %zu inverse bind poses",
                 context, meshIndex, boneCount, invBindPoseCount);
            return failCode;
        }
        if (!hasSkin && boneCount != 0)
        {
            LogF(logFn, PackageLogLevel::Error,
                 "%s: mesh %zu has HasSkin false but %zu bones", context, meshIndex, boneCount);
            return failCode;
        }
        return Gem::Result::Success;
    }

    // Identifies a mesh part in deferred log records. Passed by value so the pieces reach LogF as
    // arguments; the sink composes the location string only if it accepts the record.
    struct MeshPartLoc
    {
        const char* Fn;
        uint32_t    Mesh;
        uint32_t    Part;
    };

    // Skip the zero padding that places the next vertex stream on a 16-byte file boundary.
    // base rebases the reader's local offset space to absolute file offsets (0 when the reader is
    // already file-absolute).
    Gem::Result SkipStreamAlignment(CCpkgReader& reader, uint64_t base, MeshPartLoc loc,
                                    const char* streamName, const PackageLogFn& logFn)
    {
        const uint64_t absolute = base + reader.GetOffset();
        const size_t pad = static_cast<size_t>((kStreamAlignment - absolute % kStreamAlignment)
                                               % kStreamAlignment);
        if (!reader.SkipBytes(pad))
        {
            LogF(logFn, PackageLogLevel::Error,
                 "%s: mesh %u part %u %s: truncated in stream alignment padding; need %zu, have %zu",
                 loc.Fn, loc.Mesh, loc.Part, streamName, pad, reader.BytesRemaining());
            return Gem::Result::CorruptedData;
        }
        return Gem::Result::Success;
    }

    // Parse one vertex stream: align to the 16-byte file boundary, then either materialize the
    // elements into *dst or record the absolute byte range into *outRange and skip the bytes
    // (exactly one of dst / outRange is non-null). The truncation check precedes the resize so a
    // corrupt count cannot trigger a huge allocation.
    template <typename TElement>
    Gem::Result ReadStream(CCpkgReader& reader, uint64_t base, uint32_t vertexCount,
                           std::vector<TElement>* dst, MeshStreamRange* outRange,
                           MeshPartLoc loc, const char* streamName, const PackageLogFn& logFn)
    {
        const Gem::Result aligned = SkipStreamAlignment(reader, base, loc, streamName, logFn);
        if (Gem::Failed(aligned))
            return aligned;

        const uint64_t size = static_cast<uint64_t>(vertexCount) * sizeof(TElement);
        if (size > reader.BytesRemaining())
        {
            LogF(logFn, PackageLogLevel::Error,
                 "%s: mesh %u part %u %s: truncated stream; need %llu bytes for %u vertices, have %zu",
                 loc.Fn, loc.Mesh, loc.Part, streamName, static_cast<unsigned long long>(size),
                 vertexCount, reader.BytesRemaining());
            return Gem::Result::CorruptedData;
        }

        if (dst)
        {
            dst->resize(vertexCount);
            if (vertexCount != 0)
                reader.ReadBytes(dst->data(), static_cast<size_t>(size)); // in range per the check
        }
        else
        {
            outRange->Offset = base + reader.GetOffset();
            outRange->Size   = size;
            reader.SkipBytes(static_cast<size_t>(size));
        }
        return Gem::Result::Success;
    }

    // Shared MESH parse: one walk of the chunk serving both read shapes. Exactly one of outData /
    // outDesc is non-null: outData materializes every stream, outDesc records stream byte ranges
    // and skips the bulk bytes. base rebases reader offsets to absolute file offsets; fn names the
    // entry point in log records.
    Gem::Result ParseMeshChunk(CCpkgReader& reader, uint64_t base, PackageData* outData,
                               MeshDescriptors* outDesc, const char* fn, const PackageLogFn& logFn)
    {
        uint32_t meshCount = 0;
        if (!reader.ReadU32s(&meshCount, 1))
        {
            LogF(logFn, PackageLogLevel::Error,
                 "%s: truncated chunk header; have %zu bytes", fn, reader.BytesRemaining());
            return Gem::Result::CorruptedData;
        }

        std::vector<PackageMesh>    meshes;
        std::vector<MeshDescriptor> descriptors;

        for (uint32_t m = 0; m < meshCount; ++m)
        {
            std::string name;
            const Gem::Result nameResult = ReadName(reader, &name, fn, m, logFn);
            if (Gem::Failed(nameResult))
                return nameResult;

            uint32_t    partCount = 0;
            PackageAABB bounds;
            if (!reader.ReadU32s(&partCount, 1)
                || !reader.ReadFloats(bounds.Min.V, 3)
                || !reader.ReadFloats(bounds.Max.V, 3))
            {
                LogF(logFn, PackageLogLevel::Error,
                     "%s: mesh %u: truncated mesh header; have %zu bytes", fn, m,
                     reader.BytesRemaining());
                return Gem::Result::CorruptedData;
            }

            PackageMesh    mesh;
            MeshDescriptor desc;

            for (uint32_t p = 0; p < partCount; ++p)
            {
                const MeshPartLoc loc{ fn, m, p };

                int32_t  materialIndex = 0;
                uint32_t vertexCount   = 0;
                uint8_t  streamFlags   = 0;
                uint8_t  pad[3];
                if (!reader.ReadI32s(&materialIndex, 1)
                    || !reader.ReadU32s(&vertexCount, 1)
                    || !reader.ReadBytes(&streamFlags, 1)
                    || !reader.ReadBytes(pad, sizeof(pad)))
                {
                    LogF(logFn, PackageLogLevel::Error,
                         "%s: mesh %u part %u: truncated part header; have %zu bytes",
                         fn, m, p, reader.BytesRemaining());
                    return Gem::Result::CorruptedData;
                }

                if ((streamFlags & ~CPKG_MESH_STREAM_VALID_MASK) != 0)
                {
                    LogF(logFn, PackageLogLevel::Error,
                         "%s: mesh %u part %u: unknown StreamFlags bits 0x%02X (valid mask 0x%02X)",
                         fn, m, p, streamFlags, CPKG_MESH_STREAM_VALID_MASK);
                    return Gem::Result::CorruptedData;
                }
                if (materialIndex < -1)
                {
                    LogF(logFn, PackageLogLevel::Error,
                         "%s: mesh %u part %u: MaterialIndex %d below -1", fn, m, p, materialIndex);
                    return Gem::Result::CorruptedData;
                }

                PackageMeshPart    part;
                MeshPartDescriptor partDesc;
                part.MaterialIndex     = materialIndex;
                partDesc.MaterialIndex = materialIndex;
                partDesc.VertexCount   = vertexCount;
                partDesc.StreamFlags   = streamFlags;

                // Exactly one destination per stream, matching the outData / outDesc mode.
                std::vector<PackageFloat4>*      positions = outData ? &part.Positions : nullptr;
                std::vector<PackageFloat4>*      normals   = outData ? &part.Normals   : nullptr;
                std::vector<PackageFloat2>*      uv0       = outData ? &part.UV0       : nullptr;
                std::vector<PackageFloat4>*      tangents  = outData ? &part.Tangents  : nullptr;
                std::vector<PackageSkinVertex>*  skinVerts = outData ? &part.SkinVertices : nullptr;

                Gem::Result streamResult = ReadStream(reader, base, vertexCount, positions,
                                                      &partDesc.Positions, loc, "Positions", logFn);
                if (Gem::Failed(streamResult))
                    return streamResult;

                streamResult = ReadStream(reader, base, vertexCount, normals, &partDesc.Normals,
                                          loc, "Normals", logFn);
                if (Gem::Failed(streamResult))
                    return streamResult;

                if (streamFlags & CPKG_MESH_STREAM_UV0)
                {
                    streamResult = ReadStream(reader, base, vertexCount, uv0, &partDesc.UV0,
                                              loc, "UV0", logFn);
                    if (Gem::Failed(streamResult))
                        return streamResult;
                }
                if (streamFlags & CPKG_MESH_STREAM_TANGENTS)
                {
                    streamResult = ReadStream(reader, base, vertexCount, tangents,
                                              &partDesc.Tangents, loc, "Tangents", logFn);
                    if (Gem::Failed(streamResult))
                        return streamResult;
                }
                if (streamFlags & CPKG_MESH_STREAM_SKIN)
                {
                    streamResult = ReadStream(reader, base, vertexCount, skinVerts,
                                              &partDesc.SkinVertices, loc, "SkinVertices", logFn);
                    if (Gem::Failed(streamResult))
                        return streamResult;
                }

                if (outData)
                    mesh.Parts.push_back(std::move(part));
                else
                    desc.Parts.push_back(partDesc);
            }

            // Per-mesh skin block: bounded bone data, parsed fully on both paths.
            uint8_t  hasSkin   = 0;
            uint8_t  skinPad[3];
            uint32_t boneCount = 0;
            if (!reader.ReadBytes(&hasSkin, 1)
                || !reader.ReadBytes(skinPad, sizeof(skinPad))
                || !reader.ReadU32s(&boneCount, 1))
            {
                LogF(logFn, PackageLogLevel::Error,
                     "%s: mesh %u: truncated skin block; have %zu bytes", fn, m,
                     reader.BytesRemaining());
                return Gem::Result::CorruptedData;
            }

            if (hasSkin > 1)
            {
                LogF(logFn, PackageLogLevel::Error,
                     "%s: mesh %u: HasSkin byte is %u (expected 0 or 1)", fn, m, hasSkin);
                return Gem::Result::CorruptedData;
            }
            const Gem::Result skinValid = ValidateSkin(hasSkin != 0, boneCount, boneCount, m,
                                                       Gem::Result::CorruptedData, fn, logFn);
            if (Gem::Failed(skinValid))
                return skinValid;

            if (static_cast<uint64_t>(boneCount) * kBoneDiskBytes > reader.BytesRemaining())
            {
                LogF(logFn, PackageLogLevel::Error,
                     "%s: mesh %u: truncated skin; %u bones need %llu bytes, have %zu", fn, m,
                     boneCount, static_cast<unsigned long long>(boneCount) * kBoneDiskBytes,
                     reader.BytesRemaining());
                return Gem::Result::CorruptedData;
            }

            PackageSkin skin;
            skin.HasSkin = hasSkin != 0;
            skin.BoneNodeIndices.resize(boneCount);
            skin.InvBindPoses.resize(boneCount);
            if (boneCount != 0)
            {
                reader.ReadI32s(skin.BoneNodeIndices.data(), boneCount);       // in range per the guard
                reader.ReadBytes(skin.InvBindPoses.data(),
                                 boneCount * sizeof(PackageMatrix4x4));
            }

            if (outData)
            {
                mesh.Name   = std::move(name);
                mesh.Bounds = bounds;
                mesh.Skin   = std::move(skin);
                meshes.push_back(std::move(mesh));
            }
            else
            {
                desc.Name   = std::move(name);
                desc.Bounds = bounds;
                desc.Skin   = std::move(skin);
                descriptors.push_back(std::move(desc));
            }
        }

        if (outData)
            outData->Meshes = std::move(meshes);
        else
            outDesc->Meshes = std::move(descriptors);
        return Gem::Result::Success;
    }
}

//--------------------------------------------------------------------------------------------------
// NODE
//--------------------------------------------------------------------------------------------------

void WriteNodeChunk(CCpkgSink& sink, const PackageData& data, const PackageLogFn& logFn)
{
    const size_t nodeCount = data.Nodes.size();
    if (nodeCount > UINT32_MAX)
        ThrowF(logFn, Gem::Result::InvalidArg,
               "WriteNodeChunk: node count %zu exceeds the uint32 limit", nodeCount);
    if (data.ActiveCameraNodeIndex < -1
        || data.ActiveCameraNodeIndex >= static_cast<int64_t>(nodeCount))
        ThrowF(logFn, Gem::Result::InvalidArg,
               "WriteNodeChunk: ActiveCameraNodeIndex %d out of range [-1, %zu)",
               data.ActiveCameraNodeIndex, nodeCount);

    sink.WriteU32(static_cast<uint32_t>(nodeCount));
    sink.WriteI32(data.ActiveCameraNodeIndex);

    for (size_t i = 0; i < nodeCount; ++i)
    {
        const PackageNode& node = data.Nodes[i];

        ThrowIfFailed(ValidateNode(node, i, nodeCount, Gem::Result::InvalidArg,
                                   "WriteNodeChunk", logFn),
                      "WriteNodeChunk: invalid node");
        if (!NameLenFits(node.Name))
            ThrowF(logFn, Gem::Result::InvalidArg,
                   "WriteNodeChunk: node %zu name of %zu bytes exceeds the uint32 length prefix",
                   i, node.Name.size());

        WriteName(sink, node.Name);
        sink.WriteI32(node.ParentIndex);
        sink.WriteFloats(node.Translation.V, 4);
        sink.WriteFloats(node.Rotation.V, 4);
        sink.WriteFloats(node.Scale.V, 4);
        sink.WriteI32(node.MeshIndex);
        sink.WriteI32(node.LightIndex);
        sink.WriteI32(node.CameraIndex);
    }
}

Gem::Result ReadNodeChunk(CCpkgReader& reader, PackageData* out, const PackageLogFn& logFn)
{
    if (!out)
    {
        LogF(logFn, PackageLogLevel::Error, "ReadNodeChunk: null output pointer");
        return Gem::Result::BadPointer;
    }

    uint32_t nodeCount             = 0;
    int32_t  activeCameraNodeIndex = 0;
    if (!reader.ReadU32s(&nodeCount, 1) || !reader.ReadI32s(&activeCameraNodeIndex, 1))
    {
        LogF(logFn, PackageLogLevel::Error,
             "ReadNodeChunk: truncated chunk header; have %zu bytes", reader.BytesRemaining());
        return Gem::Result::CorruptedData;
    }

    if (activeCameraNodeIndex < -1 || activeCameraNodeIndex >= static_cast<int64_t>(nodeCount))
    {
        LogF(logFn, PackageLogLevel::Error,
             "ReadNodeChunk: ActiveCameraNodeIndex %d out of range [-1, %u)",
             activeCameraNodeIndex, nodeCount);
        return Gem::Result::CorruptedData;
    }

    std::vector<PackageNode> nodes;

    for (uint32_t i = 0; i < nodeCount; ++i)
    {
        PackageNode node;

        const Gem::Result nameResult = ReadName(reader, &node.Name, "ReadNodeChunk: node", i, logFn);
        if (Gem::Failed(nameResult))
            return nameResult;

        if (!reader.ReadI32s(&node.ParentIndex, 1)
            || !reader.ReadFloats(node.Translation.V, 4)
            || !reader.ReadFloats(node.Rotation.V, 4)
            || !reader.ReadFloats(node.Scale.V, 4)
            || !reader.ReadI32s(&node.MeshIndex, 1)
            || !reader.ReadI32s(&node.LightIndex, 1)
            || !reader.ReadI32s(&node.CameraIndex, 1))
        {
            LogF(logFn, PackageLogLevel::Error,
                 "ReadNodeChunk: node %u: truncated body; have %zu bytes", i,
                 reader.BytesRemaining());
            return Gem::Result::CorruptedData;
        }

        const Gem::Result valid = ValidateNode(node, i, nodeCount, Gem::Result::CorruptedData,
                                               "ReadNodeChunk", logFn);
        if (Gem::Failed(valid))
            return valid;

        nodes.push_back(std::move(node));
    }

    out->Nodes = std::move(nodes);
    out->ActiveCameraNodeIndex = activeCameraNodeIndex;
    return Gem::Result::Success;
}

//--------------------------------------------------------------------------------------------------
// MESH
//--------------------------------------------------------------------------------------------------

void WriteMeshChunk(CCpkgSink& sink, const PackageData& data, const PackageLogFn& logFn)
{
    const size_t meshCount = data.Meshes.size();
    if (meshCount > UINT32_MAX)
        ThrowF(logFn, Gem::Result::InvalidArg,
               "WriteMeshChunk: mesh count %zu exceeds the uint32 limit", meshCount);

    sink.WriteU32(static_cast<uint32_t>(meshCount));

    for (size_t m = 0; m < meshCount; ++m)
    {
        const PackageMesh& mesh = data.Meshes[m];

        if (!NameLenFits(mesh.Name))
            ThrowF(logFn, Gem::Result::InvalidArg,
                   "WriteMeshChunk: mesh %zu name of %zu bytes exceeds the uint32 length prefix",
                   m, mesh.Name.size());
        if (mesh.Parts.size() > UINT32_MAX)
            ThrowF(logFn, Gem::Result::InvalidArg,
                   "WriteMeshChunk: mesh %zu part count %zu exceeds the uint32 limit",
                   m, mesh.Parts.size());

        const PackageSkin& skin = mesh.Skin;
        ThrowIfFailed(ValidateSkin(skin.HasSkin, skin.BoneNodeIndices.size(),
                                   skin.InvBindPoses.size(), m, Gem::Result::InvalidArg,
                                   "WriteMeshChunk", logFn),
                      "WriteMeshChunk: invalid skin");
        if (skin.BoneNodeIndices.size() > UINT32_MAX)
            ThrowF(logFn, Gem::Result::InvalidArg,
                   "WriteMeshChunk: mesh %zu bone count %zu exceeds the uint32 limit",
                   m, skin.BoneNodeIndices.size());

        WriteName(sink, mesh.Name);
        sink.WriteU32(static_cast<uint32_t>(mesh.Parts.size()));
        sink.WriteFloats(mesh.Bounds.Min.V, 3);
        sink.WriteFloats(mesh.Bounds.Max.V, 3);

        for (size_t p = 0; p < mesh.Parts.size(); ++p)
        {
            const PackageMeshPart& part = mesh.Parts[p];
            const size_t vertexCount = part.Positions.size();

            if (vertexCount > UINT32_MAX)
                ThrowF(logFn, Gem::Result::InvalidArg,
                       "WriteMeshChunk: mesh %zu part %zu vertex count %zu exceeds the uint32 limit",
                       m, p, vertexCount);
            if (part.Normals.size() != vertexCount)
                ThrowF(logFn, Gem::Result::InvalidArg,
                       "WriteMeshChunk: mesh %zu part %zu has %zu normals for %zu positions",
                       m, p, part.Normals.size(), vertexCount);
            if (part.MaterialIndex < -1)
                ThrowF(logFn, Gem::Result::InvalidArg,
                       "WriteMeshChunk: mesh %zu part %zu MaterialIndex %d below -1",
                       m, p, part.MaterialIndex);

            // The optional streams are either absent or exactly one element per vertex; their
            // presence sets the matching StreamFlags bit.
            uint8_t streamFlags = 0;
            struct OptionalStreamSize
            {
                const char* Name;
                size_t      Count;
                uint8_t     FlagBit;
            };
            const OptionalStreamSize optionalStreams[] = {
                { "UV0",          part.UV0.size(),          CPKG_MESH_STREAM_UV0 },
                { "Tangents",     part.Tangents.size(),     CPKG_MESH_STREAM_TANGENTS },
                { "SkinVertices", part.SkinVertices.size(), CPKG_MESH_STREAM_SKIN },
            };
            for (const OptionalStreamSize& stream : optionalStreams)
            {
                if (stream.Count == 0)
                    continue;
                if (stream.Count != vertexCount)
                    ThrowF(logFn, Gem::Result::InvalidArg,
                           "WriteMeshChunk: mesh %zu part %zu has %zu %s elements for %zu positions",
                           m, p, stream.Count, stream.Name, vertexCount);
                streamFlags |= stream.FlagBit;
            }

            sink.WriteI32(part.MaterialIndex);
            sink.WriteU32(static_cast<uint32_t>(vertexCount));
            sink.WriteU8(streamFlags);
            const uint8_t pad[3] = {};
            sink.WriteBytes(pad, sizeof(pad));

            // Each stream starts on a 16-byte boundary relative to the file start; PadToAlignment
            // pads from the sink's absolute Tell().
            sink.PadToAlignment(kStreamAlignment);
            sink.WriteBytes(part.Positions.data(), vertexCount * sizeof(PackageFloat4));
            sink.PadToAlignment(kStreamAlignment);
            sink.WriteBytes(part.Normals.data(), vertexCount * sizeof(PackageFloat4));
            if (streamFlags & CPKG_MESH_STREAM_UV0)
            {
                sink.PadToAlignment(kStreamAlignment);
                sink.WriteBytes(part.UV0.data(), vertexCount * sizeof(PackageFloat2));
            }
            if (streamFlags & CPKG_MESH_STREAM_TANGENTS)
            {
                sink.PadToAlignment(kStreamAlignment);
                sink.WriteBytes(part.Tangents.data(), vertexCount * sizeof(PackageFloat4));
            }
            if (streamFlags & CPKG_MESH_STREAM_SKIN)
            {
                sink.PadToAlignment(kStreamAlignment);
                sink.WriteBytes(part.SkinVertices.data(), vertexCount * sizeof(PackageSkinVertex));
            }
        }

        const uint32_t boneCount = static_cast<uint32_t>(skin.BoneNodeIndices.size());
        sink.WriteU8(skin.HasSkin ? 1 : 0);
        const uint8_t pad[3] = {};
        sink.WriteBytes(pad, sizeof(pad));
        sink.WriteU32(boneCount);
        if (boneCount != 0)
        {
            sink.WriteI32s(skin.BoneNodeIndices.data(), boneCount);
            sink.WriteBytes(skin.InvBindPoses.data(), boneCount * sizeof(PackageMatrix4x4));
        }
    }
}

Gem::Result ReadMeshChunk(CCpkgReader& reader, PackageData* out, const PackageLogFn& logFn)
{
    if (!out)
    {
        LogF(logFn, PackageLogLevel::Error, "ReadMeshChunk: null output pointer");
        return Gem::Result::BadPointer;
    }
    // The reader is file-absolute (offset 0 == file offset 0), so no rebase.
    return ParseMeshChunk(reader, 0, out, nullptr, "ReadMeshChunk", logFn);
}

Gem::Result ReadMeshDescriptors(CCpkgReader& reader, uint64_t chunkFileOffset,
                                MeshDescriptors* out, const PackageLogFn& logFn)
{
    if (!out)
    {
        LogF(logFn, PackageLogLevel::Error, "ReadMeshDescriptors: null output pointer");
        return Gem::Result::BadPointer;
    }
    return ParseMeshChunk(reader, chunkFileOffset, nullptr, out, "ReadMeshDescriptors", logFn);
}

//--------------------------------------------------------------------------------------------------
// MATL
//--------------------------------------------------------------------------------------------------

void WriteMatlChunk(CCpkgSink& sink, const PackageData& data, const PackageLogFn& logFn)
{
    const size_t materialCount = data.Materials.size();
    if (materialCount > UINT32_MAX)
        ThrowF(logFn, Gem::Result::InvalidArg,
               "WriteMatlChunk: material count %zu exceeds the uint32 limit", materialCount);

    sink.WriteU32(static_cast<uint32_t>(materialCount));

    for (size_t i = 0; i < materialCount; ++i)
    {
        const PackageMaterial& mat = data.Materials[i];

        if (!NameLenFits(mat.Name))
            ThrowF(logFn, Gem::Result::InvalidArg,
                   "WriteMatlChunk: material %zu name of %zu bytes exceeds the uint32 length prefix",
                   i, mat.Name.size());

        WriteName(sink, mat.Name);
        sink.WriteFloats(mat.BaseColorFactor.V, 4);
        sink.WriteFloats(mat.EmissiveFactor.V, 4);
        sink.WriteFloats(mat.RoughMetalAOFactor.V, 4);
        sink.WriteI32(mat.AlbedoTextureIndex);
        sink.WriteI32(mat.NormalTextureIndex);
        sink.WriteI32(mat.EmissiveTextureIndex);
        sink.WriteI32(mat.RoughnessTextureIndex);
        sink.WriteI32(mat.MetallicTextureIndex);
        sink.WriteI32(mat.AmbientOcclusionTextureIndex);
    }
}

Gem::Result ReadMatlChunk(CCpkgReader& reader, PackageData* out, const PackageLogFn& logFn)
{
    if (!out)
    {
        LogF(logFn, PackageLogLevel::Error, "ReadMatlChunk: null output pointer");
        return Gem::Result::BadPointer;
    }

    uint32_t materialCount = 0;
    if (!reader.ReadU32s(&materialCount, 1))
    {
        LogF(logFn, PackageLogLevel::Error,
             "ReadMatlChunk: truncated chunk header; have %zu bytes", reader.BytesRemaining());
        return Gem::Result::CorruptedData;
    }

    std::vector<PackageMaterial> materials;

    for (uint32_t i = 0; i < materialCount; ++i)
    {
        PackageMaterial mat;

        const Gem::Result nameResult = ReadName(reader, &mat.Name, "ReadMatlChunk: material", i,
                                                logFn);
        if (Gem::Failed(nameResult))
            return nameResult;

        if (!reader.ReadFloats(mat.BaseColorFactor.V, 4)
            || !reader.ReadFloats(mat.EmissiveFactor.V, 4)
            || !reader.ReadFloats(mat.RoughMetalAOFactor.V, 4)
            || !reader.ReadI32s(&mat.AlbedoTextureIndex, 1)
            || !reader.ReadI32s(&mat.NormalTextureIndex, 1)
            || !reader.ReadI32s(&mat.EmissiveTextureIndex, 1)
            || !reader.ReadI32s(&mat.RoughnessTextureIndex, 1)
            || !reader.ReadI32s(&mat.MetallicTextureIndex, 1)
            || !reader.ReadI32s(&mat.AmbientOcclusionTextureIndex, 1))
        {
            LogF(logFn, PackageLogLevel::Error,
                 "ReadMatlChunk: material %u: truncated body; have %zu bytes", i,
                 reader.BytesRemaining());
            return Gem::Result::CorruptedData;
        }

        materials.push_back(std::move(mat));
    }

    out->Materials = std::move(materials);
    return Gem::Result::Success;
}

//--------------------------------------------------------------------------------------------------
// TXTR
//--------------------------------------------------------------------------------------------------

void WriteTxtrChunk(CCpkgSink& sink, const PackageData& data, const PackageLogFn& logFn)
{
    const size_t textureCount = data.Textures.size();
    if (textureCount > UINT32_MAX)
        ThrowF(logFn, Gem::Result::InvalidArg,
               "WriteTxtrChunk: texture count %zu exceeds the uint32 limit", textureCount);

    sink.WriteU32(static_cast<uint32_t>(textureCount));

    for (size_t i = 0; i < textureCount; ++i)
    {
        const PackageTexture& tex = data.Textures[i];

        if (!NameLenFits(tex.Name))
            ThrowF(logFn, Gem::Result::InvalidArg,
                   "WriteTxtrChunk: texture %zu name of %zu bytes exceeds the uint32 length prefix",
                   i, tex.Name.size());
        if (!NameLenFits(tex.Path))
            ThrowF(logFn, Gem::Result::InvalidArg,
                   "WriteTxtrChunk: texture %zu path of %zu bytes exceeds the uint32 length prefix",
                   i, tex.Path.size());
        if (tex.Subresources.size() > UINT32_MAX)
            ThrowF(logFn, Gem::Result::InvalidArg,
                   "WriteTxtrChunk: texture %zu subresource count %zu exceeds the uint32 limit",
                   i, tex.Subresources.size());

        // Every subresource must address bytes that exist in the payload.
        const uint64_t payloadSize = tex.Bytes.size();
        for (size_t s = 0; s < tex.Subresources.size(); ++s)
        {
            const PackageSubresource& sub = tex.Subresources[s];
            if (sub.Offset + sub.Size > payloadSize)
                ThrowF(logFn, Gem::Result::InvalidArg,
                       "WriteTxtrChunk: texture %zu subresource %zu range [%llu, %llu) exceeds the "
                       "%llu-byte payload",
                       i, s, static_cast<unsigned long long>(sub.Offset),
                       static_cast<unsigned long long>(sub.Offset + sub.Size),
                       static_cast<unsigned long long>(payloadSize));
        }

        WriteName(sink, tex.Name);
        WriteName(sink, tex.Path);
        sink.WriteU32(static_cast<uint32_t>(tex.Format));
        sink.WriteU32(static_cast<uint32_t>(tex.Dimension));
        sink.WriteU32(tex.Width);
        sink.WriteU32(tex.Height);
        sink.WriteU32(tex.Depth);
        sink.WriteU32(tex.ArraySize);
        sink.WriteU32(tex.MipCount);
        sink.WriteU32(static_cast<uint32_t>(tex.Subresources.size()));
        for (const PackageSubresource& sub : tex.Subresources)
        {
            sink.WriteU64(sub.Offset);
            sink.WriteU32(sub.Size);
            sink.WriteU32(sub.RowPitch);
        }
        sink.WriteU64(payloadSize);
        if (!tex.Bytes.empty())
            sink.WriteBytes(tex.Bytes.data(), tex.Bytes.size());
    }
}

Gem::Result ReadTxtrChunk(CCpkgReader& reader, PackageData* out, const PackageLogFn& logFn)
{
    if (!out)
    {
        LogF(logFn, PackageLogLevel::Error, "ReadTxtrChunk: null output pointer");
        return Gem::Result::BadPointer;
    }

    uint32_t textureCount = 0;
    if (!reader.ReadU32s(&textureCount, 1))
    {
        LogF(logFn, PackageLogLevel::Error,
             "ReadTxtrChunk: truncated chunk header; have %zu bytes", reader.BytesRemaining());
        return Gem::Result::CorruptedData;
    }

    std::vector<PackageTexture> textures;

    for (uint32_t i = 0; i < textureCount; ++i)
    {
        PackageTexture tex;

        Gem::Result nameResult = ReadName(reader, &tex.Name, "ReadTxtrChunk: texture name", i,
                                          logFn);
        if (Gem::Failed(nameResult))
            return nameResult;

        nameResult = ReadName(reader, &tex.Path, "ReadTxtrChunk: texture path", i, logFn);
        if (Gem::Failed(nameResult))
            return nameResult;

        uint32_t format      = 0;
        uint32_t dimension   = 0;
        uint32_t subresCount = 0;
        if (!reader.ReadU32s(&format, 1)
            || !reader.ReadU32s(&dimension, 1)
            || !reader.ReadU32s(&tex.Width, 1)
            || !reader.ReadU32s(&tex.Height, 1)
            || !reader.ReadU32s(&tex.Depth, 1)
            || !reader.ReadU32s(&tex.ArraySize, 1)
            || !reader.ReadU32s(&tex.MipCount, 1)
            || !reader.ReadU32s(&subresCount, 1))
        {
            LogF(logFn, PackageLogLevel::Error,
                 "ReadTxtrChunk: texture %u: truncated metadata; have %zu bytes", i,
                 reader.BytesRemaining());
            return Gem::Result::CorruptedData;
        }

        if (dimension > static_cast<uint32_t>(GfxSurfaceDimension::DimensionCube))
        {
            LogF(logFn, PackageLogLevel::Error,
                 "ReadTxtrChunk: texture %u: unknown Dimension %u (valid 0..%u)", i, dimension,
                 static_cast<uint32_t>(GfxSurfaceDimension::DimensionCube));
            return Gem::Result::CorruptedData;
        }
        tex.Format    = static_cast<GfxFormat>(format);
        tex.Dimension = static_cast<GfxSurfaceDimension>(dimension);

        // Read the subresource table entry by entry (push_back so an absurd count fails on the
        // first truncated read instead of pre-allocating).
        for (uint32_t s = 0; s < subresCount; ++s)
        {
            PackageSubresource sub;
            if (!reader.ReadBytes(&sub.Offset, sizeof(sub.Offset))
                || !reader.ReadU32s(&sub.Size, 1)
                || !reader.ReadU32s(&sub.RowPitch, 1))
            {
                LogF(logFn, PackageLogLevel::Error,
                     "ReadTxtrChunk: texture %u: truncated subresource table at entry %u of %u; "
                     "have %zu bytes", i, s, subresCount, reader.BytesRemaining());
                return Gem::Result::CorruptedData;
            }
            tex.Subresources.push_back(sub);
        }

        uint64_t payloadSize = 0;
        if (!reader.ReadBytes(&payloadSize, sizeof(payloadSize)))
        {
            LogF(logFn, PackageLogLevel::Error,
                 "ReadTxtrChunk: texture %u: truncated reading payload size; have %zu bytes", i,
                 reader.BytesRemaining());
            return Gem::Result::CorruptedData;
        }
        if (payloadSize > reader.BytesRemaining())
        {
            LogF(logFn, PackageLogLevel::Error,
                 "ReadTxtrChunk: texture %u: payload of %llu bytes exceeds the %zu bytes remaining",
                 i, static_cast<unsigned long long>(payloadSize), reader.BytesRemaining());
            return Gem::Result::CorruptedData;
        }

        // Each subresource must address bytes inside the payload just sized.
        for (uint32_t s = 0; s < subresCount; ++s)
        {
            const PackageSubresource& sub = tex.Subresources[s];
            if (sub.Offset + sub.Size > payloadSize)
            {
                LogF(logFn, PackageLogLevel::Error,
                     "ReadTxtrChunk: texture %u: subresource %u range [%llu, %llu) exceeds the "
                     "%llu-byte payload", i, s, static_cast<unsigned long long>(sub.Offset),
                     static_cast<unsigned long long>(sub.Offset + sub.Size),
                     static_cast<unsigned long long>(payloadSize));
                return Gem::Result::CorruptedData;
            }
        }

        tex.Bytes.resize(static_cast<size_t>(payloadSize));
        if (payloadSize != 0)
            reader.ReadBytes(tex.Bytes.data(), static_cast<size_t>(payloadSize)); // in range per check

        textures.push_back(std::move(tex));
    }

    out->Textures = std::move(textures);
    return Gem::Result::Success;
}

//--------------------------------------------------------------------------------------------------
// LITE
//--------------------------------------------------------------------------------------------------

void WriteLiteChunk(CCpkgSink& sink, const PackageData& data, const PackageLogFn& logFn)
{
    const size_t lightCount = data.Lights.size();
    if (lightCount > UINT32_MAX)
        ThrowF(logFn, Gem::Result::InvalidArg,
               "WriteLiteChunk: light count %zu exceeds the uint32 limit", lightCount);

    sink.WriteU32(static_cast<uint32_t>(lightCount));

    for (size_t i = 0; i < lightCount; ++i)
    {
        const PackageLight& light = data.Lights[i];

        if (!NameLenFits(light.Name))
            ThrowF(logFn, Gem::Result::InvalidArg,
                   "WriteLiteChunk: light %zu name of %zu bytes exceeds the uint32 length prefix",
                   i, light.Name.size());

        WriteName(sink, light.Name);
        sink.WriteU32(static_cast<uint32_t>(light.Type));
        sink.WriteFloats(light.Color.V, 4);
        sink.WriteFloat(light.Intensity);
        sink.WriteFloat(light.Range);
        sink.WriteFloat(light.AttenuationConst);
        sink.WriteFloat(light.AttenuationLinear);
        sink.WriteFloat(light.AttenuationQuad);
        sink.WriteFloat(light.SpotInnerAngle);
        sink.WriteFloat(light.SpotOuterAngle);
    }
}

Gem::Result ReadLiteChunk(CCpkgReader& reader, PackageData* out, const PackageLogFn& logFn)
{
    if (!out)
    {
        LogF(logFn, PackageLogLevel::Error, "ReadLiteChunk: null output pointer");
        return Gem::Result::BadPointer;
    }

    uint32_t lightCount = 0;
    if (!reader.ReadU32s(&lightCount, 1))
    {
        LogF(logFn, PackageLogLevel::Error,
             "ReadLiteChunk: truncated chunk header; have %zu bytes", reader.BytesRemaining());
        return Gem::Result::CorruptedData;
    }

    std::vector<PackageLight> lights;

    for (uint32_t i = 0; i < lightCount; ++i)
    {
        PackageLight light;

        const Gem::Result nameResult = ReadName(reader, &light.Name, "ReadLiteChunk: light", i,
                                                logFn);
        if (Gem::Failed(nameResult))
            return nameResult;

        uint32_t type = 0;
        if (!reader.ReadU32s(&type, 1))
        {
            LogF(logFn, PackageLogLevel::Error,
                 "ReadLiteChunk: light %u: truncated body; have %zu bytes", i,
                 reader.BytesRemaining());
            return Gem::Result::CorruptedData;
        }
        if (type > static_cast<uint32_t>(LightType::Area))
        {
            LogF(logFn, PackageLogLevel::Error,
                 "ReadLiteChunk: light %u: unknown LightType %u (valid 0..%u)", i, type,
                 static_cast<uint32_t>(LightType::Area));
            return Gem::Result::CorruptedData;
        }
        light.Type = static_cast<LightType>(type);

        if (!reader.ReadFloats(light.Color.V, 4)
            || !reader.ReadFloats(&light.Intensity, 1)
            || !reader.ReadFloats(&light.Range, 1)
            || !reader.ReadFloats(&light.AttenuationConst, 1)
            || !reader.ReadFloats(&light.AttenuationLinear, 1)
            || !reader.ReadFloats(&light.AttenuationQuad, 1)
            || !reader.ReadFloats(&light.SpotInnerAngle, 1)
            || !reader.ReadFloats(&light.SpotOuterAngle, 1))
        {
            LogF(logFn, PackageLogLevel::Error,
                 "ReadLiteChunk: light %u: truncated body; have %zu bytes", i,
                 reader.BytesRemaining());
            return Gem::Result::CorruptedData;
        }

        lights.push_back(std::move(light));
    }

    out->Lights = std::move(lights);
    return Gem::Result::Success;
}

//--------------------------------------------------------------------------------------------------
// CAMR
//--------------------------------------------------------------------------------------------------

void WriteCamrChunk(CCpkgSink& sink, const PackageData& data, const PackageLogFn& logFn)
{
    const size_t cameraCount = data.Cameras.size();
    if (cameraCount > UINT32_MAX)
        ThrowF(logFn, Gem::Result::InvalidArg,
               "WriteCamrChunk: camera count %zu exceeds the uint32 limit", cameraCount);

    sink.WriteU32(static_cast<uint32_t>(cameraCount));

    for (size_t i = 0; i < cameraCount; ++i)
    {
        const PackageCamera& camera = data.Cameras[i];

        if (!NameLenFits(camera.Name))
            ThrowF(logFn, Gem::Result::InvalidArg,
                   "WriteCamrChunk: camera %zu name of %zu bytes exceeds the uint32 length prefix",
                   i, camera.Name.size());

        WriteName(sink, camera.Name);
        sink.WriteFloat(camera.NearZ);
        sink.WriteFloat(camera.FarZ);
        sink.WriteFloat(camera.FovY);
        sink.WriteFloat(camera.AspectRatio);
    }
}

Gem::Result ReadCamrChunk(CCpkgReader& reader, PackageData* out, const PackageLogFn& logFn)
{
    if (!out)
    {
        LogF(logFn, PackageLogLevel::Error, "ReadCamrChunk: null output pointer");
        return Gem::Result::BadPointer;
    }

    uint32_t cameraCount = 0;
    if (!reader.ReadU32s(&cameraCount, 1))
    {
        LogF(logFn, PackageLogLevel::Error,
             "ReadCamrChunk: truncated chunk header; have %zu bytes", reader.BytesRemaining());
        return Gem::Result::CorruptedData;
    }

    std::vector<PackageCamera> cameras;

    for (uint32_t i = 0; i < cameraCount; ++i)
    {
        PackageCamera camera;

        const Gem::Result nameResult = ReadName(reader, &camera.Name, "ReadCamrChunk: camera", i,
                                                logFn);
        if (Gem::Failed(nameResult))
            return nameResult;

        if (!reader.ReadFloats(&camera.NearZ, 1)
            || !reader.ReadFloats(&camera.FarZ, 1)
            || !reader.ReadFloats(&camera.FovY, 1)
            || !reader.ReadFloats(&camera.AspectRatio, 1))
        {
            LogF(logFn, PackageLogLevel::Error,
                 "ReadCamrChunk: camera %u: truncated body; have %zu bytes", i,
                 reader.BytesRemaining());
            return Gem::Result::CorruptedData;
        }

        cameras.push_back(std::move(camera));
    }

    out->Cameras = std::move(cameras);
    return Gem::Result::Success;
}

} // namespace Canvas::Cpkg
