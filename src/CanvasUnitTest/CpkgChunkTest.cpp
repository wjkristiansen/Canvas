#include "pch.h"
#include "CpkgChunks.h"
#include "CpkgIO.h"
#include "CpkgSink.h"
#include "CpkgTestUtil.h"

#include <cstring>
#include <string>
#include <vector>

namespace CanvasUnitTest
{

using namespace Canvas::Cpkg;

namespace
{
    // Stream a single-chunk container (header + 1-entry table + the chunk writeChunk produces) to
    // a temp file and return the whole file image, so the chunk readers can be exercised against
    // real file offsets (the vertex stream alignment is relative to the file start).
    template <typename WriteChunkFn>
    std::vector<uint8_t> WriteSingleChunkFile(const wchar_t* tempName, uint32_t fourcc,
                                              uint16_t version, WriteChunkFn writeChunk,
                                              uint64_t* outDataOffset, uint32_t* outSizeRaw)
    {
        TempFile tmp(tempName);
        CCpkgSink sink;
        EXPECT_EQ(CCpkgSink::CreateFile(tmp.wstr().c_str(), &sink), Gem::Result::Success);
        WriteCpkgHeader(sink, 1);
        const uint64_t tableOffset = sink.Tell();
        WriteChunkTable(sink, 1);

        sink.PadToAlignment(4);
        const uint64_t dataOffset = sink.Tell();
        writeChunk(sink);
        const uint32_t sizeRaw = static_cast<uint32_t>(sink.Tell() - dataOffset);

        EXPECT_EQ(PatchChunkEntry(sink, tableOffset, 0, fourcc, version, dataOffset, sizeRaw),
                  Gem::Result::Success);
        EXPECT_EQ(sink.Close(), Gem::Result::Success);

        *outDataOffset = dataOffset;
        if (outSizeRaw)
            *outSizeRaw = sizeRaw;
        return ReadFileBytes(tmp.path);
    }

    void ExpectVec2Eq(const Math::FloatVector2& actual, const Math::FloatVector2& expected)
    {
        EXPECT_EQ(actual.X, expected.X);
        EXPECT_EQ(actual.Y, expected.Y);
    }

    void ExpectVec4Eq(const Math::FloatVector4& actual, const Math::FloatVector4& expected)
    {
        for (int i = 0; i < 4; ++i)
            EXPECT_EQ(actual.V[i], expected.V[i]);
    }

    void ExpectMatrixEq(const Math::FloatMatrix4x4& actual, const Math::FloatMatrix4x4& expected)
    {
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                EXPECT_EQ(actual[r][c], expected[r][c]);
    }

    // A part with recognisable per-vertex values in every requested stream.
    PackageMeshPart MakePart(int32_t materialIndex, uint32_t vertexCount,
                             bool uv0, bool tangents, bool skin)
    {
        PackageMeshPart part;
        part.MaterialIndex = materialIndex;
        for (uint32_t i = 0; i < vertexCount; ++i)
        {
            const float f = static_cast<float>(i);
            part.Positions.push_back({ f + 0.25f, f + 0.5f, f + 0.75f, 1.0f });
            part.Normals.push_back({ 0.0f, f + 1.0f, 0.0f, 0.0f });
            if (uv0)
                part.UV0.push_back({ f * 0.125f, 1.0f - f * 0.125f });
            if (tangents)
                part.Tangents.push_back({ 1.0f, 0.0f, f, -1.0f });
            if (skin)
            {
                PackageSkinVertex sv;
                for (uint32_t b = 0; b < 4; ++b)
                {
                    sv.BoneIndices[b] = i + b;
                    sv.BoneWeights[b] = 0.4f - 0.1f * static_cast<float>(b);
                }
                part.SkinVertices.push_back(sv);
            }
        }
        return part;
    }

