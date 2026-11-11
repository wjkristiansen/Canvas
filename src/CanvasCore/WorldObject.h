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
    CWorld *m_pWorld = nullptr;                     // Weak pointer; set and cleared by the world
    Gem::TGemPtr<CSceneNode> m_pRootNode;           // The root node refers back to this object weakly
    size_t m_WorldIndex = 0;
    const std::string *m_pUniqueName = nullptr;     // Points at its world's copy, in the world's unique-name map
    Gem::TGemPtr<XGfxDevice> m_pDevice;             // The device of the model it instantiates, if any

public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XNamedElement)
        GEM_INTERFACE_ENTRY(XCanvasElement)
        GEM_INTERFACE_ENTRY(XWorldObject)
    END_GEM_INTERFACE_MAP()

    explicit CWorldObject(XCanvas *pCanvas);

    void Initialize() {}
    void Uninitialize();

    // Set by the world when the object joins it, and cleared when it leaves it, by
    // ReleaseWorldObject or the world's release.
    void SetWorld(CWorld *pWorld) { m_pWorld = pWorld; }
    void ClearWorld() { m_pWorld = nullptr; m_pUniqueName = nullptr; }

    // The unique name, which its world stores and sets; nullptr when the object has none.
    const std::string *GetUniqueNamePtr() const { return m_pUniqueName; }
    void SetUniqueNamePtr(const std::string *pName) { m_pUniqueName = pName; }

    // The object's position in its world's collection, kept by the world.
    size_t GetWorldIndex() const { return m_WorldIndex; }
    void SetWorldIndex(size_t index) { m_WorldIndex = index; }

    // The instance of a model the object is created as: its root node, and the device that holds
    // the model's GPU resources.
    void SetInstance(CSceneNode *pRootNode, XGfxDevice *pDevice);

    // The device whose GPU resources the object's nodes draw with, or nullptr when it is not an
    // instance of a model.
    XGfxDevice *GetDevice() const { return m_pDevice.Get(); }

    // Updates the object's nodes: each node's animation controller, then its bound elements.
    // Called by the world for each of its active world objects.
    Gem::Result Update(float dtime);

public: // XWorldObject methods
    GEMMETHOD_(XWorld *, GetWorld)() final;
    GEMMETHOD_(XSceneNode *, GetRootNode)() final { return m_pRootNode.Get(); }
    GEMMETHOD(SetRootNode)(_In_opt_ XSceneNode *pNode) final;
    GEMMETHOD_(XSceneNode *, FindNode)(_In_opt_z_ const char *name) final;
    GEMMETHOD_(const char *, GetUniqueName)() final { return m_pUniqueName ? m_pUniqueName->c_str() : nullptr; }

    // The root node as its implementation type, or nullptr when there is none.
    CSceneNode *GetRoot() const { return m_pRootNode.Get(); }
};

}
