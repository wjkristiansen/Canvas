//================================================================================================
// Model
//================================================================================================

#include "pch.h"

#include "Model.h"
#include "Animation.h"
#include "Mesh.h"
#include "CanvasGfx.h"

namespace Canvas
{

namespace
{

std::string_view StringOrEmpty(const char *s)
{
    return s ? s : "";
}

// True when every pointer a descriptor's count requires is present.
template<class T>
bool HasArray(const T *p, uint32_t count)
{
    return count == 0 || p != nullptr;
}

// Bounds of every mesh at rest, relative to the instance root.  Nodes are listed parents
// first, so each node's parent global is composed before the node's own.  A skinned mesh is
// placed by its skin palette, so its bounds are the union of its mesh bounds under each
// bone's rest palette matrix.
Math::AABB ComputeBounds(const ModelContent &content, const std::vector<SkeletonPose> &restPoses)
{
    std::vector<Math::FloatMatrix4x4> globals(content.Nodes.size());
    for (size_t i = 0; i < content.Nodes.size(); ++i)
    {
        const ModelNodeData &node = content.Nodes[i];
        const Math::FloatMatrix4x4 local = node.LocalTransform.ToMatrix();
        globals[i] = node.ParentIndex == InvalidIndex ? local : local * globals[node.ParentIndex];
    }

    Math::AABB bounds;
    bounds.Reset();
    for (const ModelMeshData &mesh : content.Meshes)
    {
        const Math::AABB local = content.MeshData[mesh.MeshDataIndex]->GetLocalBounds();
        if (local.IsEmpty())
            continue;
        if (mesh.SkeletonIndex == InvalidIndex)
        {
            bounds.ExpandToInclude(local.Transform(globals[mesh.NodeIndex]));
            continue;
        }

        const uint32_t skeletonNode = content.Skeletons[mesh.SkeletonIndex].NodeIndex;
        const Math::FloatMatrix4x4 skeletonGlobal = skeletonNode == InvalidIndex
            ? Math::FloatMatrix4x4::Identity()
            : globals[skeletonNode];
        const SkeletonPose &pose = restPoses[mesh.SkeletonIndex];
        for (size_t b = 0; b < mesh.JointIndices.size(); ++b)
        {
            const Math::FloatMatrix4x4 palette =
                mesh.InvBindPoses[b] * pose.Matrices[mesh.JointIndices[b]] * skeletonGlobal;
            bounds.ExpandToInclude(local.Transform(palette));
        }
    }
    return bounds;
}

} // anonymous namespace

//================================================================================================
// CModel
//================================================================================================

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CModel::Instantiate(XSceneGraphNode *pTargetParent, ModelInstantiateResult *pResult)
{
    if (!pTargetParent)
        return Gem::Result::BadPointer;

    // Validate canvas affinity
    {
        Gem::TGemPtr<XCanvasElement> pTargetElement;
        if (Gem::Succeeded(pTargetParent->QueryInterface(&pTargetElement)))
        {
            XCanvas *pTargetCanvas = pTargetElement->GetCanvas();
            if (pTargetCanvas && pTargetCanvas != m_pCanvas)
            {
                Canvas::LogError(m_pCanvas->GetLogger(),
                    "XModel::Instantiate: target node belongs to a different XCanvas");
                return Gem::Result::InvalidArg;
            }
        }
    }

    try
    {
        // Synthetic instance root
        Gem::TGemPtr<XSceneGraphNode> pInstanceRoot;
        {
            std::string instanceName = std::string(GetName() ? GetName() : "Model") + "_Instance";
            Gem::ThrowGemError(m_pCanvas->CreateSceneGraphNode(&pInstanceRoot, instanceName.c_str()));
        }

        // One scene node per model node.  Nodes are listed parents first, and are added to
        // their parents in index order, so siblings keep the model's order.  The scene graph
        // owns each node through its parent; the vector holds weak pointers.
        std::vector<XSceneGraphNode *> nodes(m_Content.Nodes.size(), nullptr);
        for (size_t i = 0; i < m_Content.Nodes.size(); ++i)
        {
            const ModelNodeData &src = m_Content.Nodes[i];
            Gem::TGemPtr<XSceneGraphNode> pNode;
            Gem::ThrowGemError(m_pCanvas->CreateSceneGraphNode(&pNode, src.Name.c_str()));
            pNode->SetLocalTransform(src.LocalTransform);

            XSceneGraphNode *pParent = src.ParentIndex == InvalidIndex ? pInstanceRoot.Get() : nodes[src.ParentIndex];
            Gem::ThrowGemError(pParent->AddChild(pNode));
            nodes[i] = pNode.Get();
        }

        // One skeleton instance per skeleton, on the node whose space it is in.
        std::vector<CSkeletonInstance *> skeletons(m_Content.Skeletons.size(), nullptr);
        for (size_t s = 0; s < m_Content.Skeletons.size(); ++s)
        {
            const ModelSkeletonData &src = m_Content.Skeletons[s];
            CSkeletonInstance *pRaw = nullptr;
            Gem::ThrowGemError(
                TCanvasElement<XSkeletonInstance>::CreateAndRegister<CSkeletonInstance>(
                    &pRaw, m_pCanvas, src.Name.c_str()));
            Gem::TGemPtr<CSkeletonInstance> pSkeleton;
            pSkeleton.Attach(pRaw);
            pSkeleton->Bind(this, &src);

            XSceneGraphNode *pNode = src.NodeIndex == InvalidIndex ? pInstanceRoot.Get() : nodes[src.NodeIndex];
            Gem::ThrowGemError(pNode->BindElement(pSkeleton));
            skeletons[s] = pSkeleton.Get();
        }

        for (const ModelSocketData &socket : m_Content.Sockets)
            skeletons[socket.SkeletonIndex]->AddSocket(nodes[socket.NodeIndex], socket.JointIndex, socket.LocalTransform);

        for (const ModelMeshData &src : m_Content.Meshes)
        {
            Gem::TGemPtr<XMeshInstance> pMesh;
            Gem::ThrowGemError(m_pCanvas->CreateMeshInstance(&pMesh, src.Name.c_str()));
            pMesh->SetMeshData(m_Content.MeshData[src.MeshDataIndex]);
            if (src.SkeletonIndex != InvalidIndex)
            {
                SkinBindingDesc desc{};
                desc.pSkeleton     = skeletons[src.SkeletonIndex];
                desc.BoneCount     = static_cast<uint32_t>(src.JointIndices.size());
                desc.pJointIndices = src.JointIndices.data();
                desc.pInvBindPoses = src.InvBindPoses.data();
                Gem::ThrowGemError(pMesh->SetSkinBinding(&desc));
            }
            Gem::ThrowGemError(nodes[src.NodeIndex]->BindElement(pMesh));
        }

        XCamera *pActiveCamera = nullptr;
        for (size_t c = 0; c < m_Content.Cameras.size(); ++c)
        {
            const ModelCameraData &src = m_Content.Cameras[c];
            Gem::TGemPtr<XCamera> pCamera;
            Gem::ThrowGemError(m_pCanvas->CreateCamera(&pCamera, src.Name.c_str()));
            pCamera->SetNearClip(src.Params.NearClip);
            pCamera->SetFarClip(src.Params.FarClip);
            pCamera->SetFovAngle(src.Params.FovAngle);
            pCamera->SetAspectRatio(src.Params.AspectRatio);
            pCamera->SetExposureStops(src.Params.ExposureStops);
            Gem::ThrowGemError(nodes[src.NodeIndex]->BindElement(pCamera));
            if (c == m_Content.ActiveCameraIndex)
                pActiveCamera = pCamera.Get();
        }

        for (const ModelLightData &src : m_Content.Lights)
        {
            Gem::TGemPtr<XLight> pLight;
            Gem::ThrowGemError(m_pCanvas->CreateLight(src.Type, &pLight, src.Name.c_str()));
            pLight->SetColor(src.Params.Color);
            pLight->SetIntensity(src.Params.Intensity);
            pLight->SetFlags(src.Params.Flags);
            pLight->SetRange(src.Params.Range);
            pLight->SetAttenuation(src.Params.AttenuationConstant, src.Params.AttenuationLinear, src.Params.AttenuationQuadratic);
            pLight->SetSpotAngles(src.Params.SpotInnerAngle, src.Params.SpotOuterAngle);
            Gem::ThrowGemError(nodes[src.NodeIndex]->BindElement(pLight));
        }

        // Animation controller when the model has clips
        XAnimationController *pAnimationController = nullptr;
        if (!m_Content.AnimClips.empty())
        {
            CAnimationController* pRaw = nullptr;
            Gem::ThrowGemError(
                TCanvasElement<XAnimationController>::CreateAndRegister<CAnimationController>(
                    &pRaw, m_pCanvas, "AnimationController"));
            Gem::TGemPtr<CAnimationController> pCtrl;
            pCtrl.Attach(pRaw);

            pCtrl->BuildFromModel(this, m_Content.AnimClips, nodes, skeletons);
            // Everything the controller drives is in pInstanceRoot's subtree by
            // construction, so ValidateForNode will always succeed here.
            Gem::ThrowGemError(pInstanceRoot->SetAnimationController(pCtrl.Get()));

            pAnimationController = pCtrl.Get();
        }

        // Place sockets at their joints' rest pose before the first update.
        for (CSkeletonInstance *pSkeleton : skeletons)
            Gem::ThrowGemError(pSkeleton->Update(0.0f));

        // Attach instance root to target parent
        Gem::ThrowGemError(pTargetParent->AddChild(pInstanceRoot));

        if (pResult)
        {
            pResult->pInstanceRoot = pInstanceRoot.Get();
            pResult->pActiveCamera = pActiveCamera;
            pResult->pAnimationController = pAnimationController;
        }
    }
    catch (const Gem::GemError &e)
    {
        Canvas::LogError(m_pCanvas->GetLogger(),
            "XModel::Instantiate failed: %s", GemResultString(e.Result()));
        return e.Result();
    }

    return Gem::Result::Success;
}

//================================================================================================
// CModelBuilder
//================================================================================================

#define CANVAS_REQUIRE_BUILDING() \
    do { if (!m_Building) return Gem::Result::Uninitialized; } while (0)

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CModelBuilder::Begin(const char *name)
{
    if (m_Building)
        return Gem::Result::AlreadyExists;

    m_ModelName = StringOrEmpty(name);
    m_Building  = true;
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP_(void) CModelBuilder::Reset()
{
    m_Content = ModelContent();
    m_RestPoses.clear();
    m_ModelName.clear();
    m_Building = false;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CModelBuilder::AddMeshData(_In_ XGfxMeshData *pMeshData, _Out_opt_ uint32_t *pIndex)
{
    CANVAS_REQUIRE_BUILDING();
    if (!pMeshData)
        return Gem::Result::BadPointer;

    if (pIndex)
        *pIndex = static_cast<uint32_t>(m_Content.MeshData.size());
    m_Content.MeshData.push_back(pMeshData);
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CModelBuilder::AddMaterial(_In_ XGfxMaterial *pMaterial, _Out_opt_ uint32_t *pIndex)
{
    CANVAS_REQUIRE_BUILDING();
    if (!pMaterial)
        return Gem::Result::BadPointer;

    if (pIndex)
        *pIndex = static_cast<uint32_t>(m_Content.Materials.size());
    m_Content.Materials.push_back(pMaterial);
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CModelBuilder::AddTexture(_In_ XGfxSurface *pTexture, _Out_opt_ uint32_t *pIndex)
{
    CANVAS_REQUIRE_BUILDING();
    if (!pTexture)
        return Gem::Result::BadPointer;

    if (pIndex)
        *pIndex = static_cast<uint32_t>(m_Content.Textures.size());
    m_Content.Textures.push_back(pTexture);
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CModelBuilder::AddNode(_In_ const ModelNodeDesc *pDesc, _Out_opt_ uint32_t *pIndex)
{
    CANVAS_REQUIRE_BUILDING();
    if (!pDesc)
        return Gem::Result::BadPointer;
    if (pDesc->ParentIndex != InvalidIndex && pDesc->ParentIndex >= m_Content.Nodes.size())
        return Gem::Result::OutOfRange;

    ModelNodeData node;
    node.Name        = StringOrEmpty(pDesc->Name);
    node.ParentIndex = pDesc->ParentIndex;
    node.LocalTransform   = pDesc->LocalTransform;

    if (pIndex)
        *pIndex = static_cast<uint32_t>(m_Content.Nodes.size());
    m_Content.Nodes.push_back(std::move(node));
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CModelBuilder::AddMesh(_In_ const ModelMeshDesc *pDesc, _Out_opt_ uint32_t *pIndex)
{
    CANVAS_REQUIRE_BUILDING();
    if (!pDesc)
        return Gem::Result::BadPointer;
    if (pDesc->NodeIndex >= m_Content.Nodes.size() || pDesc->MeshDataIndex >= m_Content.MeshData.size())
        return Gem::Result::OutOfRange;

    ModelMeshData mesh;
    mesh.Name          = StringOrEmpty(pDesc->Name);
    mesh.NodeIndex     = pDesc->NodeIndex;
    mesh.MeshDataIndex = pDesc->MeshDataIndex;

    if (pDesc->SkeletonIndex != InvalidIndex)
    {
        if (pDesc->SkeletonIndex >= m_Content.Skeletons.size())
            return Gem::Result::OutOfRange;
        if (pDesc->BoneCount == 0)
            return Gem::Result::InvalidArg;
        if (!pDesc->pJointIndices || !pDesc->pInvBindPoses)
            return Gem::Result::BadPointer;

        const size_t jointCount = m_Content.Skeletons[pDesc->SkeletonIndex].Joints.size();
        for (uint32_t b = 0; b < pDesc->BoneCount; ++b)
            if (pDesc->pJointIndices[b] >= jointCount)
                return Gem::Result::OutOfRange;

        mesh.SkeletonIndex = pDesc->SkeletonIndex;
        mesh.JointIndices.assign(pDesc->pJointIndices, pDesc->pJointIndices + pDesc->BoneCount);
        mesh.InvBindPoses.assign(pDesc->pInvBindPoses, pDesc->pInvBindPoses + pDesc->BoneCount);
    }
    else if (pDesc->BoneCount != 0 || pDesc->pJointIndices || pDesc->pInvBindPoses)
    {
        return Gem::Result::InvalidArg;   // skin data without a skeleton
    }

    if (pIndex)
        *pIndex = static_cast<uint32_t>(m_Content.Meshes.size());
    m_Content.Meshes.push_back(std::move(mesh));
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CModelBuilder::AddCamera(_In_ const ModelCameraDesc *pDesc, _Out_opt_ uint32_t *pIndex)
{
    CANVAS_REQUIRE_BUILDING();
    if (!pDesc)
        return Gem::Result::BadPointer;
    if (pDesc->NodeIndex >= m_Content.Nodes.size())
        return Gem::Result::OutOfRange;

    ModelCameraData camera;
    camera.Name      = StringOrEmpty(pDesc->Name);
    camera.NodeIndex = pDesc->NodeIndex;
    camera.Params    = pDesc->Params;

    if (pIndex)
        *pIndex = static_cast<uint32_t>(m_Content.Cameras.size());
    m_Content.Cameras.push_back(std::move(camera));
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CModelBuilder::AddLight(_In_ const ModelLightDesc *pDesc, _Out_opt_ uint32_t *pIndex)
{
    CANVAS_REQUIRE_BUILDING();
    if (!pDesc)
        return Gem::Result::BadPointer;
    if (pDesc->NodeIndex >= m_Content.Nodes.size())
        return Gem::Result::OutOfRange;

    ModelLightData light;
    light.Name      = StringOrEmpty(pDesc->Name);
    light.NodeIndex = pDesc->NodeIndex;
    light.Type      = pDesc->Type;
    light.Params    = pDesc->Params;

    if (pIndex)
        *pIndex = static_cast<uint32_t>(m_Content.Lights.size());
    m_Content.Lights.push_back(std::move(light));
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CModelBuilder::AddSkeleton(_In_ const ModelSkeletonDesc *pDesc, _Out_opt_ uint32_t *pIndex)
{
    CANVAS_REQUIRE_BUILDING();
    if (!pDesc)
        return Gem::Result::BadPointer;
    if (pDesc->JointCount == 0)
        return Gem::Result::InvalidArg;
    if (!pDesc->pJoints)
        return Gem::Result::BadPointer;
    if (pDesc->NodeIndex != InvalidIndex && pDesc->NodeIndex >= m_Content.Nodes.size())
        return Gem::Result::OutOfRange;

    // Joints are listed parents first.
    for (uint32_t j = 0; j < pDesc->JointCount; ++j)
    {
        const uint32_t parent = pDesc->pJoints[j].ParentIndex;
        if (parent != InvalidIndex && parent >= j)
            return Gem::Result::OutOfRange;
    }

    try
    {
        ModelSkeletonData skeleton;
        skeleton.Name      = StringOrEmpty(pDesc->Name);
        skeleton.NodeIndex = pDesc->NodeIndex;
        skeleton.Joints.reserve(pDesc->JointCount);
        for (uint32_t j = 0; j < pDesc->JointCount; ++j)
        {
            const ModelJointDesc &src = pDesc->pJoints[j];
            ModelJointData joint;
            joint.Name          = StringOrEmpty(src.Name);
            joint.ParentIndex   = src.ParentIndex;
            joint.RestTransform = src.RestTransform;
            skeleton.Joints.push_back(std::move(joint));
        }

        std::vector<Transform> rest;
        rest.reserve(skeleton.Joints.size());
        for (const ModelJointData &joint : skeleton.Joints)
            rest.push_back(joint.RestTransform);
        SkeletonPose pose;
        pose.Compute(skeleton, rest.data());

        // Reserve both tables first so they stay in step.
        m_Content.Skeletons.reserve(m_Content.Skeletons.size() + 1);
        m_RestPoses.reserve(m_RestPoses.size() + 1);
        if (pIndex)
            *pIndex = static_cast<uint32_t>(m_Content.Skeletons.size());
        m_Content.Skeletons.push_back(std::move(skeleton));
        m_RestPoses.push_back(std::move(pose));
    }
    catch (const std::bad_alloc &)
    {
        return Gem::Result::OutOfMemory;
    }
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CModelBuilder::AddSocket(_In_ const ModelSocketDesc *pDesc, _Out_opt_ uint32_t *pNodeIndex)
{
    CANVAS_REQUIRE_BUILDING();
    if (!pDesc)
        return Gem::Result::BadPointer;
    if (pDesc->SkeletonIndex >= m_Content.Skeletons.size() ||
        pDesc->JointIndex >= m_Content.Skeletons[pDesc->SkeletonIndex].Joints.size())
        return Gem::Result::OutOfRange;

    try
    {
        // The socket's model node is a child of the skeleton's node, placed at the joint's rest
        // pose; each instance's skeleton instance moves it with the joint.
        ModelNodeData node;
        node.Name           = StringOrEmpty(pDesc->Name);
        node.ParentIndex    = m_Content.Skeletons[pDesc->SkeletonIndex].NodeIndex;
        node.LocalTransform = m_RestPoses[pDesc->SkeletonIndex].SocketTransform(pDesc->JointIndex, pDesc->LocalTransform);

        ModelSocketData socket;
        socket.NodeIndex      = static_cast<uint32_t>(m_Content.Nodes.size());
        socket.SkeletonIndex  = pDesc->SkeletonIndex;
        socket.JointIndex     = pDesc->JointIndex;
        socket.LocalTransform = pDesc->LocalTransform;

        // Reserve both tables first so they stay in step.
        m_Content.Nodes.reserve(m_Content.Nodes.size() + 1);
        m_Content.Sockets.reserve(m_Content.Sockets.size() + 1);
        if (pNodeIndex)
            *pNodeIndex = socket.NodeIndex;
        m_Content.Nodes.push_back(std::move(node));
        m_Content.Sockets.push_back(socket);
    }
    catch (const std::bad_alloc &)
    {
        return Gem::Result::OutOfMemory;
    }
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CModelBuilder::AddAnimationClip(_In_ const AnimationClipDesc *pDesc, _Out_opt_ uint32_t *pIndex)
{
    CANVAS_REQUIRE_BUILDING();
    if (!pDesc)
        return Gem::Result::BadPointer;

    // Validate the descriptor's nested arrays before building anything: a positive count
    // with a null pointer is a caller error, and forming (nullptr + count) would be
    // undefined behavior even when the count is zero.
    if (!HasArray(pDesc->Tracks, pDesc->TrackCount))
        return Gem::Result::BadPointer;
    for (uint32_t ti = 0; ti < pDesc->TrackCount; ++ti)
        if (!HasArray(pDesc->Tracks[ti].Keyframes, pDesc->Tracks[ti].KeyframeCount))
            return Gem::Result::BadPointer;

    CAnimationClip clip;
    clip.Name     = StringOrEmpty(pDesc->Name);
    clip.Duration = pDesc->Duration;
    clip.Tracks.reserve(pDesc->TrackCount);

    for (uint32_t ti = 0; ti < pDesc->TrackCount; ++ti)
    {
        const AnimationTrackDesc &td = pDesc->Tracks[ti];
        AnimTrack track;
        track.TargetType    = td.TargetType;
        track.SkeletonIndex = td.SkeletonIndex;
        track.TargetIndex   = td.TargetIndex;
        if (td.KeyframeCount > 0)
            track.Keyframes.assign(td.Keyframes, td.Keyframes + td.KeyframeCount);
        clip.Tracks.push_back(std::move(track));
    }

    if (pIndex)
        *pIndex = static_cast<uint32_t>(m_Content.AnimClips.size());
    m_Content.AnimClips.push_back(std::move(clip));
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CModelBuilder::SetActiveCamera(uint32_t cameraIndex)
{
    CANVAS_REQUIRE_BUILDING();
    if (cameraIndex != InvalidIndex && cameraIndex >= m_Content.Cameras.size())
        return Gem::Result::OutOfRange;

    m_Content.ActiveCameraIndex = cameraIndex;
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
Gem::Result CModelBuilder::ValidateClipTargets() const
{
    for (const CAnimationClip &clip : m_Content.AnimClips)
    {
        for (const AnimTrack &track : clip.Tracks)
        {
            bool valid = false;
            switch (track.TargetType)
            {
            case AnimationTargetType::ModelNode:
                valid = track.TargetIndex < m_Content.Nodes.size();
                break;
            case AnimationTargetType::Joint:
                valid = track.SkeletonIndex < m_Content.Skeletons.size() &&
                        track.TargetIndex < m_Content.Skeletons[track.SkeletonIndex].Joints.size();
                break;
            }
            if (!valid)
            {
                Canvas::LogError(m_pCanvas->GetLogger(),
                    "XModelBuilder::Build: clip '%s' has a track whose target does not exist", clip.Name.c_str());
                return Gem::Result::OutOfRange;
            }
        }
    }
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CModelBuilder::Build(_Out_ XModel **ppModel)
{
    if (!ppModel)
        return Gem::Result::BadPointer;
    *ppModel = nullptr;

    CANVAS_REQUIRE_BUILDING();
    if (m_Content.Nodes.empty())
    {
        Canvas::LogError(m_pCanvas->GetLogger(), "XModelBuilder::Build: model '%s' has no model nodes", m_ModelName.c_str());
        return Gem::Result::InvalidArg;
    }

    Gem::Result result = ValidateClipTargets();
    if (Gem::Failed(result))
        return result;

    Gem::TGemPtr<CModel> pModel;
    try
    {
        m_Content.Bounds = ComputeBounds(m_Content, m_RestPoses);

        // The model moves the content out of the builder; a failed allocation leaves it here.
        pModel = new Gem::TGenericImpl<CModel>(m_pCanvas, m_pDevice.Get(), m_Content);
    }
    catch (const std::bad_alloc &)
    {
        return Gem::Result::OutOfMemory;
    }

    if (!m_ModelName.empty())
        pModel->SetName(m_ModelName.c_str());

    result = m_pCanvas->RegisterElement(pModel.Get());
    if (Gem::Failed(result))
    {
        m_Content = pModel->TakeContent();
        return result;
    }

    *ppModel = pModel.Detach();
    Reset();
    return Gem::Result::Success;
}

#undef CANVAS_REQUIRE_BUILDING

}
