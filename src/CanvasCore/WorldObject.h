//================================================================================================
// WorldObject
//================================================================================================

#pragma once

#include "Canvas.h"
#include "SceneGraph.h"

namespace Canvas
{

class CWorld;

//------------------------------------------------------------------------------------------------
class CWorldObject :
    public TCanvasElement<XWorldObject>
{
    CWorld *m_pWorld = nullptr;                    // Weak pointer; cleared by the world when it is released
    Gem::TGemPtr<CSceneGraphNode> m_pAnchorNode;   // The anchor node refers back to this object weakly
    size_t m_WorldIndex = 0;
    const std::string *m_pUniqueName = nullptr;   // Points at its world's copy, in the world's unique-name map

public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XNamedElement)
        GEM_INTERFACE_ENTRY(XCanvasElement)
        GEM_INTERFACE_ENTRY(XWorldObject)
    END_GEM_INTERFACE_MAP()

    CWorldObject(XCanvas *pCanvas, CWorld *pWorld, CSceneGraphNode *pAnchorNode);

    void Initialize();
    void Uninitialize();

    // Called by the world when the object leaves it, by removal or the world's release.
    void ClearWorld() { m_pWorld = nullptr; m_pUniqueName = nullptr; }

    // The unique name, which its world stores and sets; nullptr when the object has none.
    const std::string *GetUniqueNamePtr() const { return m_pUniqueName; }
    void SetUniqueNamePtr(const std::string *pName) { m_pUniqueName = pName; }

    // The object's position in its world's collection, kept by the world.
    size_t GetWorldIndex() const { return m_WorldIndex; }
    void SetWorldIndex(size_t index) { m_WorldIndex = index; }

public: // XWorldObject methods
    GEMMETHOD_(XWorld *, GetWorld)() final;
    GEMMETHOD_(XSceneGraphNode *, GetAnchorNode)() final { return m_pAnchorNode.Get(); }
    GEMMETHOD_(const char *, GetUniqueName)() final { return m_pUniqueName ? m_pUniqueName->c_str() : nullptr; }

    // The anchor node as its implementation type, or nullptr when there is none.
    CSceneGraphNode *GetAnchor() const { return m_pAnchorNode.Get(); }
};

}
