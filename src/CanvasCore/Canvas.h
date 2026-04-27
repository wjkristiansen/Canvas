//================================================================================================
// Canvas
//================================================================================================

#pragma once

#include "CanvasCore.h"
#include "CanvasGfx.h"
#include "Gem.hpp"
#include "Timer.h"

namespace Canvas
{

//------------------------------------------------------------------------------------------------
class CCanvasPluginModule
{
public:
    CCanvasPluginModule(const char* path)
        : m_path(path)
    {
#if defined(_WIN32)
        m_handle = LoadLibraryA(path);
        if (!m_handle)
        {
            throw Gem::GemError(Gem::Result::PluginLoadFailed);
        }
#else
        m_handle = dlopen(path, RTLD_LAZY | RTLD_LOCAL);
        if (!m_handle)
        {
            throw Gem::GemError(Gem::Result::PluginLoadFailed);
        }
#endif
    }

    ~CCanvasPluginModule()
    {
#if defined(_WIN32)
        if (m_handle) FreeLibrary(m_handle);
#else
        if (m_handle) dlclose(m_handle);
#endif
    }

    template<typename T>
    T GetProc(const char* name) const
    {
#if defined(_WIN32)
        auto* proc = GetProcAddress(m_handle, name);
#else
        auto* proc = dlsym(m_handle, name);
#endif
        return reinterpret_cast<T>(proc);
    }

public:
    Gem::Result LoadPlugin(XCanvasPlugin **ppPlugin)
    {
        if (!ppPlugin)
        {
            return Gem::Result::BadPointer;
        }

        auto pfnCreatePlugin = GetProc<FnCreateCanvasPlugin>("CreateCanvasPlugin");
        if (!pfnCreatePlugin)
        {
            return Gem::Result::PluginProcNotFound;
        }

        try
        {
            Gem::TGemPtr<XCanvasPlugin> pPlugin;
            Gem::ThrowGemError(pfnCreatePlugin(&pPlugin));
            *ppPlugin = pPlugin.Detach();
        }
        catch (const Gem::GemError& e)
        {
            return e.Result();
        }

        return Gem::Result::Success;
    }

protected:
#if defined(_WIN32)
    HMODULE m_handle = nullptr;
#else
    void* m_handle = nullptr;
#endif
    std::string m_path;
};

//------------------------------------------------------------------------------------------------
class CCanvas :
    public Gem::TGeneric<XCanvas>
{
    std::mutex m_Mutex;

    Gem::TGemPtr<XLogger> m_pLogger;
    CTimer m_FrameTimer;
    UINT64 m_FrameEndTimeLast = 0;

    // Database of active XCanvasElement objects
    std::unordered_set<XCanvasElement *> m_ActiveCanvasElements;

    std::deque<CCanvasPluginModule> m_PluginModules;

public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XCanvas)
    END_GEM_INTERFACE_MAP()

    CCanvas(XLogger* pLogger)
        : m_pLogger(pLogger)
    {}

    ~CCanvas();

public:
    // XCanvas methods
    GEMMETHOD(RegisterElement)(XCanvasElement *) final;
    GEMMETHOD(UnregisterElement)(XCanvasElement *) final;

    GEMMETHOD(LoadPlugin)(PCSTR path, XCanvasPlugin **ppPlugin) final;
    GEMMETHOD(CreateSceneGraph)(XGfxDevice *pDevice, XSceneGraph **ppScene, PCSTR name = nullptr) final;
    GEMMETHOD(CreateSceneGraphNode)(XSceneGraphNode **ppNode, PCSTR name = nullptr) final;
    GEMMETHOD(CreateCamera)(XCamera **ppCamera, PCSTR name = nullptr) final;
    GEMMETHOD(CreateLight)(LightType type, XLight **ppLight, PCSTR name = nullptr) final;
    GEMMETHOD(CreateMeshInstance)(XMeshInstance **ppMeshInstance, PCSTR name = nullptr) final;
    GEMMETHOD(CreateModel)(XGfxDevice *pDevice, XModel **ppModel, PCSTR name = nullptr) final;

    // Text/UI factory methods
    GEMMETHOD(CreateFont)(const uint8_t* pTTFData, size_t dataSize, PCSTR name, XFont** ppFont) final;
    GEMMETHOD(CreateUIGraph)(XGfxDevice* pDevice, XUIGraph** ppGraph) final;
    GEMMETHOD(CreateTextElement)(XGfxSurface* pAtlasSurface, XUITextElement** ppElement) final;
    GEMMETHOD(CreateRectElement)(XUIRectElement** ppElement) final;

    GEMMETHOD_(XLogger *, GetLogger)() final
    {
        return m_pLogger;
    }

public:
    // CCanvas methods
    template<class _Type, typename... Args>
    Gem::Result CreateElement(typename _Type::BaseType **ppElement, PCSTR name, Args... args)
    {
        if (!ppElement)
        {
            return Gem::Result::BadPointer;
        }

        if (!name || !name[0])
        {
            Canvas::LogWarn(GetLogger(), "CCanvas::CreateElement<%s>: unnamed object created", _Type::BaseType::XFaceName);
        }

        try
        {
            Gem::TGemPtr<_Type> pObj;
            Gem::ThrowGemError(TCanvasElement<_Type>::CreateAndRegister(&pObj, this, name, args...));
            
            *ppElement = pObj.Detach();
        }
        catch(const Gem::GemError &e)
        {
            return e.Result();
        }

        return Gem::Result::Success;
    }

    Gem::Result Initialize();
    virtual void Uninitialize() override;
};

}