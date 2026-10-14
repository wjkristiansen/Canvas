//================================================================================================
// CanvasFbxTest -- checks imported FBX transforms against ufbx's own evaluation.
//
// The imported node TRS values are loaded into Canvas scene-graph nodes and every global
// matrix is compared with ufbx's node_to_world. Skin palettes (InvBind * boneGlobal) are
// compared with ufbx's per-cluster geometry_to_world, at rest and mid-animation.
//================================================================================================

#include "pch.h"
#include "CanvasFbx.h"
#include <ufbx.h>
#include <filesystem>

using namespace Canvas;

namespace CanvasUnitTest
{
namespace {

// Assets prefixed "ufbx:" come from ufbx's own test data (used for Maya-exported files).
std::filesystem::path AssetPath(std::string_view name)
{
    if (name.starts_with("ufbx:"))
        return std::filesystem::path(CANVAS_UFBX_DATA_DIR) / name.substr(5);
    return std::filesystem::path(CANVAS_FBX_ASSET_DIR) / name;
}

// Same load options as Canvas::Fbx::ImportScene, so both sides see the same scene:
// Blender files put the unit/axis conversion on the root, everything else converts geometry.
ufbx_scene *LoadReference(const std::filesystem::path &path)
{
    ufbx_load_opts opts = {};
    opts.target_axes = { UFBX_COORDINATE_AXIS_POSITIVE_Y, UFBX_COORDINATE_AXIS_POSITIVE_Z, UFBX_COORDINATE_AXIS_POSITIVE_X };
    opts.target_camera_axes = { UFBX_COORDINATE_AXIS_NEGATIVE_Y, UFBX_COORDINATE_AXIS_POSITIVE_Z, UFBX_COORDINATE_AXIS_NEGATIVE_X };
    opts.target_light_axes = opts.target_camera_axes;
    opts.target_unit_meters = 1.0f;
    opts.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
    opts.geometry_transform_handling = UFBX_GEOMETRY_TRANSFORM_HANDLING_MODIFY_GEOMETRY;
    opts.inherit_mode_handling = UFBX_INHERIT_MODE_HANDLING_HELPER_NODES;
    opts.generate_missing_normals = true;
    ufbx_scene *pScene = ufbx_load_file(path.string().c_str(), &opts, nullptr);
    if (pScene && (pScene->metadata.exporter == UFBX_EXPORTER_BLENDER_BINARY ||
                   pScene->metadata.exporter == UFBX_EXPORTER_BLENDER_ASCII))
    {
        ufbx_free_scene(pScene);
        opts.space_conversion = UFBX_SPACE_CONVERSION_TRANSFORM_ROOT;
        pScene = ufbx_load_file(path.string().c_str(), &opts, nullptr);
    }
    return pScene;
}

constexpr const char *kAllAssets[] = {
    "Blocky.fbx",
    "HAnimation.fbx",
    "WeirdLocomotive.fbx",
    "ufbx:maya_transformed_skin_7700_binary.fbx",
    "ufbx:maya_advanced_skinned_pivot_7700_binary.fbx",
};

constexpr const char *kSkinnedAssets[] = {
    "Blocky.fbx",
    "ufbx:maya_transformed_skin_7700_binary.fbx",
    "ufbx:maya_advanced_skinned_pivot_7700_binary.fbx",
};

// ufbx matrices act on column vectors; transpose the 3x4 into Canvas row-vector form.
Math::FloatMatrix4x4 ToRowMatrix(const ufbx_matrix &m)
{
    Math::FloatMatrix4x4 r = Math::FloatMatrix4x4::Identity();
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 3; ++row)
            r[col][row] = static_cast<float>(m.cols[col].v[row]);
    return r;
}

// Largest element difference, relative to the larger matrix magnitude.
float RelativeDiff(const Math::FloatMatrix4x4 &a, const Math::FloatMatrix4x4 &b)
{
    float diff = 0.0f, mag = 1.0f;
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col)
        {
            diff = std::max(diff, std::abs(a[row][col] - b[row][col]));
            mag  = std::max({ mag, std::abs(a[row][col]), std::abs(b[row][col]) });
        }
    return diff / mag;
}

