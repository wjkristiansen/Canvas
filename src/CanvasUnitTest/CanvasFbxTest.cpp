//================================================================================================
// CanvasFbxTest -- checks imported FBX transforms against ufbx's own evaluation.
//
// The imported node TRS values are loaded into Canvas scene-graph nodes. As imported, every
// global matrix must match ufbx's bind pose and the skin must leave the mesh undeformed.
// Mid-animation, globals must match ufbx's node_to_world and skin palettes (InvBind *
// boneGlobal) ufbx's per-cluster geometry_to_world.
//================================================================================================

#include "pch.h"
#include "CanvasFbx.h"
#include <ufbx.h>
#include <filesystem>

using namespace Canvas;

namespace CanvasUnitTest
{
namespace {

// Assets prefixed "ufbx:" come from ufbx's own test data (used for Maya-exported files);
// "data:" assets are hand-authored ASCII files in CanvasUnitTest/Data, each built to pin
// down one case.
std::filesystem::path AssetPath(std::string_view name)
{
    if (name.starts_with("ufbx:"))
        return std::filesystem::path(CANVAS_UFBX_DATA_DIR) / name.substr(5);
    if (name.starts_with("data:"))
        return std::filesystem::path(CANVAS_TEST_DATA_DIR) / name.substr(5);
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

// The sausage files record a mesh BindPose entry that disagrees with their skin clusters;
// maya_instanced_skin has a second, unbound instance of its skinned mesh.
constexpr const char *kSkinnedAssets[] = {
    "Blocky.fbx",
    "ufbx:maya_transformed_skin_7700_binary.fbx",
    "ufbx:maya_advanced_skinned_pivot_7700_binary.fbx",
    "ufbx:maya_instanced_skin_7700_binary.fbx",
    "ufbx:blender_279_sausage_6100_ascii.fbx",
    "ufbx:motionbuilder_sausage_rrss_7700_binary.fbx",
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
        return LoadPath(AssetPath(assetName));
    }

    bool LoadPath(const std::filesystem::path &path)
    {
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

    // SceneBounds must enclose every mesh where the imported hierarchy places it.
    void ExpectSceneBoundsMatchHierarchy() const
    {
        Math::AABB expected;
        expected.Reset();
        for (size_t i = 0; i < Scene.Nodes.size(); ++i)
        {
            const int32_t meshIndex = Scene.Nodes[i].MeshIndex;
            if (meshIndex >= 0)
                expected.ExpandToInclude(Scene.Meshes[static_cast<size_t>(meshIndex)].Bounds.Transform(Nodes[i]->GetGlobalMatrix()));
        }

        const Math::AABB &actual = Scene.SceneBounds;
        const float size = std::max({ 1.0f, expected.Max.X - expected.Min.X, expected.Max.Y - expected.Min.Y, expected.Max.Z - expected.Min.Z });
        EXPECT_NEAR(actual.Min.X, expected.Min.X, kTolerance * size);
        EXPECT_NEAR(actual.Min.Y, expected.Min.Y, kTolerance * size);
        EXPECT_NEAR(actual.Min.Z, expected.Min.Z, kTolerance * size);
        EXPECT_NEAR(actual.Max.X, expected.Max.X, kTolerance * size);
        EXPECT_NEAR(actual.Max.Y, expected.Max.Y, kTolerance * size);
        EXPECT_NEAR(actual.Max.Z, expected.Max.Z, kTolerance * size);
    }

    // True when the import reported a sheared bind pose for the named node (or, with an
    // empty name, for any node).
    bool HasShearWarning(std::string_view nodeName = {}) const
    {
        const std::string quoted = "'" + std::string(nodeName) + "'";
        return std::any_of(Scene.Diagnostics.begin(), Scene.Diagnostics.end(), [&](const Fbx::ImportDiag &d)
            {
                return d.Level == Fbx::DiagLevel::Warning && d.Message.find("shear") != std::string::npos &&
                       (nodeName.empty() || d.Message.find(quoted) != std::string::npos);
            });
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

    // ufbx's world matrix for a node in the bind pose: its cluster bind matrix; for a skinned
    // mesh (first instance), the placement its skin was bound at; its BindPose entry;
    // otherwise the parent's bind world times the node's stored local transform.
    static ufbx_matrix RefBindWorld(const ufbx_scene *pScene, const ufbx_node *pNode)
    {
        for (const ufbx_skin_cluster *pCluster : pScene->skin_clusters)
            if (pCluster->bone_node == pNode)
                return pCluster->bind_to_world;
        if (IsFirstSkinnedInstance(pNode))
            for (const ufbx_skin_cluster *pCluster : pNode->mesh->skin_deformers.data[0]->clusters)
                if (pCluster->bone_node)
                {
                    const ufbx_matrix nodeToGeometry = ufbx_matrix_invert(&pNode->geometry_to_node);
                    const ufbx_matrix nodeToBone = ufbx_matrix_mul(&pCluster->geometry_to_bone, &nodeToGeometry);
                    return ufbx_matrix_mul(&pCluster->bind_to_world, &nodeToBone);
                }
        for (const ufbx_pose *pPose : pScene->poses)
            if (pPose->is_bind_pose)
                if (const ufbx_bone_pose *pBonePose = ufbx_get_bone_pose(pPose, pNode))
                    return pBonePose->bone_to_world;
        if (!pNode->parent)
            return pNode->node_to_world;
        const ufbx_matrix parentToWorld = RefBindWorld(pScene, pNode->parent);
        return ufbx_matrix_mul(&parentToWorld, &pNode->node_to_parent);
    }

    // Every Canvas global matrix, as imported, must match ufbx's bind pose.
    void ExpectBindPoseGlobalsMatch(const ufbx_scene *pScene) const
    {
        for (size_t i = 0; i < Nodes.size(); ++i)
        {
            const ufbx_node *pNode = RefNode(pScene, i);
            ASSERT_NE(nullptr, pNode) << Scene.Nodes[i].Name;
            EXPECT_LT(RelativeDiff(Nodes[i]->GetGlobalMatrix(), ToRowMatrix(RefBindWorld(pScene, pNode))), kTolerance)
                << "node " << Scene.Nodes[i].Name;
        }
    }

    // True for the node a skinned mesh's bind data refers to: ufbx binds a skin to the
    // mesh's first instance.
    static bool IsFirstSkinnedInstance(const ufbx_node *pNode)
    {
        const ufbx_mesh *pMesh = pNode->mesh;
        return pMesh && pMesh->skin_deformers.count > 0 && pMesh->instances.count > 0 && pMesh->instances.data[0] == pNode;
    }

    // In the bind pose the skin applies no deformation: every palette entry (InvBind *
    // boneGlobal) equals the skinned mesh node's own global matrix. Checked for each skinned
    // mesh's first instance, the one its bind data refers to.
    void ExpectSkinPalettesUndeformed(const ufbx_scene *pScene) const
    {
        for (size_t ni = 0; ni < Scene.Nodes.size(); ++ni)
        {
            const int32_t meshIndex = Scene.Nodes[ni].MeshIndex;
            if (meshIndex < 0 || !Scene.Meshes[static_cast<size_t>(meshIndex)].Skin.HasSkin)
                continue;
            const ufbx_node *pMeshNode = RefNode(pScene, ni);
            ASSERT_NE(nullptr, pMeshNode) << Scene.Nodes[ni].Name;
            if (!IsFirstSkinnedInstance(pMeshNode))
                continue;

            const Fbx::ImportedSkin &skin = Scene.Meshes[static_cast<size_t>(meshIndex)].Skin;
            const Math::FloatMatrix4x4 meshGlobal = Nodes[ni]->GetGlobalMatrix();
            for (size_t c = 0; c < skin.InvBindPoses.size(); ++c)
            {
                const int32_t bone = skin.BoneNodeIndices[c];
                ASSERT_GE(bone, 0);
                const Math::FloatMatrix4x4 palette = skin.InvBindPoses[c] * Nodes[static_cast<size_t>(bone)]->GetGlobalMatrix();
                EXPECT_LT(RelativeDiff(palette, meshGlobal), kTolerance)
                    << "cluster " << c << " bone " << Scene.Nodes[static_cast<size_t>(bone)].Name;
            }
        }
    }

    // Plays the imported clip: poses every tracked node at one of the clip's key times (a
    // single-key track holds its node still for the whole clip; untracked nodes stay as
    // imported), then compares globals and skin palettes with ufbx's evaluation of the
    // matching stack at that time.
    void ExpectClipMatchesUfbx(std::string_view clipName)
    {
        const auto itClip = std::find_if(Scene.AnimationClips.begin(), Scene.AnimationClips.end(),
            [clipName](const Fbx::ImportedAnimationClip &c) { return c.Name == clipName; });
        ASSERT_NE(Scene.AnimationClips.end(), itClip) << "missing clip " << clipName;
        ASSERT_FALSE(itClip->Tracks.empty());
        const auto &keys = std::max_element(itClip->Tracks.begin(), itClip->Tracks.end(),
            [](const Fbx::ImportedAnimationTrack &a, const Fbx::ImportedAnimationTrack &b)
            { return a.Keyframes.size() < b.Keyframes.size(); })->Keyframes;
        const float time = keys[keys.size() / 2].Time;

        for (const Fbx::ImportedAnimationTrack &track : itClip->Tracks)
        {
            const auto itKey = track.Keyframes.size() == 1 ? track.Keyframes.begin() :
                std::find_if(track.Keyframes.begin(), track.Keyframes.end(),
                    [time](const Fbx::ImportedAnimationKeyframe &k) { return std::abs(k.Time - time) < 1e-5f; });
            ASSERT_NE(track.Keyframes.end(), itKey) << "track has no key at t=" << time;
            XSceneGraphNode *pNode = Nodes[static_cast<size_t>(track.NodeIndex)];
            pNode->SetLocalTranslation(itKey->Translation);
            pNode->SetLocalRotation(itKey->Rotation);
            pNode->SetLocalScale(itKey->Scale);
        }

        // Blender names stacks "Object|Action"; the clip takes the action name.
        const ufbx_anim_stack *pStack = nullptr;
        for (const ufbx_anim_stack *pCandidate : pRef->anim_stacks)
        {
            const std::string_view name(pCandidate->name.data, pCandidate->name.length);
            const size_t bar = name.find('|');
            if ((bar == std::string_view::npos ? name : name.substr(bar + 1)) == clipName)
                pStack = pCandidate;
        }
        ASSERT_NE(nullptr, pStack) << "missing stack for clip " << clipName;

        ufbx_scene *pPosed = ufbx_evaluate_scene(pRef, pStack->anim, pStack->time_begin + time, nullptr, nullptr);
        ASSERT_NE(nullptr, pPosed);
        ExpectNodeGlobalsMatch(pPosed);
        ExpectSkinPalettesMatch(pPosed);
        ufbx_free_scene(pPosed);
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
        f.ExpectBindPoseGlobalsMatch(f.pRef);
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
        f.ExpectSkinPalettesUndeformed(f.pRef);
        EXPECT_FALSE(f.HasShearWarning());
    }
    if (!anyLoaded)
        GTEST_SKIP() << "No FBX assets available";
}

// A skinned mesh moved after binding, in a file with no BindPose: the mesh node's bind
// placement comes from the skin alone. Tri and its bone were bound at the origin and saved
// 10 m away, so the mesh must import at the origin, where its skin renders it.
TEST(CanvasFbxTest, SkinnedMeshWithoutBindPoseEntry)
{
    ImportedFixture f;
    ASSERT_TRUE(f.Load("data:MovedSkinNoBindPose.fbx"));
    ASSERT_EQ(0u, f.pRef->poses.count);

    const Math::FloatVector4 tri = f.NodePosition("Tri");
    EXPECT_LT(std::sqrt(tri.X * tri.X + tri.Y * tri.Y + tri.Z * tri.Z), 1e-4f)
        << "Tri at (" << tri.X << ", " << tri.Y << ", " << tri.Z << ")";
    // The 1 m triangle's bounds must surround the origin, not its saved spot 10 m away.
    const Math::AABB &bounds = f.Scene.SceneBounds;
    EXPECT_LT(std::max({ std::abs(bounds.Min.X), std::abs(bounds.Min.Y), std::abs(bounds.Min.Z),
                         std::abs(bounds.Max.X), std::abs(bounds.Max.Y), std::abs(bounds.Max.Z) }), 1.0f + 1e-4f);

    f.ExpectBindPoseGlobalsMatch(f.pRef);
    f.ExpectSkinPalettesUndeformed(f.pRef);
}

// A clip explicitly holds bone B at its bind rotation while B's stored rotation is 30
// degrees away. Playing the clip must keep B at bind, not fall back to the stored rotation.
TEST(CanvasFbxTest, ClipHoldsNodeAtBindPose)
{
    ImportedFixture f;
    ASSERT_TRUE(f.Load("data:ClipHoldsBindPose.fbx"));
    f.ExpectClipMatchesUfbx("Hold");
}

// A clip whose only track is redundant (A held at its bind position) still plays: it holds
// unanimated bone C at its stored rotation, 30 degrees from bind.
TEST(CanvasFbxTest, PoseClipHoldsUnanimatedNode)
{
    ImportedFixture f;
    ASSERT_TRUE(f.Load("data:PoseClipHoldsUnanimatedBone.fbx"));
    f.ExpectClipMatchesUfbx("Pose");
}

// Bone B's bind matrix has shear relative to its non-uniformly scaled parent, which a Canvas
// node's translation, rotation, and scale cannot represent. The importer must say so, and
// the scene bounds must still enclose Rigid (an unskinned mesh under B) where it renders,
// with B's approximated transform.
TEST(CanvasFbxTest, ShearedBindPoseIsReported)
{
    ImportedFixture f;
    ASSERT_TRUE(f.Load("data:ShearedBindPose.fbx"));
    EXPECT_TRUE(f.HasShearWarning("B"));
    EXPECT_FALSE(f.HasShearWarning("P"));
    f.ExpectSceneBoundsMatchHierarchy();
}

// Bone Far is bound 1000 m from the origin and saved 5 mm further out; clip "Move" leaves it
// unanimated. The clip must hold Far at its saved position: the pose comparison may not
// loosen with distance from the origin. (Checked directly: ExpectClipMatchesUfbx compares
// relative to matrix magnitude, which at 1000 m cannot see 5 mm.)
TEST(CanvasFbxTest, UnanimatedNodeFarFromOrigin)
{
    ImportedFixture f;
    ASSERT_TRUE(f.Load("data:FarBoneNearBindPose.fbx"));

    const auto itNode = std::find_if(f.Scene.Nodes.begin(), f.Scene.Nodes.end(),
        [](const Fbx::ImportedNode &n) { return n.Name == "Far"; });
    ASSERT_NE(f.Scene.Nodes.end(), itNode);
    const int32_t farIndex = static_cast<int32_t>(itNode - f.Scene.Nodes.begin());

    const auto itClip = std::find_if(f.Scene.AnimationClips.begin(), f.Scene.AnimationClips.end(),
        [](const Fbx::ImportedAnimationClip &c) { return c.Name == "Move"; });
    ASSERT_NE(f.Scene.AnimationClips.end(), itClip);
    const auto itTrack = std::find_if(itClip->Tracks.begin(), itClip->Tracks.end(),
        [farIndex](const Fbx::ImportedAnimationTrack &t) { return t.NodeIndex == farIndex; });
    ASSERT_NE(itClip->Tracks.end(), itTrack) << "no track holds Far at its saved position";
    ASSERT_EQ(1u, itTrack->Keyframes.size());

    const Math::FloatVector4 d = itTrack->Keyframes[0].Translation - itNode->Translation;
    EXPECT_NEAR(std::sqrt(Math::DotProduct(d, d)), 0.005f, 0.001f);
}

// Clip "Raise" animates only bone A and clip "Turn" only bone C; both are saved at bind.
// Playing Raise and then Turn, without resetting in between (as the animation controller
// does), must leave A where Turn holds it: its stored pose, not Raise's last pose.
TEST(CanvasFbxTest, SwitchingClipsResetsNodesTheyShare)
{
    ImportedFixture f;
    ASSERT_TRUE(f.Load("data:SwitchClipsDisjointBones.fbx"));
    f.ExpectClipMatchesUfbx("Raise");
    f.ExpectClipMatchesUfbx("Turn");
    f.ExpectClipMatchesUfbx("Raise");
}

// A clip leaves bone C unanimated, and C's stored rotation is 0.25 degrees from bind.
// Playing the clip must hold C at the stored rotation; the small difference is not the
// bind pose.
TEST(CanvasFbxTest, UnanimatedNodeNearBindPose)
{
    ImportedFixture f;
    ASSERT_TRUE(f.Load("data:UnanimatedBoneNearBindPose.fbx"));
    f.ExpectClipMatchesUfbx("Move");
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

// SceneBounds must describe the hierarchy as imported (the bind pose), not the stored file pose.
TEST(CanvasFbxTest, SceneBoundsMatchImportedHierarchy)
{
    bool anyLoaded = false;
    for (const char *asset : kAllAssets)
    {
        SCOPED_TRACE(asset);
        ImportedFixture f;
        if (!f.Load(asset))
            continue;
        anyLoaded = true;
        f.ExpectSceneBoundsMatchHierarchy();
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

    f.ExpectClipMatchesUfbx("Walk");
}

} // namespace CanvasUnitTest
