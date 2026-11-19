//================================================================================================
// CanvasPkgTest - Tests for the CanvasPkg format declarations and payload helpers
//================================================================================================

#include "pch.h"
#include "CanvasPkg.h"

#include <array>

namespace
{
    // A payload buffer aligned to kCpkgPayloadAlignment, as ReadAsset fills one.
    struct alignas(kCpkgPayloadAlignment) AlignedBuffer
    {
        std::byte Bytes[256] = {};

        std::span<const std::byte> First(size_t size) const { return { Bytes, size }; }
    };

    struct TestHeader
    {
        CpkgSpan<float>    Floats;
        CpkgSpan<uint64_t> Wide;
    };

    // The on-disk check rejects implicit padding and unlisted members.
    struct Padded
    {
        uint32_t Narrow;
        uint64_t Wide;
    };
    struct ExplicitlyPadded
    {
        uint32_t Narrow;
        uint32_t Padding = 0;
        uint64_t Wide;
    };
    static_assert(!CpkgDetail::IsOnDiskStruct<Padded>(&Padded::Narrow, &Padded::Wide));
    static_assert(CpkgDetail::IsOnDiskStruct<ExplicitlyPadded>(&ExplicitlyPadded::Narrow, &ExplicitlyPadded::Padding, &ExplicitlyPadded::Wide));
    static_assert(!CpkgDetail::IsOnDiskStruct<ExplicitlyPadded>(&ExplicitlyPadded::Narrow, &ExplicitlyPadded::Wide));
}

//------------------------------------------------------------------------------------------------
TEST(CanvasPkg, SharedAssetTypes)
{
    const AssetType shared[] = {
        AssetType::Image, AssetType::Material, AssetType::MeshData, AssetType::MeshSkin,
        AssetType::Skeleton, AssetType::SkeletalAnimation, AssetType::Skybox,
    };
    const AssetType local[] = {
        AssetType::TransformNode, AssetType::Placement, AssetType::ImportNode,
        AssetType::SkeletonInstance, AssetType::MeshInstance, AssetType::MeshField,
        AssetType::Camera, AssetType::Light, AssetType::AnimationClip, AssetType::ExportList,
        AssetType::WorldSettings,
    };

    for (AssetType type : shared)
        EXPECT_TRUE(CpkgIsSharedAssetType(type)) << uint32_t(type);
    for (AssetType type : local)
        EXPECT_FALSE(CpkgIsSharedAssetType(type)) << uint32_t(type);
    EXPECT_FALSE(CpkgIsSharedAssetType(AssetType::Unknown));
}

//------------------------------------------------------------------------------------------------
TEST(CanvasPkg, DefaultsZeroEveryPadding)
{
    // Padding members default to zero, so a struct built from its defaults writes zero bytes there.
    CpkgImportNodeData importNode;
    CpkgMaterialData material{};
    CpkgImageData image{};
    CpkgSkyboxData skybox{};
    EXPECT_EQ(importNode.Padding, 0u);
    EXPECT_EQ(material.Padding, 0u);
    EXPECT_EQ(image.Padding, 0u);
    EXPECT_EQ(skybox.Padding, 0u);

    // Defaults match the package source's defaults.
    EXPECT_EQ(material.BaseColorFactor[3], 1.0f);
    EXPECT_EQ(material.RoughMetalAOFactor[0], 1.0f);
    EXPECT_EQ(material.RoughMetalAOFactor[1], 0.0f);
    EXPECT_EQ(material.RoughMetalAOFactor[2], 1.0f);
    EXPECT_EQ(skybox.Orientation[3], 1.0f);
    EXPECT_EQ(CpkgWorldSettingsData{}.BackgroundColor[3], 1.0f);
    EXPECT_EQ(CpkgMeshData{}.Topology, GfxPrimitiveTopology::TriangleList);

    // The empty bounds sentinel.
    CpkgAABB bounds;
    for (int i = 0; i < 3; ++i)
    {
        EXPECT_EQ(bounds.Min[i], FLT_MAX);
        EXPECT_EQ(bounds.Max[i], -FLT_MAX);
    }
}

