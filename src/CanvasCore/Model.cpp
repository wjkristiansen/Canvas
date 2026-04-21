//================================================================================================
// Model
//================================================================================================

#include "pch.h"

#include "Model.h"
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

            // Copy transforms
            pClonedNode->SetLocalTranslation(pModelNode->GetLocalTranslation());
            pClonedNode->SetLocalRotation(pModelNode->GetLocalRotation());
            pClonedNode->SetLocalScale(pModelNode->GetLocalScale());

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

}
