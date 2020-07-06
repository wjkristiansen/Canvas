//================================================================================================
// GraphicsContextVk
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
class CGraphicsContextVk :
    public Canvas::XGfxGraphicsContext,
    public Gem::CGenericBase
{
public:
    std::mutex m_mutex;
    class CDeviceVk *m_pDevice; // Weak pointer
    VkQueue m_VkQueue;
    UniqueVkCommandPool m_VkCommandPool;
    VkCommandBuffer m_VkCommandBuffer;
    uint32_t m_QueueFamilyIndex;

    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(Canvas::XGfxGraphicsContext)
    END_GEM_INTERFACE_MAP()

    CGraphicsContextVk(class CDeviceVk *pDevice);

    // XGfxGraphicsContext methods
    GEMMETHOD(CreateSwapChain)(HWND hWnd, bool Windowed, Canvas::XGfxSwapChain **ppSwapChain, Canvas::GfxFormat Format, UINT NumBuffers) final;
    GEMMETHOD_(void, CopyBuffer(Canvas::XGfxBuffer *pDest, Canvas::XGfxBuffer *pSource)) final;
    GEMMETHOD_(void, ClearSurface)(Canvas::XGfxSurface *pSurface, const float Color[4]) final;
    GEMMETHOD(Flush)() final;
    GEMMETHOD(FlushAndPresent)(Canvas::XGfxSwapChain *pSwapChain) final;
    GEMMETHOD(Wait)() final;

    // Internal methods
    Gem::Result Initialize();
    Gem::Result FlushImpl();
};