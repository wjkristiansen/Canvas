#include "pch.h"
#include "CpkgChunks.h"
#include "CpkgIO.h"
#include "CpkgSink.h"
#include "CpkgTestUtil.h"

#include <cstring>
#include <iterator>
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
    std::vector<uint8_t> WriteSingleChunkFile(const char* tempName, uint32_t fourcc,
                                              uint16_t version, WriteChunkFn writeChunk,
                                              uint64_t* outDataOffset, uint32_t* outSizeRaw)
    {
        TempFile tmp(tempName);
        CCpkgSink sink;
        EXPECT_EQ(CCpkgSink::CreateFile(tmp.str().c_str(), &sink), Gem::Result::Success);
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

    template <typename T> // PackageFloat4 or PackageQuat (both expose V[4])
    void ExpectVec4Eq(const T& actual, const T& expected)
    {
        for (int i = 0; i < 4; ++i)
            EXPECT_EQ(actual.V[i], expected.V[i]);
    }

    void ExpectVec3Eq(const PackageFloat3& actual, const PackageFloat3& expected)
    {
        for (int i = 0; i < 3; ++i)
            EXPECT_EQ(actual.V[i], expected.V[i]);
    }

    void ExpectVec2Eq(const PackageFloat2& actual, const PackageFloat2& expected)
    {
        for (int i = 0; i < 2; ++i)
            EXPECT_EQ(actual.V[i], expected.V[i]);
    }

    void ExpectMatrixEq(const PackageMatrix4x4& actual, const PackageMatrix4x4& expected)
    {
        for (int i = 0; i < 16; ++i)
            EXPECT_EQ(actual.M[i], expected.M[i]);
    }

    PackageMatrix4x4 Identity4x4()
    {
        PackageMatrix4x4 m{};
        m.M[0] = m.M[5] = m.M[10] = m.M[15] = 1.0f;
        return m;
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
        "canvas_cpkg_chunk_node.cpkg", CPKG_FOURCC_NODE, CPKG_NODE_CHUNK_VERSION,
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

    TempFile tmp("canvas_cpkg_chunk_node_reject.cpkg");
    CCpkgSink sink;
    ASSERT_EQ(CCpkgSink::CreateFile(tmp.str().c_str(), &sink), Gem::Result::Success);
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
        "canvas_cpkg_chunk_node_trunc.cpkg", CPKG_FOURCC_NODE, CPKG_NODE_CHUNK_VERSION,
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
    mesh.Bounds.Min = { -1.0f, -2.0f, -3.0f };
    mesh.Bounds.Max = { 4.0f, 5.0f, 6.0f };
    mesh.Parts.push_back(MakePart(2, 6, true, true, true));
    mesh.Skin.HasSkin = true;
    mesh.Skin.BoneNodeIndices = { 1, 2 };
    mesh.Skin.InvBindPoses.resize(2, Identity4x4());
    original.Meshes.push_back(mesh);

    uint64_t dataOffset = 0;
    uint32_t sizeRaw    = 0;
    std::vector<uint8_t> file = WriteSingleChunkFile(
        "canvas_cpkg_chunk_mesh_full.cpkg", CPKG_FOURCC_MESH, CPKG_MESH_CHUNK_VERSION,
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
    ExpectVec3Eq(a.Bounds.Min, mesh.Bounds.Min);
    ExpectVec3Eq(a.Bounds.Max, mesh.Bounds.Max);
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
    mesh.Bounds.Min = { 0.0f, 0.0f, 0.0f };
    mesh.Bounds.Max = { 1.0f, 1.0f, 1.0f };
    mesh.Parts.push_back(MakePart(-1, 3, false, false, false));
    original.Meshes.push_back(mesh);

    uint64_t dataOffset = 0;
    std::vector<uint8_t> file = WriteSingleChunkFile(
        "canvas_cpkg_chunk_mesh_min.cpkg", CPKG_FOURCC_MESH, CPKG_MESH_CHUNK_VERSION,
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
    mesh.Bounds.Min = { -2.0f, -2.0f, -2.0f };
    mesh.Bounds.Max = { 2.0f, 2.0f, 2.0f };
    mesh.Parts.push_back(MakePart(0, 4, false, false, true));
    mesh.Skin.HasSkin = true;
    mesh.Skin.BoneNodeIndices = { 7, 11, 13 };
    for (int bone = 0; bone < 3; ++bone)
    {
        PackageMatrix4x4 pose;
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                pose.M[r * 4 + c] = static_cast<float>(bone * 100 + r * 10 + c);
        mesh.Skin.InvBindPoses.push_back(pose);
    }
    original.Meshes.push_back(mesh);

    uint64_t dataOffset = 0;
    std::vector<uint8_t> file = WriteSingleChunkFile(
        "canvas_cpkg_chunk_mesh_skin.cpkg", CPKG_FOURCC_MESH, CPKG_MESH_CHUNK_VERSION,
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
    mesh.Bounds.Min = { 0.0f, 0.0f, 0.0f };
    mesh.Bounds.Max = { 1.0f, 1.0f, 1.0f };
    mesh.Parts.push_back(MakePart(0, 5, true, true, true));
    mesh.Parts.push_back(MakePart(1, 3, true, false, false));
    mesh.Skin.HasSkin = true;
    mesh.Skin.BoneNodeIndices = { 0 };
    mesh.Skin.InvBindPoses.resize(1, Identity4x4());
    original.Meshes.push_back(mesh);

    uint64_t dataOffset = 0;
    uint32_t sizeRaw    = 0;
    std::vector<uint8_t> file = WriteSingleChunkFile(
        "canvas_cpkg_chunk_mesh_align.cpkg", CPKG_FOURCC_MESH, CPKG_MESH_CHUNK_VERSION,
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
        EXPECT_EQ(part.Positions.Size, src.Positions.size() * sizeof(PackageFloat4));
        EXPECT_EQ(std::memcmp(file.data() + part.Positions.Offset, src.Positions.data(),
                              static_cast<size_t>(part.Positions.Size)), 0);
        EXPECT_EQ(part.UV0.Size, src.UV0.size() * sizeof(PackageFloat2));
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

    TempFile tmp("canvas_cpkg_chunk_mesh_reject.cpkg");
    CCpkgSink sink;
    ASSERT_EQ(CCpkgSink::CreateFile(tmp.str().c_str(), &sink), Gem::Result::Success);
    ExpectCpkgError(Gem::Result::InvalidArg, [&] { WriteMeshChunk(sink, data); });
}

TEST(CpkgChunkTest, ReadMeshChunkRejectsUnknownStreamFlags)
{
    PackageData original;
    PackageMesh mesh;
    mesh.Name   = "Flags";
    mesh.Bounds.Min = { 0.0f, 0.0f, 0.0f };
    mesh.Bounds.Max = { 1.0f, 1.0f, 1.0f };
    mesh.Parts.push_back(MakePart(0, 3, false, false, false));
    original.Meshes.push_back(mesh);

    uint64_t dataOffset = 0;
    std::vector<uint8_t> file = WriteSingleChunkFile(
        "canvas_cpkg_chunk_mesh_flags.cpkg", CPKG_FOURCC_MESH, CPKG_MESH_CHUNK_VERSION,
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

//--------------------------------------------------------------------------------------------------
// MATL
//--------------------------------------------------------------------------------------------------

// Two materials: one with all six texture slots bound to valid indices, one fully unbound (-1).
TEST(CpkgChunkTest, MatlRoundTrip)
{
    PackageData original;

    PackageMaterial bound;
    bound.Name                         = "Bound";
    bound.BaseColorFactor              = { 0.1f, 0.2f, 0.3f, 0.4f };
    bound.EmissiveFactor               = { 0.5f, 0.6f, 0.7f, 0.0f };
    bound.RoughMetalAOFactor           = { 0.25f, 0.75f, 0.5f, 0.0f };
    bound.AlbedoTextureIndex           = 0;
    bound.NormalTextureIndex           = 1;
    bound.EmissiveTextureIndex         = 2;
    bound.RoughnessTextureIndex        = 3;
    bound.MetallicTextureIndex         = 4;
    bound.AmbientOcclusionTextureIndex = 5;
    original.Materials.push_back(bound);

    PackageMaterial unbound; // every texture slot -1, non-default factors
    unbound.Name               = "Unbound";
    unbound.BaseColorFactor    = { 1.0f, 0.0f, 1.0f, 1.0f };
    unbound.EmissiveFactor     = { 0.0f, 0.0f, 0.0f, 0.0f };
    unbound.RoughMetalAOFactor = { 0.9f, 0.1f, 1.0f, 0.0f };
    original.Materials.push_back(unbound);

    uint64_t dataOffset = 0;
    uint32_t sizeRaw    = 0;
    std::vector<uint8_t> file = WriteSingleChunkFile(
        "canvas_cpkg_chunk_matl.cpkg", CPKG_FOURCC_MATL, CPKG_MATL_CHUNK_VERSION,
        [&](CCpkgSink& sink) { WriteMatlChunk(sink, original); },
        &dataOffset, &sizeRaw);

    CCpkgReader reader(file.data(), file.size());
    reader.SetOffset(static_cast<size_t>(dataOffset));
    PackageData readBack;
    ASSERT_EQ(ReadMatlChunk(reader, &readBack), Gem::Result::Success);
    EXPECT_EQ(reader.GetOffset(), static_cast<size_t>(dataOffset) + sizeRaw);

    ASSERT_EQ(readBack.Materials.size(), original.Materials.size());
    for (size_t i = 0; i < original.Materials.size(); ++i)
    {
        const PackageMaterial& e = original.Materials[i];
        const PackageMaterial& a = readBack.Materials[i];
        EXPECT_EQ(a.Name, e.Name);
        ExpectVec4Eq(a.BaseColorFactor, e.BaseColorFactor);
        ExpectVec4Eq(a.EmissiveFactor, e.EmissiveFactor);
        ExpectVec4Eq(a.RoughMetalAOFactor, e.RoughMetalAOFactor);
        EXPECT_EQ(a.AlbedoTextureIndex, e.AlbedoTextureIndex);
        EXPECT_EQ(a.NormalTextureIndex, e.NormalTextureIndex);
        EXPECT_EQ(a.EmissiveTextureIndex, e.EmissiveTextureIndex);
        EXPECT_EQ(a.RoughnessTextureIndex, e.RoughnessTextureIndex);
        EXPECT_EQ(a.MetallicTextureIndex, e.MetallicTextureIndex);
        EXPECT_EQ(a.AmbientOcclusionTextureIndex, e.AmbientOcclusionTextureIndex);
    }
}

//--------------------------------------------------------------------------------------------------
// TXTR
//--------------------------------------------------------------------------------------------------

namespace
{
    void ExpectTextureEq(const PackageTexture& a, const PackageTexture& e)
    {
        EXPECT_EQ(a.Name, e.Name);
        EXPECT_EQ(a.Path, e.Path);
        EXPECT_EQ(a.Format, e.Format);
        EXPECT_EQ(a.Dimension, e.Dimension);
        EXPECT_EQ(a.Width, e.Width);
        EXPECT_EQ(a.Height, e.Height);
        EXPECT_EQ(a.Depth, e.Depth);
        EXPECT_EQ(a.ArraySize, e.ArraySize);
        EXPECT_EQ(a.MipCount, e.MipCount);

        ASSERT_EQ(a.Subresources.size(), e.Subresources.size());
        for (size_t s = 0; s < e.Subresources.size(); ++s)
        {
            EXPECT_EQ(a.Subresources[s].Offset, e.Subresources[s].Offset);
            EXPECT_EQ(a.Subresources[s].Size, e.Subresources[s].Size);
            EXPECT_EQ(a.Subresources[s].RowPitch, e.Subresources[s].RowPitch);
        }
        EXPECT_EQ(a.Bytes, e.Bytes);
    }

    // A subresource whose payload range holds recognisable per-slice bytes (value = base + slice).
    PackageSubresource AppendSlice(std::vector<uint8_t>& bytes, uint32_t size, uint32_t rowPitch,
                                   uint8_t fill)
    {
        PackageSubresource sub;
        sub.Offset   = bytes.size();
        sub.Size     = size;
        sub.RowPitch = rowPitch;
        bytes.insert(bytes.end(), size, fill);
        return sub;
    }
}

// Four entries exercising every payload case: external, encoded source, embedded mip-mapped 2D, and
// embedded cube map with per-(face, mip) subresources.
TEST(CpkgChunkTest, TxtrRoundTrip)
{
    PackageData original;

    // Entry A: named external texture - no payload, no subresources.
    PackageTexture external;
    external.Name      = "sky_px";
    external.Path      = "textures/sky_px.dds";
    external.Format    = GfxFormat::Unknown;
    external.Dimension = GfxSurfaceDimension::Dimension2D;
    original.Textures.push_back(external);

    // Entry B: embedded encoded-source image - one subresource covering the whole blob, RowPitch 0.
    PackageTexture encoded;
    encoded.Format    = GfxFormat::Unknown;
    encoded.Dimension = GfxSurfaceDimension::Dimension2D;
    encoded.Bytes.assign(16, 0xAB);
    {
        PackageSubresource sub;
        sub.Offset   = 0;
        sub.Size     = 16;
        sub.RowPitch = 0;
        encoded.Subresources.push_back(sub);
    }
    original.Textures.push_back(encoded);

    // Entry C: embedded mip-mapped 2D - 10 mips, distinct Offset/Size/RowPitch and per-mip bytes.
    PackageTexture mipped;
    mipped.Format    = GfxFormat::BC7_UNorm_SRGB;
    mipped.Dimension = GfxSurfaceDimension::Dimension2D;
    mipped.Width     = 512;
    mipped.Height    = 512;
    mipped.ArraySize = 1;
    mipped.MipCount  = 10;
    for (uint32_t mip = 0; mip < mipped.MipCount; ++mip)
    {
        const uint32_t size     = 32u * (mipped.MipCount - mip); // distinct, decreasing sizes
        const uint32_t rowPitch = 16u * (mipped.MipCount - mip);
        mipped.Subresources.push_back(
            AppendSlice(mipped.Bytes, size, rowPitch, static_cast<uint8_t>(0x10 + mip)));
    }
    original.Textures.push_back(mipped);

    // Entry D: embedded cube map - 6 faces * 3 mips = 18 subresources, D3D order mip + face*MipCount.
    PackageTexture cube;
    cube.Format    = GfxFormat::BC7_UNorm_SRGB;
    cube.Dimension = GfxSurfaceDimension::DimensionCube;
    cube.Width     = 64;
    cube.Height    = 64;
    cube.ArraySize = 6;
    cube.MipCount  = 3;
    cube.Subresources.resize(static_cast<size_t>(cube.ArraySize) * cube.MipCount);
    for (uint32_t face = 0; face < cube.ArraySize; ++face)
        for (uint32_t mip = 0; mip < cube.MipCount; ++mip)
        {
            const uint32_t size     = 48u * (cube.MipCount - mip);
            const uint8_t  fill      = static_cast<uint8_t>(0x40 + face * cube.MipCount + mip);
            cube.Subresources[mip + face * cube.MipCount] =
                AppendSlice(cube.Bytes, size, 8u * (cube.MipCount - mip), fill);
        }
    original.Textures.push_back(cube);

    uint64_t dataOffset = 0;
    uint32_t sizeRaw    = 0;
    std::vector<uint8_t> file = WriteSingleChunkFile(
        "canvas_cpkg_chunk_txtr.cpkg", CPKG_FOURCC_TXTR, CPKG_TXTR_CHUNK_VERSION,
        [&](CCpkgSink& sink) { WriteTxtrChunk(sink, original); },
        &dataOffset, &sizeRaw);

    CCpkgReader reader(file.data(), file.size());
    reader.SetOffset(static_cast<size_t>(dataOffset));
    PackageData readBack;
    ASSERT_EQ(ReadTxtrChunk(reader, &readBack), Gem::Result::Success);
    EXPECT_EQ(reader.GetOffset(), static_cast<size_t>(dataOffset) + sizeRaw);

    ASSERT_EQ(readBack.Textures.size(), original.Textures.size());
    for (size_t i = 0; i < original.Textures.size(); ++i)
        ExpectTextureEq(readBack.Textures[i], original.Textures[i]);

    // Spot-check that specific cube (face, mip) slices survived, not just the aggregate compare.
    const PackageTexture& c = readBack.Textures[3];
    const PackageSubresource& face4mip1 = c.Subresources[1 + 4 * 3];
    EXPECT_EQ(c.Bytes[static_cast<size_t>(face4mip1.Offset)],
              static_cast<uint8_t>(0x40 + 4 * 3 + 1));
}

// A subresource whose range runs past the payload is rejected on write.
TEST(CpkgChunkTest, WriteTxtrChunkRejectsSubresourceOutsidePayload)
{
    PackageData data;
    PackageTexture tex;
    tex.Format = GfxFormat::Unknown;
    tex.Bytes.assign(8, 0x00);
    PackageSubresource sub;
    sub.Offset = 4;
    sub.Size   = 8; // [4, 12) exceeds the 8-byte payload
    tex.Subresources.push_back(sub);
    data.Textures.push_back(tex);

    TempFile tmp("canvas_cpkg_chunk_txtr_reject.cpkg");
    CCpkgSink sink;
    ASSERT_EQ(CCpkgSink::CreateFile(tmp.str().c_str(), &sink), Gem::Result::Success);
    ExpectCpkgError(Gem::Result::InvalidArg, [&] { WriteTxtrChunk(sink, data); });
}

//--------------------------------------------------------------------------------------------------
// LITE
//--------------------------------------------------------------------------------------------------

// One light of each LightType; the spot light's inner/outer cone angles must survive.
TEST(CpkgChunkTest, LiteRoundTrip)
{
    PackageData original;

    const LightType types[] = {
        LightType::Ambient, LightType::Directional, LightType::Point, LightType::Spot,
    };
    for (size_t i = 0; i < std::size(types); ++i)
    {
        const float f           = static_cast<float>(i);
        PackageLight light;
        light.Name              = "Light" + std::to_string(i);
        light.Type              = types[i];
        light.Color             = { 0.1f * f, 0.2f * f, 0.3f * f, 1.0f };
        light.Intensity         = 2.0f + f;
        light.Range             = 10.0f * f;
        light.AttenuationConst  = 1.0f;
        light.AttenuationLinear = 0.1f * f;
        light.AttenuationQuad   = 0.01f * f;
        light.SpotInnerAngle    = 0.3f + 0.1f * f;
        light.SpotOuterAngle    = 0.6f + 0.1f * f;
        original.Lights.push_back(light);
    }

    uint64_t dataOffset = 0;
    uint32_t sizeRaw    = 0;
    std::vector<uint8_t> file = WriteSingleChunkFile(
        "canvas_cpkg_chunk_lite.cpkg", CPKG_FOURCC_LITE, CPKG_LITE_CHUNK_VERSION,
        [&](CCpkgSink& sink) { WriteLiteChunk(sink, original); },
        &dataOffset, &sizeRaw);

    CCpkgReader reader(file.data(), file.size());
    reader.SetOffset(static_cast<size_t>(dataOffset));
    PackageData readBack;
    ASSERT_EQ(ReadLiteChunk(reader, &readBack), Gem::Result::Success);
    EXPECT_EQ(reader.GetOffset(), static_cast<size_t>(dataOffset) + sizeRaw);

    ASSERT_EQ(readBack.Lights.size(), original.Lights.size());
    for (size_t i = 0; i < original.Lights.size(); ++i)
    {
        const PackageLight& e = original.Lights[i];
        const PackageLight& a = readBack.Lights[i];
        EXPECT_EQ(a.Name, e.Name);
        EXPECT_EQ(a.Type, e.Type);
        ExpectVec4Eq(a.Color, e.Color);
        EXPECT_EQ(a.Intensity, e.Intensity);
        EXPECT_EQ(a.Range, e.Range);
        EXPECT_EQ(a.AttenuationConst, e.AttenuationConst);
        EXPECT_EQ(a.AttenuationLinear, e.AttenuationLinear);
        EXPECT_EQ(a.AttenuationQuad, e.AttenuationQuad);
        EXPECT_EQ(a.SpotInnerAngle, e.SpotInnerAngle);
        EXPECT_EQ(a.SpotOuterAngle, e.SpotOuterAngle);
    }
}

//--------------------------------------------------------------------------------------------------
// CAMR
//--------------------------------------------------------------------------------------------------

// Two cameras with distinct FOV / aspect / clip planes.
TEST(CpkgChunkTest, CamrRoundTrip)
{
    PackageData original;

    PackageCamera camA;
    camA.Name        = "CamA";
    camA.NearZ       = 0.05f;
    camA.FarZ        = 500.0f;
    camA.FovY        = 1.0472f; // 60 degrees
    camA.AspectRatio = 16.0f / 9.0f;
    original.Cameras.push_back(camA);

    PackageCamera camB;
    camB.Name        = "CamB";
    camB.NearZ       = 0.5f;
    camB.FarZ        = 2000.0f;
    camB.FovY        = 0.7854f; // 45 degrees
    camB.AspectRatio = 4.0f / 3.0f;
    original.Cameras.push_back(camB);

    uint64_t dataOffset = 0;
    uint32_t sizeRaw    = 0;
    std::vector<uint8_t> file = WriteSingleChunkFile(
        "canvas_cpkg_chunk_camr.cpkg", CPKG_FOURCC_CAMR, CPKG_CAMR_CHUNK_VERSION,
        [&](CCpkgSink& sink) { WriteCamrChunk(sink, original); },
        &dataOffset, &sizeRaw);

    CCpkgReader reader(file.data(), file.size());
    reader.SetOffset(static_cast<size_t>(dataOffset));
    PackageData readBack;
    ASSERT_EQ(ReadCamrChunk(reader, &readBack), Gem::Result::Success);
    EXPECT_EQ(reader.GetOffset(), static_cast<size_t>(dataOffset) + sizeRaw);

    ASSERT_EQ(readBack.Cameras.size(), original.Cameras.size());
    for (size_t i = 0; i < original.Cameras.size(); ++i)
    {
        const PackageCamera& e = original.Cameras[i];
        const PackageCamera& a = readBack.Cameras[i];
        EXPECT_EQ(a.Name, e.Name);
        EXPECT_EQ(a.NearZ, e.NearZ);
        EXPECT_EQ(a.FarZ, e.FarZ);
        EXPECT_EQ(a.FovY, e.FovY);
        EXPECT_EQ(a.AspectRatio, e.AspectRatio);
    }
}

//--------------------------------------------------------------------------------------------------
// ANIM
//--------------------------------------------------------------------------------------------------

namespace
{
    // A track for one node with recognisable per-keyframe TRS values seeded from the node and
    // keyframe indices.
    PackageAnimTrack MakeTrack(int32_t nodeIndex, uint32_t keyframeCount)
    {
        PackageAnimTrack track;
        track.NodeIndex = nodeIndex;
        for (uint32_t k = 0; k < keyframeCount; ++k)
        {
            const float f = static_cast<float>(nodeIndex * 100 + k);
            PackageAnimKeyframe key;
            key.Time        = 0.25f * static_cast<float>(k);
            key.Translation = { f + 0.1f, f + 0.2f, f + 0.3f, 0.0f };
            key.Rotation    = { f + 0.4f, f + 0.5f, f + 0.6f, f + 0.7f };
            key.Scale       = { f + 0.8f, f + 0.9f, f + 1.0f, 0.0f };
            track.Keyframes.push_back(key);
        }
        return track;
    }

    void ExpectTrackEq(const PackageAnimTrack& a, const PackageAnimTrack& e)
    {
        EXPECT_EQ(a.NodeIndex, e.NodeIndex);
        ASSERT_EQ(a.Keyframes.size(), e.Keyframes.size());
        for (size_t k = 0; k < e.Keyframes.size(); ++k)
        {
            EXPECT_EQ(a.Keyframes[k].Time, e.Keyframes[k].Time);
            ExpectVec4Eq(a.Keyframes[k].Translation, e.Keyframes[k].Translation);
            ExpectVec4Eq(a.Keyframes[k].Rotation, e.Keyframes[k].Rotation);
            ExpectVec4Eq(a.Keyframes[k].Scale, e.Keyframes[k].Scale);
        }
    }

    void ExpectClipsEq(const std::vector<PackageAnimClip>& actual,
                       const std::vector<PackageAnimClip>& expected)
    {
        ASSERT_EQ(actual.size(), expected.size());
        for (size_t c = 0; c < expected.size(); ++c)
        {
            const PackageAnimClip& e = expected[c];
            const PackageAnimClip& a = actual[c];
            EXPECT_EQ(a.Name, e.Name);
            EXPECT_EQ(a.DurationSeconds, e.DurationSeconds);
            ASSERT_EQ(a.Tracks.size(), e.Tracks.size());
            for (size_t t = 0; t < e.Tracks.size(); ++t)
                ExpectTrackEq(a.Tracks[t], e.Tracks[t]);
        }
    }

    std::vector<PackageAnimClip> RoundTripAnim(const char* tempName, const PackageData& original)
    {
        uint64_t dataOffset = 0;
        uint32_t sizeRaw    = 0;
        std::vector<uint8_t> file = WriteSingleChunkFile(
            tempName, CPKG_FOURCC_ANIM, CPKG_ANIM_CHUNK_VERSION,
            [&](CCpkgSink& sink) { WriteAnimChunk(sink, original); },
            &dataOffset, &sizeRaw);

        CCpkgReader reader(file.data(), file.size());
        reader.SetOffset(static_cast<size_t>(dataOffset));
        PackageData readBack;
        EXPECT_EQ(ReadAnimChunk(reader, &readBack), Gem::Result::Success);
        // The cursor must land exactly on the chunk end.
        EXPECT_EQ(reader.GetOffset(), static_cast<size_t>(dataOffset) + sizeRaw);
        return readBack.AnimClips;
    }
}

// One clip, two tracks, five keyframes each; every TRS field must survive.
TEST(CpkgChunkTest, AnimRoundTripBasic)
{
    PackageData original;
    PackageAnimClip clip;
    clip.Name            = "Walk";
    clip.DurationSeconds = 1.0f;
    clip.Tracks.push_back(MakeTrack(0, 5));
    clip.Tracks.push_back(MakeTrack(3, 5));
    original.AnimClips.push_back(clip);

    ExpectClipsEq(RoundTripAnim("canvas_cpkg_chunk_anim_basic.cpkg", original), original.AnimClips);
}

// Three clips with 0, 1, and 4 tracks; clip names, durations, and per-track keyframes must match.
TEST(CpkgChunkTest, AnimRoundTripMultiClip)
{
    PackageData original;

    PackageAnimClip empty;
    empty.Name            = "Idle";
    empty.DurationSeconds = 0.5f;
    original.AnimClips.push_back(empty);

    PackageAnimClip single;
    single.Name            = "Jump";
    single.DurationSeconds = 2.25f;
    single.Tracks.push_back(MakeTrack(1, 3));
    original.AnimClips.push_back(single);

    PackageAnimClip many;
    many.Name            = "Run";
    many.DurationSeconds = 3.75f;
    for (int32_t node = 0; node < 4; ++node)
        many.Tracks.push_back(MakeTrack(node, static_cast<uint32_t>(node) + 1));
    original.AnimClips.push_back(many);

    ExpectClipsEq(RoundTripAnim("canvas_cpkg_chunk_anim_multi.cpkg", original), original.AnimClips);
}

// Zero clips: ReadAnimChunk must produce an empty AnimClips vector.
TEST(CpkgChunkTest, AnimRoundTripEmpty)
{
    PackageData original; // no clips

    std::vector<PackageAnimClip> clips =
        RoundTripAnim("canvas_cpkg_chunk_anim_empty.cpkg", original);
    EXPECT_TRUE(clips.empty());
}

} // namespace CanvasUnitTest
