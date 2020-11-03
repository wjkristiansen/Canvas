//================================================================================================
// InstanceVk
//================================================================================================

#pragma once

//------------------------------------------------------------------------------------------------
class CInstanceVk :
    public Canvas::XGfxInstance,
    public Gem::CGenericBase
{
    QLog::CBasicLogger m_Logger;
    VkDebugUtilsMessengerEXT m_vkMessenger = VK_NULL_HANDLE;
    static CInstanceVk *m_pThis;
#ifdef _DEBUG
      const bool m_EnableValidationLayers = true;  
#else
      const bool m_EnableValidationLayers = false;  
#endif

public:

    wil::unique_hmodule m_VkModule = NULL;
    VkInstance m_VkInstance = VK_NULL_HANDLE;
    CInstanceVk(QLog::CLogClient *pLogClient);
    ~CInstanceVk();

    QLog::CBasicLogger &Logger() { return m_Logger; }

    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(Canvas::XGfxInstance)
    END_GEM_INTERFACE_MAP()

    static CInstanceVk *GetSingleton() { return m_pThis; }
    
    Gem::Result Initialize();

    bool IsValidateLayersEnabled() const { return m_EnableValidationLayers; }
    
    // XGfxInstance methods
    GEMMETHOD(CreateGfxDevice)(Canvas::XGfxDevice **ppDevice);

    static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
        void *pUserData
    );
};