    void ExpectPartEq(const PackageMeshPart& actual, const PackageMeshPart& expected)
    {
        EXPECT_EQ(actual.MaterialIndex, expected.MaterialIndex);

        ASSERT_EQ(actual.Positions.size(), expected.Positions.size());
        ASSERT_EQ(actual.Normals.size(), expected.Normals.size());
        ASSERT_EQ(actual.UV0.size(), expected.UV0.size());
        ASSERT_EQ(actual.Tangents.size(), expected.Tangents.size());
        ASSERT_EQ(actual.SkinVertices.size(), expected.SkinVertices.size());

        for (size_t i = 0; i < expected.Positions.size(); ++i)
            ExpectVec4Eq(actual.Positions[i], expected.Positions[i]);
        for (size_t i = 0; i < expected.Normals.size(); ++i)
            ExpectVec4Eq(actual.Normals[i], expected.Normals[i]);
        for (size_t i = 0; i < expected.UV0.size(); ++i)
            ExpectVec2Eq(actual.UV0[i], expected.UV0[i]);
        for (size_t i = 0; i < expected.Tangents.size(); ++i)
            ExpectVec4Eq(actual.Tangents[i], expected.Tangents[i]);
        for (size_t i = 0; i < expected.SkinVertices.size(); ++i)
        {
            for (int b = 0; b < 4; ++b)
            {
                EXPECT_EQ(actual.SkinVertices[i].BoneIndices[b],
                          expected.SkinVertices[i].BoneIndices[b]);
                EXPECT_EQ(actual.SkinVertices[i].BoneWeights[b],
                          expected.SkinVertices[i].BoneWeights[b]);
            }
        }
    }
}

//--------------------------------------------------------------------------------------------------
// NODE
//--------------------------------------------------------------------------------------------------

// Root, two children, one grandchild; distinct TRS everywhere, one node with all three payload
// indices bound, one unnamed node.
TEST(CpkgChunkTest, NodeRoundTrip)
{
    PackageData original;
    original.ActiveCameraNodeIndex = 2;

    PackageNode root;
    root.Name        = "Root";
    root.ParentIndex = -1;
    root.Translation = { 1.0f, 2.0f, 3.0f, 0.0f };
    root.Scale       = { 2.0f, 2.0f, 2.0f, 0.0f };
    root.Rotation    = { 0.0f, 0.0f, 0.0f, 1.0f };
    original.Nodes.push_back(root);

    PackageNode childA;
    childA.Name        = "ChildA";
    childA.ParentIndex = 0;
    childA.Translation = { -4.0f, 5.5f, 0.25f, 0.0f };
    childA.Scale       = { 1.0f, 0.5f, 0.25f, 0.0f };
    childA.Rotation    = { 0.5f, 0.5f, 0.5f, 0.5f };
    childA.MeshIndex   = 3;
    original.Nodes.push_back(childA);

    PackageNode childB; // all three payload indices bound
    childB.Name        = "ChildB";
    childB.ParentIndex = 0;
    childB.Translation = { 7.0f, -8.0f, 9.0f, 0.0f };
    childB.Scale       = { 1.0f, 1.0f, 1.0f, 0.0f };
    childB.Rotation    = { 0.0f, 0.70710678f, 0.0f, 0.70710678f };
    childB.MeshIndex   = 0;
    childB.LightIndex  = 1;
    childB.CameraIndex = 5;
    original.Nodes.push_back(childB);

    PackageNode grandChild; // unnamed, light only
    grandChild.ParentIndex = 1;
    grandChild.Translation = { 0.0f, 0.0f, -1.5f, 0.0f };
    grandChild.Scale       = { 3.0f, 3.0f, 3.0f, 0.0f };
    grandChild.LightIndex  = 0;
    original.Nodes.push_back(grandChild);

    uint64_t dataOffset = 0;
    uint32_t sizeRaw    = 0;
    std::vector<uint8_t> file = WriteSingleChunkFile(
        L"canvas_cpkg_chunk_node.cpkg", CPKG_FOURCC_NODE, CPKG_NODE_CHUNK_VERSION,
        [&](CCpkgSink& sink) { WriteNodeChunk(sink, original); },
        &dataOffset, &sizeRaw);

    CCpkgReader reader(file.data(), file.size());
    reader.SetOffset(static_cast<size_t>(dataOffset));
    PackageData readBack;
    ASSERT_EQ(ReadNodeChunk(reader, &readBack), Gem::Result::Success);

    // The cursor must land exactly on the chunk end: write and read agree on every byte.
    EXPECT_EQ(reader.GetOffset(), static_cast<size_t>(dataOffset) + sizeRaw);

    EXPECT_EQ(readBack.ActiveCameraNodeIndex, original.ActiveCameraNodeIndex);
    ASSERT_EQ(readBack.Nodes.size(), original.Nodes.size());
    for (size_t i = 0; i < original.Nodes.size(); ++i)
    {
        const PackageNode& e = original.Nodes[i];
        const PackageNode& a = readBack.Nodes[i];
        EXPECT_EQ(a.Name, e.Name);
        EXPECT_EQ(a.ParentIndex, e.ParentIndex);
        ExpectVec4Eq(a.Translation, e.Translation);
        ExpectVec4Eq(a.Rotation, e.Rotation);
        ExpectVec4Eq(a.Scale, e.Scale);
        EXPECT_EQ(a.MeshIndex, e.MeshIndex);
        EXPECT_EQ(a.LightIndex, e.LightIndex);
        EXPECT_EQ(a.CameraIndex, e.CameraIndex);
    }
}

TEST(CpkgChunkTest, WriteNodeChunkRejectsOutOfRangeParentIndex)
{
    PackageData data;
    PackageNode node;
    node.Name        = "Orphan";
    node.ParentIndex = 4; // only one node exists
    data.Nodes.push_back(node);

    TempFile tmp(L"canvas_cpkg_chunk_node_reject.cpkg");
    CCpkgSink sink;
    ASSERT_EQ(CCpkgSink::CreateFile(tmp.wstr().c_str(), &sink), Gem::Result::Success);
    ExpectCpkgError(Gem::Result::InvalidArg, [&] { WriteNodeChunk(sink, data); });
}

TEST(CpkgChunkTest, ReadNodeChunkRejectsTruncatedChunk)
{
    PackageData original;
    PackageNode node;
    node.Name = "OnlyNode";
    original.Nodes.push_back(node);

    uint64_t dataOffset = 0;
    uint32_t sizeRaw    = 0;
    std::vector<uint8_t> file = WriteSingleChunkFile(
        L"canvas_cpkg_chunk_node_trunc.cpkg", CPKG_FOURCC_NODE, CPKG_NODE_CHUNK_VERSION,
        [&](CCpkgSink& sink) { WriteNodeChunk(sink, original); },
        &dataOffset, &sizeRaw);

    // Present a span that ends 10 bytes short of the chunk's declared extent.
    CCpkgReader reader(file.data(), static_cast<size_t>(dataOffset) + sizeRaw - 10);
    reader.SetOffset(static_cast<size_t>(dataOffset));
    PackageData readBack;
    EXPECT_EQ(ReadNodeChunk(reader, &readBack), Gem::Result::CorruptedData);
}

//--------------------------------------------------------------------------------------------------
// MESH
//--------------------------------------------------------------------------------------------------

// Test A: one mesh, one part, 6 vertices, every stream present.
TEST(CpkgChunkTest, MeshRoundTripFullStreams)
{
    PackageData original;
    PackageMesh mesh;
    mesh.Name   = "FullStreams";
    mesh.Bounds = Math::AABB({ -1.0f, -2.0f, -3.0f, 0.0f }, { 4.0f, 5.0f, 6.0f, 0.0f });
    mesh.Parts.push_back(MakePart(2, 6, true, true, true));
    mesh.Skin.HasSkin = true;
    mesh.Skin.BoneNodeIndices = { 1, 2 };
    mesh.Skin.InvBindPoses.resize(2, Math::FloatMatrix4x4::Identity());
    original.Meshes.push_back(mesh);

    uint64_t dataOffset = 0;
    uint32_t sizeRaw    = 0;
    std::vector<uint8_t> file = WriteSingleChunkFile(
        L"canvas_cpkg_chunk_mesh_full.cpkg", CPKG_FOURCC_MESH, CPKG_MESH_CHUNK_VERSION,
        [&](CCpkgSink& sink) { WriteMeshChunk(sink, original); },
        &dataOffset, &sizeRaw);

    CCpkgReader reader(file.data(), file.size());
    reader.SetOffset(static_cast<size_t>(dataOffset));
    PackageData readBack;
    ASSERT_EQ(ReadMeshChunk(reader, &readBack), Gem::Result::Success);
    EXPECT_EQ(reader.GetOffset(), static_cast<size_t>(dataOffset) + sizeRaw);

    ASSERT_EQ(readBack.Meshes.size(), size_t(1));
    const PackageMesh& a = readBack.Meshes[0];
    EXPECT_EQ(a.Name, mesh.Name);
    ExpectVec4Eq(a.Bounds.Min, mesh.Bounds.Min);
    ExpectVec4Eq(a.Bounds.Max, mesh.Bounds.Max);
    ASSERT_EQ(a.Parts.size(), size_t(1));
    ExpectPartEq(a.Parts[0], mesh.Parts[0]);
    EXPECT_TRUE(a.Skin.HasSkin);
    EXPECT_EQ(a.Skin.BoneNodeIndices, mesh.Skin.BoneNodeIndices);
}

// Test B: 3 vertices, Positions + Normals only (StreamFlags = 0); the optional streams must come
// back empty.
TEST(CpkgChunkTest, MeshRoundTripMinimalStreams)
{
    PackageData original;
    PackageMesh mesh;
    mesh.Name   = "Minimal";
    mesh.Bounds = Math::AABB({ 0.0f, 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 0.0f });
    mesh.Parts.push_back(MakePart(-1, 3, false, false, false));
    original.Meshes.push_back(mesh);

    uint64_t dataOffset = 0;
    std::vector<uint8_t> file = WriteSingleChunkFile(
        L"canvas_cpkg_chunk_mesh_min.cpkg", CPKG_FOURCC_MESH, CPKG_MESH_CHUNK_VERSION,
        [&](CCpkgSink& sink) { WriteMeshChunk(sink, original); },
        &dataOffset, nullptr);

    CCpkgReader reader(file.data(), file.size());
    reader.SetOffset(static_cast<size_t>(dataOffset));
    PackageData readBack;
    ASSERT_EQ(ReadMeshChunk(reader, &readBack), Gem::Result::Success);

    ASSERT_EQ(readBack.Meshes.size(), size_t(1));
    ASSERT_EQ(readBack.Meshes[0].Parts.size(), size_t(1));
    const PackageMeshPart& a = readBack.Meshes[0].Parts[0];
    ExpectPartEq(a, mesh.Parts[0]);
    EXPECT_TRUE(a.UV0.empty());
    EXPECT_TRUE(a.Tangents.empty());
    EXPECT_TRUE(a.SkinVertices.empty());
    EXPECT_FALSE(readBack.Meshes[0].Skin.HasSkin);
    EXPECT_TRUE(readBack.Meshes[0].Skin.BoneNodeIndices.empty());
    EXPECT_TRUE(readBack.Meshes[0].Skin.InvBindPoses.empty());
}

// Test C: skinned mesh with 3 bones; bone node indices and inverse bind poses must survive.
TEST(CpkgChunkTest, MeshRoundTripSkinned)
{
    PackageData original;
    PackageMesh mesh;
    mesh.Name   = "Skinned";
    mesh.Bounds = Math::AABB({ -2.0f, -2.0f, -2.0f, 0.0f }, { 2.0f, 2.0f, 2.0f, 0.0f });
    mesh.Parts.push_back(MakePart(0, 4, false, false, true));
    mesh.Skin.HasSkin = true;
    mesh.Skin.BoneNodeIndices = { 7, 11, 13 };
    for (int bone = 0; bone < 3; ++bone)
    {
        Math::FloatMatrix4x4 pose;
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                pose[r][c] = static_cast<float>(bone * 100 + r * 10 + c);
        mesh.Skin.InvBindPoses.push_back(pose);
    }
    original.Meshes.push_back(mesh);

    uint64_t dataOffset = 0;
    std::vector<uint8_t> file = WriteSingleChunkFile(
        L"canvas_cpkg_chunk_mesh_skin.cpkg", CPKG_FOURCC_MESH, CPKG_MESH_CHUNK_VERSION,
        [&](CCpkgSink& sink) { WriteMeshChunk(sink, original); },
        &dataOffset, nullptr);

    CCpkgReader reader(file.data(), file.size());
    reader.SetOffset(static_cast<size_t>(dataOffset));
    PackageData readBack;
    ASSERT_EQ(ReadMeshChunk(reader, &readBack), Gem::Result::Success);

    ASSERT_EQ(readBack.Meshes.size(), size_t(1));
    const PackageSkin& skin = readBack.Meshes[0].Skin;
    EXPECT_TRUE(skin.HasSkin);
    EXPECT_EQ(skin.BoneNodeIndices, mesh.Skin.BoneNodeIndices);
    ASSERT_EQ(skin.InvBindPoses.size(), mesh.Skin.InvBindPoses.size());
    for (size_t i = 0; i < mesh.Skin.InvBindPoses.size(); ++i)
        ExpectMatrixEq(skin.InvBindPoses[i], mesh.Skin.InvBindPoses[i]);
    ExpectPartEq(readBack.Meshes[0].Parts[0], mesh.Parts[0]);
}

// Every vertex stream must start on a 16-byte file boundary. The descriptor read reports each
// stream's absolute byte range, so it both proves the alignment and pins the recorded ranges to
// the original bytes.
TEST(CpkgChunkTest, MeshStreamAlignment)
{
    PackageData original;
    PackageMesh mesh;
    mesh.Name   = "Mesh_A"; // odd header sizes so the alignment pads are non-trivial
    mesh.Bounds = Math::AABB({ 0.0f, 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 0.0f });
    mesh.Parts.push_back(MakePart(0, 5, true, true, true));
    mesh.Parts.push_back(MakePart(1, 3, true, false, false));
    mesh.Skin.HasSkin = true;
    mesh.Skin.BoneNodeIndices = { 0 };
    mesh.Skin.InvBindPoses.resize(1, Math::FloatMatrix4x4::Identity());
    original.Meshes.push_back(mesh);

    uint64_t dataOffset = 0;
    uint32_t sizeRaw    = 0;
    std::vector<uint8_t> file = WriteSingleChunkFile(
        L"canvas_cpkg_chunk_mesh_align.cpkg", CPKG_FOURCC_MESH, CPKG_MESH_CHUNK_VERSION,
        [&](CCpkgSink& sink) { WriteMeshChunk(sink, original); },
        &dataOffset, &sizeRaw);

    // Descriptor read: chunk-relative reader + the chunk's file offset.
    CCpkgReader reader(file.data() + dataOffset, sizeRaw);
    MeshDescriptors descriptors;
    ASSERT_EQ(ReadMeshDescriptors(reader, dataOffset, &descriptors), Gem::Result::Success);

    ASSERT_EQ(descriptors.Meshes.size(), size_t(1));
    const MeshDescriptor& desc = descriptors.Meshes[0];
    EXPECT_EQ(desc.Name, mesh.Name);
    EXPECT_TRUE(desc.Skin.HasSkin);
    EXPECT_EQ(desc.Skin.BoneNodeIndices, mesh.Skin.BoneNodeIndices);
    ASSERT_EQ(desc.Parts.size(), mesh.Parts.size());

    for (size_t p = 0; p < desc.Parts.size(); ++p)
    {
        const MeshPartDescriptor& part = desc.Parts[p];
        const PackageMeshPart& src = mesh.Parts[p];
        EXPECT_EQ(part.MaterialIndex, src.MaterialIndex);
        EXPECT_EQ(part.VertexCount, static_cast<uint32_t>(src.Positions.size()));

        const MeshStreamRange* ranges[] = {
            &part.Positions, &part.Normals, &part.UV0, &part.Tangents, &part.SkinVertices,
        };
        for (const MeshStreamRange* range : ranges)
        {
            if (range->Size == 0)
                continue;
            EXPECT_EQ(range->Offset % 16, 0u);                       // 16-byte file alignment
            EXPECT_LE(range->Offset + range->Size, file.size());     // range stays inside the file
        }

        // The recorded ranges must address the original stream bytes.
        EXPECT_EQ(part.Positions.Size, src.Positions.size() * sizeof(Math::FloatVector4));
        EXPECT_EQ(std::memcmp(file.data() + part.Positions.Offset, src.Positions.data(),
                              static_cast<size_t>(part.Positions.Size)), 0);
        EXPECT_EQ(part.UV0.Size, src.UV0.size() * sizeof(Math::FloatVector2));
        EXPECT_EQ(std::memcmp(file.data() + part.UV0.Offset, src.UV0.data(),
                              static_cast<size_t>(part.UV0.Size)), 0);
    }

    // Part 1 carries UV0 but neither Tangents nor SkinVertices.
    EXPECT_EQ(descriptors.Meshes[0].Parts[1].StreamFlags, CPKG_MESH_STREAM_UV0);
    EXPECT_EQ(descriptors.Meshes[0].Parts[1].Tangents.Size, 0u);
    EXPECT_EQ(descriptors.Meshes[0].Parts[1].SkinVertices.Size, 0u);
}

TEST(CpkgChunkTest, WriteMeshChunkRejectsMismatchedStreamSizes)
{
    PackageData data;
    PackageMesh mesh;
    mesh.Name = "Broken";
    PackageMeshPart part = MakePart(0, 4, false, false, false);
    part.Normals.pop_back(); // 3 normals for 4 positions
    mesh.Parts.push_back(part);
    data.Meshes.push_back(mesh);

    TempFile tmp(L"canvas_cpkg_chunk_mesh_reject.cpkg");
    CCpkgSink sink;
    ASSERT_EQ(CCpkgSink::CreateFile(tmp.wstr().c_str(), &sink), Gem::Result::Success);
    ExpectCpkgError(Gem::Result::InvalidArg, [&] { WriteMeshChunk(sink, data); });
}

TEST(CpkgChunkTest, ReadMeshChunkRejectsUnknownStreamFlags)
{
    PackageData original;
    PackageMesh mesh;
    mesh.Name   = "Flags";
    mesh.Bounds = Math::AABB({ 0.0f, 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 0.0f });
    mesh.Parts.push_back(MakePart(0, 3, false, false, false));
    original.Meshes.push_back(mesh);

    uint64_t dataOffset = 0;
    std::vector<uint8_t> file = WriteSingleChunkFile(
        L"canvas_cpkg_chunk_mesh_flags.cpkg", CPKG_FOURCC_MESH, CPKG_MESH_CHUNK_VERSION,
        [&](CCpkgSink& sink) { WriteMeshChunk(sink, original); },
        &dataOffset, nullptr);

    // StreamFlags sits after MeshCount, the name record, PartCount, the bounds, and the part's
    // MaterialIndex + VertexCount.
    const size_t flagsOffset = static_cast<size_t>(dataOffset)
                             + sizeof(uint32_t)                          // MeshCount
                             + sizeof(uint32_t) + mesh.Name.size() + 1   // NameLen + name + '\0'
                             + sizeof(uint32_t)                          // PartCount
                             + 6 * sizeof(float)                         // bounds
                             + sizeof(int32_t) + sizeof(uint32_t);       // MaterialIndex + VertexCount
    file[flagsOffset] |= 0x80u; // set a bit no v1 stream defines

    CCpkgReader reader(file.data(), file.size());
    reader.SetOffset(static_cast<size_t>(dataOffset));
    PackageData readBack;
    EXPECT_EQ(ReadMeshChunk(reader, &readBack), Gem::Result::CorruptedData);
}

} // namespace CanvasUnitTest
