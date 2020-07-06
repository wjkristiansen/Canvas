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
    static CInstanceVk *m_pThis;

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
    
    // XGfxInstance methods
    GEMMETHOD(CreateGfxDevice)(Canvas::XGfxDevice **ppDevice);
};

