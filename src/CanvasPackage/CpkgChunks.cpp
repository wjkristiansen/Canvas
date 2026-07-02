#include "pch.h"
#include "CpkgChunks.h"
#include "CpkgLog.h"

#include <cstdio>
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

    // Read a length-prefixed name written by WriteName. context names the caller in log records
    // (e.g. "ReadNodeChunk: node 3"). Fails with CorruptedData on truncation, a zero length, or a
    // missing terminator.
    Gem::Result ReadName(CCpkgReader& reader, std::string* out, const char* context,
                         const PackageLogFn& logFn)
    {
        if (reader.BytesRemaining() < sizeof(uint32_t))
        {
            LogF(logFn, PackageLogLevel::Error,
                 "%s: truncated reading name length; have %zu bytes", context,
                 reader.BytesRemaining());
            return Gem::Result::CorruptedData;
        }

        const uint32_t len = reader.ReadU32();
        if (len == 0)
        {
            LogF(logFn, PackageLogLevel::Error,
                 "%s: name length 0 (the terminator is included, so the minimum is 1)", context);
            return Gem::Result::CorruptedData;
        }
        if (len > reader.BytesRemaining())
        {
            LogF(logFn, PackageLogLevel::Error,
                 "%s: truncated name; length %u exceeds the %zu bytes remaining",
                 context, len, reader.BytesRemaining());
            return Gem::Result::CorruptedData;
        }

        std::string name(len, '\0');
        reader.ReadBytes(name.data(), len); // in range per the check above
        if (name.back() != '\0')
        {
            LogF(logFn, PackageLogLevel::Error,
                 "%s: name of length %u is not null-terminated", context, len);
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

    // Fixed-size portion of one node record after the name: ParentIndex + Translation + Rotation +
    // Scale + the three payload indices.
    constexpr size_t kNodeFixedBytes = sizeof(int32_t) + 3 * 4 * sizeof(float) + 3 * sizeof(int32_t);

    // Smallest possible node record on disk: name length prefix + terminator + the fixed fields.
    // Used to sanity-check NodeCount against the bytes remaining before anything is allocated.
    constexpr size_t kMinNodeDiskBytes = sizeof(uint32_t) + 1 + kNodeFixedBytes;

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

    // The vertex streams and inverse bind poses are block-copied between the file and the
    // in-memory vectors, so the element types must match the on-disk strides exactly.
    static_assert(sizeof(Math::FloatVector4) == 16,
                  "FloatVector4 drifted from the float[4] on-disk stream stride");
    static_assert(sizeof(Math::FloatVector2) == 8,
                  "FloatVector2 drifted from the float[2] on-disk stream stride");
    static_assert(sizeof(PackageSkinVertex) == 32,
                  "PackageSkinVertex drifted from the {uint32[4], float[4]} on-disk stride");
    static_assert(sizeof(Math::FloatMatrix4x4) == 64,
                  "FloatMatrix4x4 drifted from the float[16] on-disk inverse-bind-pose stride");

    constexpr size_t kStreamAlignment = 16;

    // Fixed-size per-part header: MaterialIndex + VertexCount + StreamFlags + 3 pad bytes.
    constexpr size_t kPartHeaderBytes = sizeof(int32_t) + sizeof(uint32_t) + 4;

    // Fixed-size per-mesh skin block prefix: HasSkin + 3 pad bytes + BoneCount.
    constexpr size_t kSkinPrefixBytes = 4 + sizeof(uint32_t);

    // Per-bone payload: one node index plus one inverse bind pose.
    constexpr size_t kBoneDiskBytes = sizeof(int32_t) + sizeof(Math::FloatMatrix4x4);

    // Smallest possible records on disk, used to sanity-check declared counts against the bytes
    // remaining before anything is allocated.
    constexpr size_t kMinMeshDiskBytes = sizeof(uint32_t) + 1     // name length prefix + terminator
                                       + sizeof(uint32_t)         // PartCount
                                       + 6 * sizeof(float)        // BoundsMin + BoundsMax
                                       + kSkinPrefixBytes;
    constexpr size_t kMinPartDiskBytes = kPartHeaderBytes;        // all alignment pads can be 0

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

    // Skip the zero padding that places the next vertex stream on a 16-byte file boundary.
    // base rebases the reader's local offset space to absolute file offsets (0 when the reader is
    // already file-absolute).
    Gem::Result SkipStreamAlignment(CCpkgReader& reader, uint64_t base, const char* context,
                                    const PackageLogFn& logFn)
    {
        const uint64_t absolute = base + reader.GetOffset();
        const size_t pad = static_cast<size_t>((kStreamAlignment - absolute % kStreamAlignment)
                                               % kStreamAlignment);
        if (!reader.SkipBytes(pad))
        {
            LogF(logFn, PackageLogLevel::Error,
                 "%s: truncated in stream alignment padding; need %zu bytes, have %zu",
                 context, pad, reader.BytesRemaining());
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
                           const char* context, const char* streamName, const PackageLogFn& logFn)
    {
        const Gem::Result aligned = SkipStreamAlignment(reader, base, context, logFn);
        if (Gem::Failed(aligned))
            return aligned;

        const uint64_t size = static_cast<uint64_t>(vertexCount) * sizeof(TElement);
        if (size > reader.BytesRemaining())
        {
            LogF(logFn, PackageLogLevel::Error,
                 "%s: truncated %s stream; need %llu bytes for %u vertices, have %zu",
                 context, streamName, static_cast<unsigned long long>(size), vertexCount,
                 reader.BytesRemaining());
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
        if (reader.BytesRemaining() < sizeof(uint32_t))
        {
            LogF(logFn, PackageLogLevel::Error,
                 "%s: truncated chunk header; have %zu bytes", fn, reader.BytesRemaining());
            return Gem::Result::CorruptedData;
        }

        const uint32_t meshCount = reader.ReadU32();
        if (static_cast<uint64_t>(meshCount) * kMinMeshDiskBytes > reader.BytesRemaining())
        {
            LogF(logFn, PackageLogLevel::Error,
                 "%s: mesh count %u needs at least %llu bytes, have %zu", fn, meshCount,
                 static_cast<unsigned long long>(meshCount) * kMinMeshDiskBytes,
                 reader.BytesRemaining());
            return Gem::Result::CorruptedData;
        }

        std::vector<PackageMesh>    meshes;
        std::vector<MeshDescriptor> descriptors;

        for (uint32_t m = 0; m < meshCount; ++m)
        {
            char context[64];
            std::snprintf(context, sizeof(context), "%s: mesh %u", fn, m);

            std::string name;
            const Gem::Result nameResult = ReadName(reader, &name, context, logFn);
            if (Gem::Failed(nameResult))
                return nameResult;

            if (reader.BytesRemaining() < sizeof(uint32_t) + 6 * sizeof(float))
            {
                LogF(logFn, PackageLogLevel::Error,
                     "%s: truncated mesh header; have %zu bytes", context, reader.BytesRemaining());
                return Gem::Result::CorruptedData;
            }

            const uint32_t partCount = reader.ReadU32();
            float boundsMin[3];
            float boundsMax[3];
            reader.ReadFloats(boundsMin, 3);
            reader.ReadFloats(boundsMax, 3);

            if (static_cast<uint64_t>(partCount) * kMinPartDiskBytes > reader.BytesRemaining())
            {
                LogF(logFn, PackageLogLevel::Error,
                     "%s: part count %u needs at least %llu bytes, have %zu", context, partCount,
                     static_cast<unsigned long long>(partCount) * kMinPartDiskBytes,
                     reader.BytesRemaining());
                return Gem::Result::CorruptedData;
            }

            PackageMesh    mesh;
            MeshDescriptor desc;
            const Math::AABB bounds(
                Math::FloatVector4(boundsMin[0], boundsMin[1], boundsMin[2], 0.0f),
                Math::FloatVector4(boundsMax[0], boundsMax[1], boundsMax[2], 0.0f));

            for (uint32_t p = 0; p < partCount; ++p)
            {
                char partContext[64];
                std::snprintf(partContext, sizeof(partContext), "%s: mesh %u part %u", fn, m, p);

                if (reader.BytesRemaining() < kPartHeaderBytes)
                {
                    LogF(logFn, PackageLogLevel::Error,
                         "%s: truncated part header; need %zu bytes, have %zu",
                         partContext, kPartHeaderBytes, reader.BytesRemaining());
                    return Gem::Result::CorruptedData;
                }

                const int32_t  materialIndex = reader.ReadI32();
                const uint32_t vertexCount   = reader.ReadU32();
                const uint8_t  streamFlags   = reader.ReadU8();
                reader.SkipBytes(3); // _pad

                if ((streamFlags & ~CPKG_MESH_STREAM_VALID_MASK) != 0)
                {
                    LogF(logFn, PackageLogLevel::Error,
                         "%s: unknown StreamFlags bits 0x%02X (valid mask 0x%02X)",
                         partContext, streamFlags, CPKG_MESH_STREAM_VALID_MASK);
                    return Gem::Result::CorruptedData;
                }
                if (materialIndex < -1)
                {
                    LogF(logFn, PackageLogLevel::Error,
                         "%s: MaterialIndex %d below -1", partContext, materialIndex);
                    return Gem::Result::CorruptedData;
                }

                PackageMeshPart    part;
                MeshPartDescriptor partDesc;
                part.MaterialIndex     = materialIndex;
                partDesc.MaterialIndex = materialIndex;
                partDesc.VertexCount   = vertexCount;
                partDesc.StreamFlags   = streamFlags;

                // Exactly one destination per stream, matching the outData / outDesc mode.
                std::vector<Math::FloatVector4>* positions = outData ? &part.Positions : nullptr;
                std::vector<Math::FloatVector4>* normals   = outData ? &part.Normals   : nullptr;
                std::vector<Math::FloatVector2>* uv0       = outData ? &part.UV0       : nullptr;
                std::vector<Math::FloatVector4>* tangents  = outData ? &part.Tangents  : nullptr;
                std::vector<PackageSkinVertex>*  skinVerts = outData ? &part.SkinVertices : nullptr;

                Gem::Result streamResult = ReadStream(reader, base, vertexCount, positions,
                                                      &partDesc.Positions, partContext,
                                                      "Positions", logFn);
                if (Gem::Failed(streamResult))
                    return streamResult;

                streamResult = ReadStream(reader, base, vertexCount, normals, &partDesc.Normals,
                                          partContext, "Normals", logFn);
                if (Gem::Failed(streamResult))
                    return streamResult;

                if (streamFlags & CPKG_MESH_STREAM_UV0)
                {
                    streamResult = ReadStream(reader, base, vertexCount, uv0, &partDesc.UV0,
                                              partContext, "UV0", logFn);
                    if (Gem::Failed(streamResult))
                        return streamResult;
                }
                if (streamFlags & CPKG_MESH_STREAM_TANGENTS)
                {
                    streamResult = ReadStream(reader, base, vertexCount, tangents,
                                              &partDesc.Tangents, partContext, "Tangents", logFn);
                    if (Gem::Failed(streamResult))
                        return streamResult;
                }
                if (streamFlags & CPKG_MESH_STREAM_SKIN)
                {
                    streamResult = ReadStream(reader, base, vertexCount, skinVerts,
                                              &partDesc.SkinVertices, partContext, "SkinVertices",
                                              logFn);
                    if (Gem::Failed(streamResult))
                        return streamResult;
                }

                if (outData)
                    mesh.Parts.push_back(std::move(part));
                else
                    desc.Parts.push_back(partDesc);
            }

            // Per-mesh skin block: bounded bone data, parsed fully on both paths.
            if (reader.BytesRemaining() < kSkinPrefixBytes)
            {
                LogF(logFn, PackageLogLevel::Error,
                     "%s: truncated skin block; need %zu bytes, have %zu",
                     context, kSkinPrefixBytes, reader.BytesRemaining());
                return Gem::Result::CorruptedData;
            }

            const uint8_t hasSkin = reader.ReadU8();
            reader.SkipBytes(3); // _pad
            const uint32_t boneCount = reader.ReadU32();

            if (hasSkin > 1)
            {
                LogF(logFn, PackageLogLevel::Error,
                     "%s: HasSkin byte is %u (expected 0 or 1)", context, hasSkin);
                return Gem::Result::CorruptedData;
            }
            const Gem::Result skinValid = ValidateSkin(hasSkin != 0, boneCount, boneCount, m,
                                                       Gem::Result::CorruptedData, fn, logFn);
            if (Gem::Failed(skinValid))
                return skinValid;

            if (static_cast<uint64_t>(boneCount) * kBoneDiskBytes > reader.BytesRemaining())
            {
                LogF(logFn, PackageLogLevel::Error,
                     "%s: truncated skin; %u bones need %llu bytes, have %zu", context, boneCount,
                     static_cast<unsigned long long>(boneCount) * kBoneDiskBytes,
                     reader.BytesRemaining());
                return Gem::Result::CorruptedData;
            }

            PackageSkin skin;
            skin.HasSkin = hasSkin != 0;
            skin.BoneNodeIndices.resize(boneCount);
            skin.InvBindPoses.resize(boneCount);
            if (boneCount != 0)
            {
                reader.ReadI32s(skin.BoneNodeIndices.data(), boneCount);
                reader.ReadBytes(skin.InvBindPoses.data(),
                                 boneCount * sizeof(Math::FloatMatrix4x4));
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

    if (reader.BytesRemaining() < sizeof(uint32_t) + sizeof(int32_t))
    {
        LogF(logFn, PackageLogLevel::Error,
             "ReadNodeChunk: truncated chunk header; have %zu bytes", reader.BytesRemaining());
        return Gem::Result::CorruptedData;
    }

    const uint32_t nodeCount = reader.ReadU32();
    const int32_t activeCameraNodeIndex = reader.ReadI32();

    if (activeCameraNodeIndex < -1 || activeCameraNodeIndex >= static_cast<int64_t>(nodeCount))
    {
        LogF(logFn, PackageLogLevel::Error,
             "ReadNodeChunk: ActiveCameraNodeIndex %d out of range [-1, %u)",
             activeCameraNodeIndex, nodeCount);
        return Gem::Result::CorruptedData;
    }

    // Guard the allocation below: the declared count cannot possibly fit in fewer bytes than the
    // minimum record size times the count.
    if (static_cast<uint64_t>(nodeCount) * kMinNodeDiskBytes > reader.BytesRemaining())
    {
        LogF(logFn, PackageLogLevel::Error,
             "ReadNodeChunk: node count %u needs at least %llu bytes, have %zu",
             nodeCount, static_cast<unsigned long long>(nodeCount) * kMinNodeDiskBytes,
             reader.BytesRemaining());
        return Gem::Result::CorruptedData;
    }

    std::vector<PackageNode> nodes;
    nodes.reserve(nodeCount);

    for (uint32_t i = 0; i < nodeCount; ++i)
    {
        PackageNode node;

        char context[64];
        std::snprintf(context, sizeof(context), "ReadNodeChunk: node %u", i);
        const Gem::Result nameResult = ReadName(reader, &node.Name, context, logFn);
        if (Gem::Failed(nameResult))
            return nameResult;

        if (reader.BytesRemaining() < kNodeFixedBytes)
        {
            LogF(logFn, PackageLogLevel::Error,
                 "ReadNodeChunk: truncated at node %u of %u; need %zu bytes, have %zu",
                 i, nodeCount, kNodeFixedBytes, reader.BytesRemaining());
            return Gem::Result::CorruptedData;
        }

        node.ParentIndex = reader.ReadI32();
        reader.ReadFloats(node.Translation.V, 4);
        reader.ReadFloats(node.Rotation.V, 4);
        reader.ReadFloats(node.Scale.V, 4);
        node.MeshIndex   = reader.ReadI32();
        node.LightIndex  = reader.ReadI32();
        node.CameraIndex = reader.ReadI32();

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
            sink.WriteBytes(part.Positions.data(), vertexCount * sizeof(Math::FloatVector4));
            sink.PadToAlignment(kStreamAlignment);
            sink.WriteBytes(part.Normals.data(), vertexCount * sizeof(Math::FloatVector4));
            if (streamFlags & CPKG_MESH_STREAM_UV0)
            {
                sink.PadToAlignment(kStreamAlignment);
                sink.WriteBytes(part.UV0.data(), vertexCount * sizeof(Math::FloatVector2));
            }
            if (streamFlags & CPKG_MESH_STREAM_TANGENTS)
            {
                sink.PadToAlignment(kStreamAlignment);
                sink.WriteBytes(part.Tangents.data(), vertexCount * sizeof(Math::FloatVector4));
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
            sink.WriteBytes(skin.InvBindPoses.data(), boneCount * sizeof(Math::FloatMatrix4x4));
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

} // namespace Canvas::Cpkg
