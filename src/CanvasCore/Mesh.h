//================================================================================================
// Mesh
//================================================================================================

#pragma once

#include "SceneGraph.h"

namespace Canvas
{

//------------------------------------------------------------------------------------------------
class CMeshInstance :
    public TSceneGraphElement<XMeshInstance>
{
    Gem::TGemPtr<XGfxMeshData> m_pMeshData;
    uint32_t m_MaterialGroupIndex = 0;

public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XNamedElement)
        GEM_INTERFACE_ENTRY(XCanvasElement)
        GEM_INTERFACE_ENTRY(XSceneGraphElement)
        GEM_INTERFACE_ENTRY(XMeshInstance)
    END_GEM_INTERFACE_MAP()

    CMeshInstance(XCanvas *pCanvas) :
        TSceneGraphElement<XMeshInstance>(pCanvas)
    {}

    Gem::Result Initialize() { return Gem::Result::Success; }
    void Uninitialize() {}
    
    // XSceneGraphElement methods
    GEMMETHOD(Update)(float /*dtime*/) final 
    { 
        return Gem::Result::Success; 
    }

    GEMMETHOD(NotifyNodeContextChanged)(_In_ XSceneGraphNode *pNode) final
    {
        return TSceneGraphElement<XMeshInstance>::NotifyNodeContextChanged(pNode);
    }

    GEMMETHOD(DispatchForRender)(XRenderQueue *pRenderQueue) final
    {
        auto *pNode = GetAttachedNode();
        if (!pNode || !m_pMeshData)
            return Gem::Result::Success;

        GfxPerObjectConstants objConstants = {};
        objConstants.World = pNode->GetGlobalMatrix();
        objConstants.WorldInvTranspose = objConstants.World;
        objConstants.WorldInvTranspose[3] = Math::FloatVector4(0.0f, 0.0f, 0.0f, 1.0f);

        return pRenderQueue->DrawMesh(m_pMeshData, objConstants);
    }

    // XMeshInstance methods
    GEMMETHOD_(XGfxMeshData *, GetMeshData)() final 
    { 
        return m_pMeshData.Get(); 
    }

    GEMMETHOD_(void, SetMeshData)(XGfxMeshData *pMesh) final 
    { 
        m_pMeshData = pMesh; 
    }

    GEMMETHOD_(uint32_t, GetMaterialGroupIndex)() final 
    { 
        return m_MaterialGroupIndex; 
    }
};

}
