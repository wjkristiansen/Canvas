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

    // Returns the mesh data's local bounds, or an empty AABB when no
    // mesh data is bound.  Aggregating callers transform this through
    // the attached node's global matrix to obtain a world AABB.
    GEMMETHOD_(Math::AABB, GetLocalBounds)() const final
    {
        return m_pMeshData ? m_pMeshData->GetLocalBounds() : Math::AABB{};
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
};

}