constexpr float kTolerance = 1e-4f;

struct ImportedFixture
{
    Fbx::ImportedScene                             Scene;
    ufbx_scene                                    *pRef = nullptr;
    Gem::TGemPtr<XCanvas>                          pCanvas;
    std::vector<Gem::TGemPtr<XSceneGraphNode>>     Nodes;

    ~ImportedFixture() { if (pRef) ufbx_free_scene(pRef); }

    // Returns false (after recording a skip/failure) when the asset is unavailable.
    bool Load(std::string_view assetName)
    {
        const std::filesystem::path path = AssetPath(assetName);
        if (!std::filesystem::exists(path))
            return false;

        EXPECT_TRUE(SUCCEEDED(Fbx::ImportScene(path.wstring().c_str(), Fbx::ImportOptions{}, &Scene)));
        pRef = LoadReference(path);
        EXPECT_NE(nullptr, pRef);
        EXPECT_TRUE(Succeeded(CreateCanvas(nullptr, &pCanvas)));
        if (!pRef || !pCanvas)
            return false;

        Nodes.resize(Scene.Nodes.size());
        for (size_t i = 0; i < Scene.Nodes.size(); ++i)
        {
            const Fbx::ImportedNode &src = Scene.Nodes[i];
            EXPECT_TRUE(Succeeded(pCanvas->CreateSceneGraphNode(&Nodes[i], src.Name.c_str())));
            Nodes[i]->SetLocalTranslation(src.Translation);
            Nodes[i]->SetLocalRotation(src.Rotation);
            Nodes[i]->SetLocalScale(src.Scale);
        }
        for (size_t i = 0; i < Scene.Nodes.size(); ++i)
            if (Scene.Nodes[i].ParentIndex >= 0)
                Nodes[static_cast<size_t>(Scene.Nodes[i].ParentIndex)]->AddChild(Nodes[i]);
        return true;
    }

    // World-space position of the imported node with the given name.
    Math::FloatVector4 NodePosition(std::string_view name) const
    {
        for (size_t i = 0; i < Scene.Nodes.size(); ++i)
            if (Scene.Nodes[i].Name == name)
            {
                const Math::FloatMatrix4x4 m = Nodes[i]->GetGlobalMatrix();
                return Math::FloatVector4(m[3][0], m[3][1], m[3][2], 1.0f);
            }
        ADD_FAILURE() << "missing node " << name;
        return Math::FloatVector4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    const ufbx_node *RefNode(const ufbx_scene *pScene, size_t nodeIndex) const
    {
        const std::string &name = Scene.Nodes[nodeIndex].Name;
        return ufbx_find_node_len(pScene, name.data(), name.size());
    }

    // Every Canvas global matrix must match ufbx's node_to_world.
    void ExpectNodeGlobalsMatch(const ufbx_scene *pScene) const
    {
        for (size_t i = 0; i < Nodes.size(); ++i)
        {
            const ufbx_node *pNode = RefNode(pScene, i);
            ASSERT_NE(nullptr, pNode) << Scene.Nodes[i].Name;
            EXPECT_LT(RelativeDiff(Nodes[i]->GetGlobalMatrix(), ToRowMatrix(pNode->node_to_world)), kTolerance)
                << "node " << Scene.Nodes[i].Name;
        }
    }

    // Every skin palette entry (InvBind * boneGlobal, as uploaded by the renderer) must
    // match ufbx's geometry_to_world for that cluster.
    void ExpectSkinPalettesMatch(const ufbx_scene *pScene) const
    {
        for (size_t ni = 0; ni < Scene.Nodes.size(); ++ni)
        {
            const int32_t meshIndex = Scene.Nodes[ni].MeshIndex;
            if (meshIndex < 0 || !Scene.Meshes[static_cast<size_t>(meshIndex)].Skin.HasSkin)
                continue;

            const Fbx::ImportedSkin &skin = Scene.Meshes[static_cast<size_t>(meshIndex)].Skin;
            const ufbx_node *pMeshNode = RefNode(pScene, ni);
            ASSERT_TRUE(pMeshNode && pMeshNode->mesh && pMeshNode->mesh->skin_deformers.count > 0);
            const ufbx_skin_deformer *pSkin = pMeshNode->mesh->skin_deformers.data[0];
            ASSERT_EQ(pSkin->clusters.count, skin.InvBindPoses.size());

            for (size_t c = 0; c < skin.InvBindPoses.size(); ++c)
            {
                const int32_t bone = skin.BoneNodeIndices[c];
                ASSERT_GE(bone, 0);
                const Math::FloatMatrix4x4 palette = skin.InvBindPoses[c] * Nodes[static_cast<size_t>(bone)]->GetGlobalMatrix();
                EXPECT_LT(RelativeDiff(palette, ToRowMatrix(pSkin->clusters.data[c]->geometry_to_world)), kTolerance)
                    << "cluster " << c << " bone " << Scene.Nodes[static_cast<size_t>(bone)].Name;
            }
        }
    }
};

} // anonymous namespace

