//================================================================================================
// GraphicsContextVk
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
class CCommandBufferManager
{
protected:
    UniqueVkCommandPool m_VkCommandPool;
    struct VkCommandBufferEntry
    {
        VkCommandBufferEntry(VkCommandBuffer cb) :
            vkCommandBuffer(cb) {}
        VkCommandBufferEntry(VkCommandBuffer cb, const std::shared_ptr<CFenceVk> &pf) :
            vkCommandBuffer(cb),
            pFence(pf) {}
        VkCommandBuffer vkCommandBuffer;
        std::shared_ptr<CFenceVk> pFence;
    };

    std::deque<VkCommandBufferEntry> m_CommandBuffers;
    uint32_t m_QueueFamilyIndex = 0;

    Gem::Result Initialize(VkDevice vkDevice, uint32_t NumBuffers, uint32_t QueueFamilyIndex);

    CCommandBufferManager() = default;
    ~CCommandBufferManager();

public:
    // Unacquires the next available command buffer
    VkCommandBuffer AcquireCommandBuffer(); // throw(Gem::GemError)
    void UnacquireCommandBuffer(VkCommandBuffer vkCommandBuffer, const std::shared_ptr<CFenceVk> &pFence);
};

//------------------------------------------------------------------------------------------------
class CGraphicsContextVk :
    public Canvas::XGfxGraphicsContext,
    public Gem::CGenericBase,
    public CCommandBufferManager
{
public:
    std::mutex m_mutex;
    class CDeviceVk *m_pDevice; // Weak pointer
    VkQueue m_VkQueue = VK_NULL_HANDLE;
    VkCommandBuffer m_VkCommandBuffer = VK_NULL_HANDLE;
    uint32_t m_QueueFamilyIndex = 0;

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

    VkDevice GetVkDevice() const { return m_pDevice ? m_pDevice->m_VkDevice.Get() : VK_NULL_HANDLE; }
};