//================================================================================================
// Buffer12
//================================================================================================

#pragma once

#include "D3D12ResourceUtils.h"
#include "CanvasGfx12.h"

class CDevice12;

//------------------------------------------------------------------------------------------------
class CBuffer12 :
    public TGfxElement<Canvas::XGfxBuffer>,
    public CResource
{
    Gem::TGemPtr<CDevice12> m_pDevice;  // Ref-counted; keeps device (and its allocator) alive
    uint64_t m_AllocationKey = 0;
    uint32_t m_SizeInUnits   = 0;
    uint32_t m_AllocatorTier = 0;

public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(Canvas::XGfxBuffer)
        GEM_INTERFACE_ENTRY(Canvas::XCanvasElement)
        GEM_INTERFACE_ENTRY(Canvas::XNamedElement)
    END_GEM_INTERFACE_MAP()

    CBuffer12(Canvas::XCanvas* pCanvas, ID3D12Resource *pResource, PCSTR name = nullptr)
        : TGfxElement(pCanvas),
          CResource(pResource)
    {
        if (name != nullptr)
            SetName(name);
    }

    ~CBuffer12();

    void SetAllocationTracking(CDevice12* pDevice,
                               uint64_t allocationKey, uint32_t sizeInUnits, uint32_t allocatorTier)
    {
        m_pDevice       = pDevice;  // AddRef — keeps device alive
        m_AllocationKey = allocationKey;
        m_SizeInUnits   = sizeInUnits;
        m_AllocatorTier = allocatorTier;
    }

    Gem::Result Initialize() { return Gem::Result::Success; }    
    void Uninitialize() {}
};