TEST(CanvasFbxTest, HierarchyMatchesUfbx)
{
    bool anyLoaded = false;
    for (const char *asset : kAllAssets)
    {
        SCOPED_TRACE(asset);
        ImportedFixture f;
        if (!f.Load(asset))
            continue;
        anyLoaded = true;
        f.ExpectNodeGlobalsMatch(f.pRef);
    }
    if (!anyLoaded)
        GTEST_SKIP() << "No FBX assets available";
}

TEST(CanvasFbxTest, BindPoseMatchesUfbx)
{
    bool anyLoaded = false;
    for (const char *asset : kSkinnedAssets)
    {
        SCOPED_TRACE(asset);
        ImportedFixture f;
        if (!f.Load(asset))
            continue;
        anyLoaded = true;
        f.ExpectSkinPalettesMatch(f.pRef);
    }
    if (!anyLoaded)
        GTEST_SKIP() << "No FBX assets available";
}

// Unit conversion must not leave scale on any node: every local and global scale is 1,
// whether the file came from Blender (x100 object scale folded away) or Maya.
TEST(CanvasFbxTest, NodesHaveUnitScale)
{
    bool anyLoaded = false;
    for (const char *asset : kAllAssets)
    {
        SCOPED_TRACE(asset);
        ImportedFixture f;
        if (!f.Load(asset))
            continue;
        anyLoaded = true;
        for (size_t i = 0; i < f.Nodes.size(); ++i)
        {
            const Math::FloatVector4 s = f.Scene.Nodes[i].Scale;
            EXPECT_NEAR(s.X, 1.0f, 1e-4f) << f.Scene.Nodes[i].Name;
            EXPECT_NEAR(s.Y, 1.0f, 1e-4f) << f.Scene.Nodes[i].Name;
            EXPECT_NEAR(s.Z, 1.0f, 1e-4f) << f.Scene.Nodes[i].Name;

            const Math::FloatMatrix4x4 world = f.Nodes[i]->GetGlobalMatrix();
            for (int row = 0; row < 3; ++row)
            {
                const Math::FloatVector4 axis(world[row][0], world[row][1], world[row][2], 0.0f);
                EXPECT_NEAR(std::sqrt(Math::DotProduct(axis, axis)), 1.0f, 1e-4f)
                    << f.Scene.Nodes[i].Name << " row " << row;
            }
        }
    }
    if (!anyLoaded)
        GTEST_SKIP() << "No FBX assets available";
}

// Blocky is a roughly 1 m tall character exported from Blender in centimeters.
TEST(CanvasFbxTest, BlenderGeometryIsInMeters)
{
    ImportedFixture f;
    if (!f.Load("Blocky.fbx"))
        GTEST_SKIP() << "FBX asset not available";
    const Math::AABB &bounds = f.Scene.SceneBounds;
    const float height = bounds.Max.Z - bounds.Min.Z;
    EXPECT_GT(height, 0.5f);
    EXPECT_LT(height, 2.0f);
}

