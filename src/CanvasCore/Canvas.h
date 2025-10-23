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

extern std::unique_ptr<QLog::Logger> g_pLogger; 

//------------------------------------------------------------------------------------------------
class CCanvasPluginLoader : public Gem::TGeneric<XCanvasPluginLoader>
{
public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XCanvasPluginLoader)
    END_GEM_INTERFACE_MAP()

public:
    CCanvasPluginLoader(const char* path)
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

    ~CCanvasPluginLoader()
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
    GEMMETHOD(LoadPlugin)(XCanvasPlugin **ppPlugin) final
    {
        CFunctionSentinel Sentinel("XCanvasPluginLoader::LoadPlugin");

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

    Gem::Result Initialize()
    {
        return Gem::Result::Success;
    }

    void Uninitialize()
    {
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

    CTimer m_FrameTimer;
    UINT64 m_FrameEndTimeLast = 0;

    // Database of active XCanvasElement objects
    std::unordered_set<XCanvasElement *> m_ActiveCanvasElements;

public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XCanvas)
    END_GEM_INTERFACE_MAP()

    CCanvas()
    {}

    ~CCanvas();

public:
    // XCanvas methods
    GEMMETHOD(RegisterElement)(XCanvasElement *) final;
    GEMMETHOD(UnregisterElement)(XCanvasElement *) final;

    GEMMETHOD(CreatePluginLoader)(PCSTR path, XCanvasPluginLoader **ppPluginLoader) final;
    GEMMETHOD(CreateScene)(XScene **ppScene, PCSTR name = nullptr) final;
    GEMMETHOD(CreateSceneGraphNode)(XSceneGraphNode **ppNode, PCSTR name = nullptr) final;
    GEMMETHOD(CreateCamera)(XCamera **ppCamera, PCSTR name = nullptr) final;
    GEMMETHOD(CreateLight)(LightType type, XLight **ppLight, PCSTR name = nullptr) final;

public:
    // CCanvas methods
    
    // Internal methods used by TCanvasElement - do not call directly from external code
    Gem::Result RegisterElementInternal(XCanvasElement *pElement);
    Gem::Result UnregisterElementInternal(XCanvasElement *pElement);
    
    template<class _Type, typename... Args>
    Gem::Result CreateElement(typename _Type::BaseType **ppElement, PCSTR name, Args... args)
    {
        CFunctionSentinel Sentinel("XCanvas::CreateElement");

        if (!ppElement)
        {
            return Gem::Result::BadPointer;
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

    static CCanvas *CastFrom(XCanvas *pXCanvas) { return static_cast<CCanvas *>(pXCanvas); }

    Gem::Result Initialize();
    virtual void Uninitialize() override;
};

}