//------------------------------------------------------------------------------------------------
TEST(CanvasPkg, PayloadBuilderLaysOutArraysAfterHeader)
{
    CCpkgPayloadBuilder<TestHeader> builder;
    EXPECT_TRUE(builder.GetPayload().empty());

    TestHeader header;
    const float floats[] = { 1.0f, 2.0f, 3.0f };
    const uint64_t wide[] = { 0x1122334455667788ull, 42 };
    ASSERT_EQ(builder.Append(std::span(floats), &header.Floats), Result::Success);
    ASSERT_EQ(builder.Append(std::span(wide), &header.Wide), Result::Success);
    ASSERT_EQ(builder.SetHeader(header), Result::Success);

    // Floats follow the header; the uint64_t array is padded up to 8-byte alignment.
    EXPECT_EQ(header.Floats.Offset, sizeof(TestHeader));
    EXPECT_EQ(header.Floats.Count, 3u);
    EXPECT_EQ(header.Wide.Offset, sizeof(TestHeader) + 16);
    EXPECT_EQ(header.Wide.Count, 2u);

    std::span<const std::byte> payload = builder.GetPayload();
    ASSERT_EQ(payload.size(), sizeof(TestHeader) + 16 + sizeof(wide));

    // The alignment gap is zero.
    for (size_t i = sizeof(TestHeader) + sizeof(floats); i < header.Wide.Offset; ++i)
        EXPECT_EQ(payload[i], std::byte{ 0 });

    const TestHeader *pHeader = nullptr;
    ASSERT_EQ(CpkgGetPayloadHeader(payload, &pHeader), Result::Success);

    std::span<const float> resolvedFloats;
    std::span<const uint64_t> resolvedWide;
    ASSERT_EQ(CpkgResolveSpan<TestHeader>(payload, pHeader->Floats, &resolvedFloats), Result::Success);
    ASSERT_EQ(CpkgResolveSpan<TestHeader>(payload, pHeader->Wide, &resolvedWide), Result::Success);
    EXPECT_TRUE(std::equal(resolvedFloats.begin(), resolvedFloats.end(), std::begin(floats), std::end(floats)));
    EXPECT_TRUE(std::equal(resolvedWide.begin(), resolvedWide.end(), std::begin(wide), std::end(wide)));
}

//------------------------------------------------------------------------------------------------
TEST(CanvasPkg, PayloadBuilderReservesHeaderOnFirstCall)
{
    CCpkgPayloadBuilder<CpkgCameraData> builder;
    EXPECT_TRUE(builder.GetPayload().empty());

    CpkgCameraData camera;
    camera.NodeIndex = 3;
    ASSERT_EQ(builder.SetHeader(camera), Result::Success);
    ASSERT_EQ(builder.GetPayload().size(), sizeof(CpkgCameraData));

    const CpkgCameraData *pCamera = nullptr;
    ASSERT_EQ(CpkgGetPayloadHeader(builder.GetPayload(), &pCamera), Result::Success);
    EXPECT_EQ(pCamera->NodeIndex, 3);
}

//------------------------------------------------------------------------------------------------
TEST(CanvasPkg, PayloadBuilderEmptyAppendWritesEmptySpan)
{
    CCpkgPayloadBuilder<TestHeader> builder;
    CpkgSpan<float> span{ 123, 456 };
    ASSERT_EQ(builder.Append(std::span<const float>(), &span), Result::Success);
    EXPECT_EQ(span.Offset, 0u);
    EXPECT_EQ(span.Count, 0u);
    EXPECT_EQ(builder.GetPayload().size(), sizeof(TestHeader));

    EXPECT_EQ(builder.Append(std::span<const float>(), static_cast<CpkgSpan<float> *>(nullptr)), Result::InvalidArg);
}

//------------------------------------------------------------------------------------------------
TEST(CanvasPkg, PayloadBuilderNestedSpans)
{
    // A mesh with two groups: each group's streams, then the group array.
    const CpkgVertexPosition positions0[] = { {{0, 0, 0}}, {{1, 0, 0}}, {{1, 1, 0}} };
    const CpkgVertexNormal normals0[] = { {{0, 0, 1}}, {{0, 0, 1}}, {{0, 0, 1}} };
    const CpkgVertexPosition positions1[] = { {{0, 0, 2}}, {{1, 0, 2}}, {{1, 1, 2}}, {{0, 1, 2}} };
    const CpkgVertexNormal normals1[] = { {{0, 0, 1}}, {{0, 0, 1}}, {{0, 0, 1}}, {{0, 0, 1}} };
    const char material[] = { 'W', 'o', 'o', 'd' };

    CCpkgPayloadBuilder<CpkgMeshData> builder;
    std::array<CpkgMeshDataGroup, 2> groups;
    ASSERT_EQ(builder.Append(std::span(material), &groups[0].Material), Result::Success);
    ASSERT_EQ(builder.Append(std::span(positions0), &groups[0].Positions), Result::Success);
    ASSERT_EQ(builder.Append(std::span(normals0), &groups[0].Normals), Result::Success);
    ASSERT_EQ(builder.Append(std::span(positions1), &groups[1].Positions), Result::Success);
    ASSERT_EQ(builder.Append(std::span(normals1), &groups[1].Normals), Result::Success);

    CpkgMeshData mesh{};
    mesh.Topology = GfxPrimitiveTopology::TriangleList;
    ASSERT_EQ(builder.Append(std::span(groups), &mesh.Groups), Result::Success);
    ASSERT_EQ(builder.SetHeader(mesh), Result::Success);

    std::span<const std::byte> payload = builder.GetPayload();
    const CpkgMeshData *pMesh = nullptr;
    ASSERT_EQ(CpkgGetPayloadHeader(payload, &pMesh), Result::Success);
    EXPECT_EQ(pMesh->Topology, GfxPrimitiveTopology::TriangleList);
    EXPECT_EQ(pMesh->LocalBounds.Min[0], FLT_MAX);

    std::span<const CpkgMeshDataGroup> resolvedGroups;
    ASSERT_EQ(CpkgResolveSpan<CpkgMeshData>(payload, pMesh->Groups, &resolvedGroups), Result::Success);
    ASSERT_EQ(resolvedGroups.size(), 2u);

    std::span<const char> resolvedMaterial;
    ASSERT_EQ(CpkgResolveSpan<CpkgMeshData>(payload, resolvedGroups[0].Material, &resolvedMaterial), Result::Success);
    EXPECT_EQ(std::string_view(resolvedMaterial.data(), resolvedMaterial.size()), "Wood");

    std::span<const char> noMaterial;
    ASSERT_EQ(CpkgResolveSpan<CpkgMeshData>(payload, resolvedGroups[1].Material, &noMaterial), Result::Success);
    EXPECT_TRUE(noMaterial.empty());

    std::span<const CpkgVertexPosition> resolvedPositions;
    ASSERT_EQ(CpkgResolveSpan<CpkgMeshData>(payload, resolvedGroups[1].Positions, &resolvedPositions), Result::Success);
    ASSERT_EQ(resolvedPositions.size(), 4u);
    EXPECT_EQ(resolvedPositions[3].XYZ[1], 1.0f);
    EXPECT_EQ(resolvedPositions[3].XYZ[2], 2.0f);
}

