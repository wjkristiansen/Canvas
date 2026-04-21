//================================================================================================
// Model
//================================================================================================

#pragma once

#include "pch.h"
#include "Canvas.h"
#include "CanvasElement.h"

namespace Canvas
{

//------------------------------------------------------------------------------------------------
class CModel :
    public TCanvasElement<XModel>
{
    Gem::TGemPtr<XGfxDevice> m_pDevice;
    Gem::TGemPtr<XSceneGraphNode> m_pRoot;

    // Shared GPU resource libraries — kept alive by the model,
    // shared across all instantiated copies.
    std::vector<Gem::TGemPtr<XGfxMeshData>> m_MeshData;
    std::vector<Gem::TGemPtr<XGfxMaterial>> m_Materials;
    std::vector<Gem::TGemPtr<XGfxSurface>> m_Textures;

    // The node in the model's hierarchy that carries the "default" camera.
    XSceneGraphNode *m_pActiveCameraNode = nullptr; // Weak pointer

    Math::AABB m_Bounds;

public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XNamedElement)
        GEM_INTERFACE_ENTRY(XCanvasElement)
        GEM_INTERFACE_ENTRY(XModel)
    END_GEM_INTERFACE_MAP()

    CModel(XCanvas *pCanvas, XGfxDevice *pDevice);

    Gem::Result Initialize();
    void Uninitialize() {}

public: // XModel methods
    GEMMETHOD_(XGfxDevice *, GetDevice)() final
    {
        return m_pDevice.Get();
    }

    GEMMETHOD_(XSceneGraphNode *, GetRootNode)() final
    {
        return m_pRoot.Get();
    }

    // Mesh data library
    GEMMETHOD_(uint32_t, GetMeshDataCount)() final
    {
        return static_cast<uint32_t>(m_MeshData.size());
    }

    GEMMETHOD_(XGfxMeshData *, GetMeshData)(uint32_t index) final
    {
        return (index < m_MeshData.size()) ? m_MeshData[index].Get() : nullptr;
    }

    GEMMETHOD(AddMeshData)(XGfxMeshData *pMeshData) final;

    // Material library
    GEMMETHOD_(uint32_t, GetMaterialCount)() final
    {
        return static_cast<uint32_t>(m_Materials.size());
    }

    GEMMETHOD_(XGfxMaterial *, GetMaterial)(uint32_t index) final
    {
        return (index < m_Materials.size()) ? m_Materials[index].Get() : nullptr;
    }

    GEMMETHOD(AddMaterial)(XGfxMaterial *pMaterial) final;

    // Texture library
    GEMMETHOD_(uint32_t, GetTextureCount)() final
    {
        return static_cast<uint32_t>(m_Textures.size());
    }

    GEMMETHOD_(XGfxSurface *, GetTexture)(uint32_t index) final
    {
        return (index < m_Textures.size()) ? m_Textures[index].Get() : nullptr;
    }

    GEMMETHOD(AddTexture)(XGfxSurface *pTexture) final;

    // Active camera node — must be a node within this model's hierarchy.
    // Passing a node that is not a descendant of GetRootNode() is
    // undefined; a debug assertion guards against this.
    GEMMETHOD_(void, SetActiveCameraNode)(XSceneGraphNode *pNode) final
    {
        if (pNode)
        {
            // Walk up to verify the node belongs to this model's hierarchy
            bool found = false;
            for (XSceneGraphNode *pCur = pNode; pCur; pCur = pCur->GetParent())
            {
                if (pCur == m_pRoot.Get())
                {
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                Canvas::LogWarn(m_pCanvas->GetLogger(),
                    "XModel::SetActiveCameraNode: node '%s' is not in this model's hierarchy; ignoring",
                    pNode->GetName() ? pNode->GetName() : "<unnamed>");
                return;
            }
        }
        m_pActiveCameraNode = pNode;
    }

    GEMMETHOD_(XSceneGraphNode *, GetActiveCameraNode)() final
    {
        return m_pActiveCameraNode;
    }

    // Instantiation
    GEMMETHOD(Instantiate)(XSceneGraphNode *pTargetParent, ModelInstantiateResult *pResult = nullptr) final;

    // Bounds
    GEMMETHOD_(Math::AABB, GetBounds)() final
    {
        return m_Bounds;
    }

    void SetBounds(const Math::AABB &bounds)
    {
        m_Bounds = bounds;
    }
};

}
