//================================================================================================
// Model
//================================================================================================

#include "pch.h"

#include "Model.h"
#include "Animation.h"
#include "CanvasGfx.h"

namespace Canvas
{

//------------------------------------------------------------------------------------------------
CModel::CModel(XCanvas *pCanvas, XGfxDevice *pDevice) :
    TCanvasElement(pCanvas),
    m_pDevice(pDevice)
{
}

//------------------------------------------------------------------------------------------------
Gem::Result CModel::Initialize()
{
    try
    {
        Gem::TGemPtr<XSceneGraphNode> pRoot;
        Gem::ThrowGemError(m_pCanvas->CreateSceneGraphNode(&pRoot, "ModelRoot"));
        m_pRoot = pRoot;
    }
    catch (const Gem::GemError &e)
    {
        return e.Result();
    }

    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CModel::AddMeshData(XGfxMeshData *pMeshData)
{
    if (!pMeshData)
        return Gem::Result::BadPointer;

    m_MeshData.push_back(pMeshData);
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CModel::AddMaterial(XGfxMaterial *pMaterial)
{
    if (!pMaterial)
        return Gem::Result::BadPointer;

    m_Materials.push_back(pMaterial);
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CModel::AddTexture(XGfxSurface *pTexture)
{
    if (!pTexture)
        return Gem::Result::BadPointer;

    m_Textures.push_back(pTexture);
    return Gem::Result::Success;
}

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CModel::AddAnimationClip(const AnimationClipDesc* pDesc)
{
    if (!pDesc)
        return Gem::Result::BadPointer;

    // Validate the descriptor's nested arrays before building anything: a positive count
    // with a null pointer is a caller error, and forming (nullptr + count) would be
    // undefined behavior even when the count is zero.
    if (pDesc->TrackCount > 0 && !pDesc->Tracks)
        return Gem::Result::BadPointer;

    for (uint32_t ti = 0; ti < pDesc->TrackCount; ++ti)
    {
        if (pDesc->Tracks[ti].KeyframeCount > 0 && !pDesc->Tracks[ti].Keyframes)
            return Gem::Result::BadPointer;
    }

    CAnimationClip clip;
    clip.Name     = pDesc->Name     ? pDesc->Name     : "";
    clip.Duration = pDesc->Duration;
    clip.Tracks.reserve(pDesc->TrackCount);

    for (uint32_t ti = 0; ti < pDesc->TrackCount; ++ti)
    {
        const AnimationTrackDesc& td = pDesc->Tracks[ti];
        AnimNodeTrack track;
        track.NodeName = td.NodeName ? td.NodeName : "";
        if (td.KeyframeCount > 0)
            track.Keyframes.assign(td.Keyframes, td.Keyframes + td.KeyframeCount);
        clip.Tracks.push_back(std::move(track));
    }

    m_AnimClips.push_back(std::move(clip));
    return Gem::Result::Success;
}

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
        // Create synthetic instance root
        Gem::TGemPtr<XSceneGraphNode> pInstanceRoot;
        {
            std::string instanceName = std::string(GetName() ? GetName() : "Model") + "_Instance";
            Gem::ThrowGemError(m_pCanvas->CreateSceneGraphNode(&pInstanceRoot, instanceName.c_str()));
        }

        // Map from model node → cloned node (for parent-child wiring and active camera lookup)
        std::unordered_map<XSceneGraphNode *, Gem::TGemPtr<XSceneGraphNode>> cloneMap;

        // Cloned camera that corresponds to the active camera node (if any)
        XCamera *pClonedActiveCamera = nullptr;

        // Iterative DFS traversal of the model's node tree
        struct StackEntry
        {
            XSceneGraphNode *pModelNode;
            XSceneGraphNode *pClonedParent;
        };

        std::vector<StackEntry> stack;
        std::vector<XSceneGraphNode *> childCollector; // Reused to reverse child order

        // Push immediate children of model root in reverse order
        // so they are popped (and thus cloned/added) in the original order.
        for (XSceneGraphNode *pChild = m_pRoot->GetFirstChild(); pChild;
             pChild = m_pRoot->GetNextChild(pChild))
        {
            childCollector.push_back(pChild);
        }
        for (auto it = childCollector.rbegin(); it != childCollector.rend(); ++it)
        {
            stack.push_back({*it, pInstanceRoot.Get()});
        }
        childCollector.clear();

        while (!stack.empty())
        {
            StackEntry entry = stack.back();
            stack.pop_back();

            XSceneGraphNode *pModelNode = entry.pModelNode;
            XSceneGraphNode *pClonedParent = entry.pClonedParent;

            // Create cloned node
            Gem::TGemPtr<XSceneGraphNode> pClonedNode;
            Gem::ThrowGemError(m_pCanvas->CreateSceneGraphNode(&pClonedNode, pModelNode->GetName()));

            // Copy transforms. Scale intentionally omitted: FBX-imported models
            // never set a node-local scale (units/inherit-mode compensation are
            // baked into geometry by the importer), so cloning the source scale
            // is unnecessary and risks propagating any latent non-unit value.
            pClonedNode->SetLocalTranslation(pModelNode->GetLocalTranslation());
            pClonedNode->SetLocalRotation(pModelNode->GetLocalRotation());

            // Clone bound elements
            const UINT elementCount = pModelNode->GetBoundElementCount();
            for (UINT i = 0; i < elementCount; ++i)
            {
                XSceneGraphElement *pElement = pModelNode->GetBoundElement(i);
                if (!pElement)
                    continue;

                // Try XMeshInstance
                Gem::TGemPtr<XMeshInstance> pMeshInstance;
                if (Gem::Succeeded(pElement->QueryInterface(&pMeshInstance)))
                {
                    Gem::TGemPtr<XMeshInstance> pClonedMesh;
                    std::string meshName = std::string(pMeshInstance->GetName() ? pMeshInstance->GetName() : "Mesh") + "_Clone";
                    Gem::ThrowGemError(m_pCanvas->CreateMeshInstance(&pClonedMesh, meshName.c_str()));
                    pClonedMesh->SetMeshData(pMeshInstance->GetMeshData());
                    Gem::ThrowGemError(pClonedNode->BindElement(pClonedMesh));
                    continue;
                }

                // Try XCamera
                Gem::TGemPtr<XCamera> pCamera;
                if (Gem::Succeeded(pElement->QueryInterface(&pCamera)))
                {
                    Gem::TGemPtr<XCamera> pClonedCamera;
                    std::string camName = std::string(pCamera->GetName() ? pCamera->GetName() : "Camera") + "_Clone";
                    Gem::ThrowGemError(m_pCanvas->CreateCamera(&pClonedCamera, camName.c_str()));
                    pClonedCamera->SetNearClip(pCamera->GetNearClip());
                    pClonedCamera->SetFarClip(pCamera->GetFarClip());
                    pClonedCamera->SetFovAngle(pCamera->GetFovAngle());
                    pClonedCamera->SetAspectRatio(pCamera->GetAspectRatio());
                    pClonedCamera->SetExposureStops(pCamera->GetExposureStops());
                    Gem::ThrowGemError(pClonedNode->BindElement(pClonedCamera));

                    // Track active camera mapping
                    if (m_pActiveCameraNode == pModelNode)
                        pClonedActiveCamera = pClonedCamera.Get();

                    continue;
                }

                // Try XLight
                Gem::TGemPtr<XLight> pLight;
                if (Gem::Succeeded(pElement->QueryInterface(&pLight)))
                {
                    Gem::TGemPtr<XLight> pClonedLight;
                    std::string lightName = std::string(pLight->GetName() ? pLight->GetName() : "Light") + "_Clone";
                    Gem::ThrowGemError(m_pCanvas->CreateLight(pLight->GetType(), &pClonedLight, lightName.c_str()));
                    pClonedLight->SetColor(pLight->GetColor());
                    pClonedLight->SetIntensity(pLight->GetIntensity());
                    pClonedLight->SetFlags(pLight->GetFlags());
                    pClonedLight->SetRange(pLight->GetRange());
                    float ac, al, aq;
                    pLight->GetAttenuation(&ac, &al, &aq);
                    pClonedLight->SetAttenuation(ac, al, aq);
                    if (pLight->GetType() == LightType::Spot)
                    {
                        float inner, outer;
                        pLight->GetSpotAngles(&inner, &outer);
                        pClonedLight->SetSpotAngles(inner, outer);
                    }
                    Gem::ThrowGemError(pClonedNode->BindElement(pClonedLight));
                    continue;
                }

                // Unknown element type — warn and skip
                Canvas::LogWarn(m_pCanvas->GetLogger(),
                    "XModel::Instantiate: unsupported element type '%s' on node '%s'; skipping",
                    pElement->GetTypeName(),
                    pModelNode->GetName() ? pModelNode->GetName() : "<unnamed>");
            }

            // Wire to parent
            Gem::ThrowGemError(pClonedParent->AddChild(pClonedNode));

            // Store in clone map
            cloneMap[pModelNode] = pClonedNode;

            // Push children for traversal (in reverse order to preserve sibling order)
            childCollector.clear();
            for (XSceneGraphNode *pChild = pModelNode->GetFirstChild(); pChild;
                 pChild = pModelNode->GetNextChild(pChild))
            {
                childCollector.push_back(pChild);
            }
            for (auto it = childCollector.rbegin(); it != childCollector.rend(); ++it)
            {
                stack.push_back({*it, pClonedNode.Get()});
            }
        }

        // Resolve skin bindings: for each skinned mesh entry, find the cloned mesh
        // instance and connect it to the cloned bone nodes.
        for (const MeshSkinEntry& skin : m_MeshSkins)
        {
            auto meshNodeIt = cloneMap.find(skin.pMeshNode);
            if (meshNodeIt == cloneMap.end())
                continue;
            XSceneGraphNode* pClonedMeshNode = meshNodeIt->second.Get();

            // Find the XMeshInstance bound to the cloned node
            const UINT elementCount = pClonedMeshNode->GetBoundElementCount();
            for (UINT ei = 0; ei < elementCount; ++ei)
            {
                XSceneGraphElement* pEl = pClonedMeshNode->GetBoundElement(ei);
                Gem::TGemPtr<XMeshInstance> pMeshInst;
                if (Gem::Failed(pEl->QueryInterface(&pMeshInst)))
                    continue;

                // Resolve each bone node through the clone map.  Every bone a skinned mesh
                // binds must exist in the model hierarchy; an unresolved bone is a malformed
                // asset and is reported as an error here, at instantiation time, rather than
                // silently producing a degenerate bone matrix during rendering.
                std::vector<XSceneGraphNode*> clonedBones;
                clonedBones.reserve(skin.BoneNodes.size());
                for (XSceneGraphNode* pBone : skin.BoneNodes)
                {
                    XSceneGraphNode* pClonedBone = nullptr;
                    if (pBone)
                    {
                        auto it = cloneMap.find(pBone);
                        if (it != cloneMap.end())
                            pClonedBone = it->second.Get();
                    }
                    if (!pClonedBone)
                    {
                        Canvas::LogError(m_pCanvas->GetLogger(),
                            "XModel::Instantiate: skinned mesh on node '%s' references a bone "
                            "that is not part of the model hierarchy",
                            pClonedMeshNode->GetName() ? pClonedMeshNode->GetName() : "<unnamed>");
                        Gem::ThrowGemError(Gem::Result::InvalidArg);
                    }
                    clonedBones.push_back(pClonedBone);
                }

                SkinBindingDesc desc{};
                desc.BoneCount     = static_cast<uint32_t>(clonedBones.size());
                desc.ppBoneNodes   = clonedBones.data();
                desc.pInvBindPoses = skin.InvBindPoses.data();
                Gem::ThrowGemError(pMeshInst->SetSkinBinding(&desc));
                break;
            }
        }

        // Create animation controller when the model has clips
        if (!m_AnimClips.empty())
        {
            CAnimationController* pRaw = nullptr;
            Gem::ThrowGemError(
                TCanvasElement<XAnimationController>::CreateAndRegister<CAnimationController>(
                    &pRaw, m_pCanvas, "AnimationController"));
            Gem::TGemPtr<CAnimationController> pCtrl;
            pCtrl.Attach(pRaw);

            pCtrl->BuildFromModel(this, m_AnimClips, cloneMap);
            // All driven nodes are descendants of pInstanceRoot by construction, so
            // ValidateForNode will always succeed here.
            Gem::ThrowGemError(pInstanceRoot->SetAnimationController(pCtrl.Get()));

            if (pResult)
                pResult->pAnimationController = pCtrl.Get();
        }

        // Attach instance root to target parent
        Gem::ThrowGemError(pTargetParent->AddChild(pInstanceRoot));

        // Populate result
        if (pResult)
        {
            pResult->pInstanceRoot = pInstanceRoot.Get();
            pResult->pActiveCamera = pClonedActiveCamera;
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

//------------------------------------------------------------------------------------------------
GEMMETHODIMP CModel::AddMeshSkin(const MeshSkinDesc *pDesc)
{
    if (!pDesc || !pDesc->pMeshNode || pDesc->BoneCount == 0)
        return Gem::Result::InvalidArg;
    if (!pDesc->ppBoneNodes || !pDesc->pInvBindPoses)
        return Gem::Result::BadPointer;

    MeshSkinEntry entry;
    entry.pMeshNode = pDesc->pMeshNode;
    entry.BoneNodes.assign(pDesc->ppBoneNodes, pDesc->ppBoneNodes + pDesc->BoneCount);
    entry.InvBindPoses.assign(pDesc->pInvBindPoses, pDesc->pInvBindPoses + pDesc->BoneCount);
    m_MeshSkins.push_back(std::move(entry));
    return Gem::Result::Success;
}

}
