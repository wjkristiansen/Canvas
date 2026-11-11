//================================================================================================
// World
//================================================================================================

#pragma once

#include "Canvas.h"
#include "SceneGraph.h"
#include "WorldBVH.h"
#include "LightBVH.h"
#include "WorldObject.h"

namespace Canvas
{

//------------------------------------------------------------------------------------------------
class CWorld :
    public TCanvasElement<XWorld>
{
private:
    Gem::TGemPtr<XGfxDevice> m_pDevice;
    Gem::TGemPtr<XSceneNode> m_pRoot;
    Gem::TGemPtr<XCamera> m_pActiveCamera;
    std::vector<XSceneNode*> m_TraversalStack;  // Reused across frames to avoid allocation

    // BVH over the world-space AABB of every renderable scene element
    // bound to a node in the scene graph (one primitive per
    // [node, element] pair whose element returns a non-empty
    // GetLocalBounds()).  Non-renderable elements (lights, cameras)
    // honor the empty-AABB contract and contribute no primitives; they
    // continue to flow through SubmitRenderables unfiltered.  Built
    // explicitly via BuildBVH(), or lazily on the first
    // SubmitRenderables when not yet built.  See WorldBVH for the
    // current scope and capabilities.
    WorldBVH m_WorldBVH;
    mutable std::unordered_set<XSceneNode*> m_VisibleNodeScratch; // reused per frame

    // BVH over per-light influence volumes.  Built alongside the
    // WorldBVH (same lifecycle rules); consumed by SubmitRenderables
    // to compute a frustum-culled visible-light list that gets pushed
    // to the render queue via SetVisibleLights.  See LightBVH for
    // tracked vs untracked light handling and the per-light
    // influence-AABB derivation.
    LightBVH m_LightBVH;
    mutable std::vector<XLight*> m_VisibleTrackedLightsScratch;
    mutable std::vector<XLight*> m_VisibleLightsScratch;

    // Background storage.  Every world has a background; default-constructed
    // GfxBackgroundDesc is opaque black with no cubemap.  pSkyboxCubemapA/B
    // raw observer pointers in m_Background are kept alive by the strong
    // refs m_pSkyboxCubemapA/B; setter populates both together.
    GfxBackgroundDesc                 m_Background;
    Gem::TGemPtr<XGfxSurface>         m_pSkyboxCubemapA;
    Gem::TGemPtr<XGfxSurface>         m_pSkyboxCubemapB;

    // The world's UI graphs, in creation order, which is their draw order.
    std::vector<Gem::TGemPtr<XUIGraph>> m_UIGraphs;

    // The world's world objects, active ones first: [0, m_ActiveObjectCount) are active and the
    // rest inactive.  Each object stores its index here, so activating or deactivating one is a
    // swap across the boundary.  Each refers back to the world weakly, cleared in Uninitialize.
    std::vector<Gem::TGemPtr<CWorldObject>> m_WorldObjects;
    size_t m_ActiveObjectCount = 0;

    // The world objects that have unique names, by unique name.  The map stores each unique
    // name, and each object points at its entry's string, which stays in place until the entry
    // is erased, since rehashing moves no element.  The hash is transparent, so a lookup by
    // const char * builds no string.
    struct UniqueNameHash
    {
        using is_transparent = void;
        size_t operator()(std::string_view name) const noexcept { return std::hash<std::string_view>{}(name); }
    };
    std::unordered_map<std::string, CWorldObject *, UniqueNameHash, std::equal_to<>> m_UniqueNames;
    void ReleaseUniqueName(CWorldObject *pObject);

    // Finds pObject among this world's world objects, or returns nullptr.
    CWorldObject *FindObject(XWorldObject *pObject) const;
    void SwapObjects(size_t a, size_t b);
    void EraseObject(size_t index);

    // Clears the active camera, and queues its event, when it is not in this world's scene graph.
    Gem::Result CheckActiveCamera();

    // The events for the next Update to deliver, in the order they happened.  Each event type
    // matches a WorldListener callback, and its payload holds that callback's arguments.
    enum class EventType
    {
        ObjectOrphaned,     // pObject is the orphan
        ActiveCameraLost,   // no payload
    };
    struct Event
    {
        EventType type;
        Gem::TGemPtr<CWorldObject> pObject;
    };
    std::vector<Event> m_Events;

    // Registered listeners.  A listener unregistered while callbacks are being delivered is set
    // to nullptr, and the list is compacted after delivery.
    std::vector<WorldListener *> m_Listeners;
    bool m_Delivering = false;
    Gem::Result DeliverEvents();

public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XNamedElement)
        GEM_INTERFACE_ENTRY(XCanvasElement)
        GEM_INTERFACE_ENTRY(XWorld)
    END_GEM_INTERFACE_MAP()

    CWorld(XCanvas *pCanvas, XGfxDevice *pDevice);

    Gem::Result Initialize();
    void Uninitialize();
    
public: // XWorld methods
    GEMMETHOD_(XGfxDevice *, GetDevice)() final
    {
        return m_pDevice.Get();
    }

    GEMMETHOD_(XSceneNode *, GetRootNode)();

    GEMMETHOD(AddWorldObject)(_In_ XWorldObject *pObject) final;
    GEMMETHOD(ActivateObject)(_In_ XWorldObject *pObject) final;
    GEMMETHOD(DeactivateObject)(_In_ XWorldObject *pObject) final;
    GEMMETHOD(PlaceObject)(_In_ XWorldObject *pObject, _In_ XSceneNode *pParent, bool keepWorldTransform) final;
    GEMMETHOD(DetachObject)(_In_ XWorldObject *pObject) final;
    GEMMETHOD(ReleaseWorldObject)(_In_ XWorldObject *pObject) final;
    GEMMETHOD(SetUniqueName)(_In_ XWorldObject *pObject, _In_opt_z_ const char *uniqueName) final;
    GEMMETHOD_(XWorldObject *, FindObject)(_In_opt_z_ const char *uniqueName) final;
    GEMMETHOD(RegisterListener)(_In_ WorldListener *pListener) final;
    GEMMETHOD(UnregisterListener)(_In_ WorldListener *pListener) final;

    GEMMETHOD_(void, SetActiveCamera)(XCamera *pCamera) final
    {
        m_pActiveCamera = pCamera;
    }

    GEMMETHOD_(XCamera *, GetActiveCamera)() const final
    {
        return m_pActiveCamera.Get();
    }

    GEMMETHOD_(void, SetBackground)(const GfxBackgroundDesc *pDesc) final
    {
        if (pDesc)
        {
            m_Background      = *pDesc;
            m_pSkyboxCubemapA = pDesc->pSkyboxCubemapA;
            m_pSkyboxCubemapB = pDesc->pSkyboxCubemapB;
        }
        else
        {
            m_Background      = {};
            m_pSkyboxCubemapA = nullptr;
            m_pSkyboxCubemapB = nullptr;
        }
    }

    GEMMETHOD_(const GfxBackgroundDesc *, GetBackground)() const final
    {
        return &m_Background;
    }

    GEMMETHOD(CreateUIGraph)(XUIGraph **ppGraph) final;
    GEMMETHOD(RemoveUIGraph)(XUIGraph *pGraph) final;

    GEMMETHOD(Update)(float dtime) final;

    GEMMETHOD(SubmitRenderables)(XGfxRenderQueue *pRenderQueue) final;

    GEMMETHOD_(void, BuildBVH)() final
    {
        m_WorldBVH.Build(m_pRoot.Get());
        m_LightBVH.Build(m_pRoot.Get());
    }
};

}