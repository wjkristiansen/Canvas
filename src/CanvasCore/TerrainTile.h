//================================================================================================
// TerrainTile
//
// Scene-graph element wrapping a GPU-tessellated heightfield terrain tile.
// Holds non-owning references to a heightmap surface plus a triplet of
// per-tile composite material textures (albedo / AO / roughness). The render
// queue picks up the tile during SubmitForRender and routes it through the
// terrain pipeline.
//================================================================================================

#pragma once

#include "SceneGraph.h"

namespace Canvas
{

//------------------------------------------------------------------------------------------------
class CTerrainTile :
    public TSceneGraphElement<XTerrainTile>
{
    Gem::TGemPtr<XGfxSurface> m_pHeightmap;
    Gem::TGemPtr<XGfxSurface> m_pAlbedo;
    Gem::TGemPtr<XGfxSurface> m_pAOMap;
    Gem::TGemPtr<XGfxSurface> m_pRoughness;

    float    m_OriginX     = 0.0f;
    float    m_OriginY     = 0.0f;
    float    m_WorldSizeX  = 0.0f;
    float    m_WorldSizeY  = 0.0f;
    float    m_HeightScale = 0.0f;
    float    m_HeightBias  = 0.0f;
    uint32_t m_PatchGridDim = 64;

public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XNamedElement)
        GEM_INTERFACE_ENTRY(XCanvasElement)
        GEM_INTERFACE_ENTRY(XSceneGraphElement)
        GEM_INTERFACE_ENTRY(XTerrainTile)
    END_GEM_INTERFACE_MAP()

    CTerrainTile(XCanvas *pCanvas) :
        TSceneGraphElement<XTerrainTile>(pCanvas)
    {}

    Gem::Result Initialize() { return Gem::Result::Success; }
    void Uninitialize() {}

    // XSceneGraphElement
    GEMMETHOD(Update)(float /*dtime*/) final
    {
        return Gem::Result::Success;
    }

    GEMMETHOD(NotifyNodeContextChanged)(_In_ XSceneGraphNode *pNode) final
    {
        return TSceneGraphElement<XTerrainTile>::NotifyNodeContextChanged(pNode);
    }

    // XTerrainTile
    GEMMETHOD_(XGfxSurface *, GetHeightmap)() final
    {
        return m_pHeightmap.Get();
    }

    GEMMETHOD_(void, SetHeightmap)(XGfxSurface *pHeightmap) final
    {
        m_pHeightmap = pHeightmap;
    }

    GEMMETHOD_(void, SetMaterial)(
        XGfxSurface *pAlbedo,
        XGfxSurface *pAOMap,
        XGfxSurface *pRoughnessMap) final
    {
        m_pAlbedo    = pAlbedo;
        m_pAOMap     = pAOMap;
        m_pRoughness = pRoughnessMap;
    }

    GEMMETHOD_(void, GetMaterial)(
        XGfxSurface **ppAlbedo,
        XGfxSurface **ppAOMap,
        XGfxSurface **ppRoughnessMap) const final
    {
        if (ppAlbedo)         *ppAlbedo         = m_pAlbedo.Get();
        if (ppAOMap)          *ppAOMap          = m_pAOMap.Get();
        if (ppRoughnessMap)   *ppRoughnessMap   = m_pRoughness.Get();
    }

    GEMMETHOD_(void, SetExtents)(
        float originX, float originY,
        float worldSizeX, float worldSizeY,
        float heightScale, float heightBias) final
    {
        m_OriginX     = originX;
        m_OriginY     = originY;
        m_WorldSizeX  = worldSizeX;
        m_WorldSizeY  = worldSizeY;
        m_HeightScale = heightScale;
        m_HeightBias  = heightBias;
    }

    GEMMETHOD_(void, GetExtents)(
        float *pOriginX, float *pOriginY,
        float *pWorldSizeX, float *pWorldSizeY,
        float *pHeightScale, float *pHeightBias) const final
    {
        if (pOriginX)     *pOriginX     = m_OriginX;
        if (pOriginY)     *pOriginY     = m_OriginY;
        if (pWorldSizeX)  *pWorldSizeX  = m_WorldSizeX;
        if (pWorldSizeY)  *pWorldSizeY  = m_WorldSizeY;
        if (pHeightScale) *pHeightScale = m_HeightScale;
        if (pHeightBias)  *pHeightBias  = m_HeightBias;
    }

    GEMMETHOD_(void, SetPatchGridDim)(uint32_t dim) final
    {
        m_PatchGridDim = (dim == 0) ? 1u : dim;
    }

    GEMMETHOD_(uint32_t, GetPatchGridDim)() const final
    {
        return m_PatchGridDim;
    }

    // Internal accessors retained for any future engine-internal use.
    XGfxSurface *GetAlbedoInternal()    const { return m_pAlbedo.Get(); }
    XGfxSurface *GetAOMapInternal()     const { return m_pAOMap.Get(); }
    XGfxSurface *GetRoughnessInternal() const { return m_pRoughness.Get(); }
};

}