// Imported characters must face +X with their left on +Y (the Canvas local basis): the left
// hand sits on the +Y side of the right hand. Both assets face their file's front axis.
static void ExpectLeftHandOnPositiveY(const ImportedFixture &f, std::string_view leftHand, std::string_view rightHand)
{
    const Math::FloatVector4 toLeft = f.NodePosition(leftHand) - f.NodePosition(rightHand);
    EXPECT_GT(toLeft.Y, 0.0f) << "right-to-left hand (" << toLeft.X << ", " << toLeft.Y << ")";
    EXPECT_GT(toLeft.Y, 4.0f * std::abs(toLeft.X)) << "right-to-left hand (" << toLeft.X << ", " << toLeft.Y << ")";
}

// Maya HumanIK character. A second, independent cue: the toes sit on the +X side of the head.
TEST(CanvasFbxTest, MayaCharacterFacesPositiveX)
{
    ImportedFixture f;
    if (!f.Load("ufbx:maya_character_7500_binary.fbx"))
        GTEST_SKIP() << "FBX asset not available";

    ExpectLeftHandOnPositiveY(f, "Character1_LeftHand", "Character1_RightHand");

    const Math::FloatVector4 toes = (f.NodePosition("Character1_LeftToeBase") + f.NodePosition("Character1_RightToeBase")) * 0.5f;
    const Math::FloatVector4 toToes = toes - f.NodePosition("Character1_Head");
    EXPECT_GT(toToes.X, 0.0f) << "head-to-toes (" << toToes.X << ", " << toToes.Y << ")";
    EXPECT_GT(toToes.X, 4.0f * std::abs(toToes.Y)) << "head-to-toes (" << toToes.X << ", " << toToes.Y << ")";
}

// Blender-authored character, modeled facing Blender's Front view (-Y).
TEST(CanvasFbxTest, BlenderCharacterFacesPositiveX)
{
    ImportedFixture f;
    if (!f.Load("Blocky.fbx"))
        GTEST_SKIP() << "FBX asset not available";

    ExpectLeftHandOnPositiveY(f, "Bone.LHand", "Bone.RHand");
}

TEST(CanvasFbxTest, AnimatedSkinMatchesUfbx)
{
    ImportedFixture f;
    if (!f.Load("Blocky.fbx"))
        GTEST_SKIP() << "FBX asset not available";

    // Pose every animated node from the imported "Walk" clip at one of its key times.
    const auto itClip = std::find_if(f.Scene.AnimationClips.begin(), f.Scene.AnimationClips.end(),
        [](const Fbx::ImportedAnimationClip &c) { return c.Name == "Walk"; });
    ASSERT_NE(f.Scene.AnimationClips.end(), itClip);
    ASSERT_FALSE(itClip->Tracks.empty());
    const auto &firstKeys = itClip->Tracks[0].Keyframes;
    const float time = firstKeys[firstKeys.size() / 2].Time;

    for (const Fbx::ImportedAnimationTrack &track : itClip->Tracks)
    {
        const auto itKey = std::find_if(track.Keyframes.begin(), track.Keyframes.end(),
            [time](const Fbx::ImportedAnimationKeyframe &k) { return std::abs(k.Time - time) < 1e-5f; });
        ASSERT_NE(track.Keyframes.end(), itKey) << "track has no key at t=" << time;
        XSceneGraphNode *pNode = f.Nodes[static_cast<size_t>(track.NodeIndex)];
        pNode->SetLocalTranslation(itKey->Translation);
        pNode->SetLocalRotation(itKey->Rotation);
        pNode->SetLocalScale(itKey->Scale);
    }

    const ufbx_anim_stack *pStack = nullptr;
    for (size_t i = 0; i < f.pRef->anim_stacks.count; ++i)
    {
        const ufbx_string name = f.pRef->anim_stacks.data[i]->name;
        if (std::string_view(name.data, name.length).ends_with("|Walk"))
            pStack = f.pRef->anim_stacks.data[i];
    }
    ASSERT_NE(nullptr, pStack);

    ufbx_scene *pPosed = ufbx_evaluate_scene(f.pRef, pStack->anim, pStack->time_begin + time, nullptr, nullptr);
    ASSERT_NE(nullptr, pPosed);
    f.ExpectNodeGlobalsMatch(pPosed);
    f.ExpectSkinPalettesMatch(pPosed);
    ufbx_free_scene(pPosed);
}

} // namespace CanvasUnitTest