//------------------------------------------------------------------------------------------------
TEST(CanvasPkg, GetPayloadHeaderChecksBuffer)
{
    AlignedBuffer buffer;
    const TestHeader *pHeader = nullptr;

    EXPECT_EQ(CpkgGetPayloadHeader(std::span<const std::byte>(), &pHeader), Result::InvalidArg);
    EXPECT_EQ(CpkgGetPayloadHeader(std::span<const std::byte>(buffer.Bytes + 1, 64), &pHeader), Result::InvalidArg);
    EXPECT_EQ(CpkgGetPayloadHeader(buffer.First(sizeof(TestHeader) - 1), &pHeader), Result::CorruptedData);
    EXPECT_EQ(CpkgGetPayloadHeader(buffer.First(sizeof(TestHeader)), static_cast<const TestHeader **>(nullptr)), Result::InvalidArg);

    ASSERT_EQ(CpkgGetPayloadHeader(buffer.First(sizeof(TestHeader)), &pHeader), Result::Success);
    EXPECT_EQ(static_cast<const void *>(pHeader), static_cast<const void *>(buffer.Bytes));
}

//------------------------------------------------------------------------------------------------
TEST(CanvasPkg, ResolveSpanRejectsMalformedSpans)
{
    AlignedBuffer buffer;
    const std::span<const std::byte> payload = buffer.First(sizeof(TestHeader) + 4 * sizeof(float));
    std::span<const float> elements;

    auto resolve = [&](uint64_t offset, uint64_t count)
    {
        return CpkgResolveSpan<TestHeader>(payload, CpkgSpan<float>{ offset, count }, &elements);
    };

    // Valid: the whole data area, a part of it, and an empty span.
    EXPECT_EQ(resolve(sizeof(TestHeader), 4), Result::Success);
    EXPECT_EQ(elements.size(), 4u);
    EXPECT_EQ(resolve(sizeof(TestHeader) + 8, 2), Result::Success);
    EXPECT_EQ(resolve(0, 0), Result::Success);
    EXPECT_TRUE(elements.empty());

    // An empty span has offset 0.
    EXPECT_EQ(resolve(sizeof(TestHeader), 0), Result::CorruptedData);
    // Misaligned.
    EXPECT_EQ(resolve(sizeof(TestHeader) + 2, 1), Result::CorruptedData);
    // Overlaps the header.
    EXPECT_EQ(resolve(sizeof(TestHeader) - 4, 1), Result::CorruptedData);
    // Extends past the end.
    EXPECT_EQ(resolve(sizeof(TestHeader), 5), Result::CorruptedData);
    EXPECT_EQ(resolve(sizeof(TestHeader) + 16, 1), Result::CorruptedData);
    EXPECT_EQ(resolve(payload.size() + 4, 1), Result::CorruptedData);
    // Sizes that overflow when multiplied or added.
    EXPECT_EQ(resolve(sizeof(TestHeader), UINT64_MAX / 2), Result::CorruptedData);
    EXPECT_EQ(resolve(UINT64_MAX - 3, 1), Result::CorruptedData);

    // Buffer checks.
    EXPECT_EQ(CpkgResolveSpan<TestHeader>(std::span<const std::byte>(), CpkgSpan<float>{}, &elements), Result::InvalidArg);
    EXPECT_EQ(CpkgResolveSpan<TestHeader>(std::span<const std::byte>(buffer.Bytes + 4, 64), CpkgSpan<float>{}, &elements), Result::InvalidArg);
    EXPECT_EQ(CpkgResolveSpan<TestHeader>(payload, CpkgSpan<float>{}, static_cast<std::span<const float> *>(nullptr)), Result::InvalidArg);
}
