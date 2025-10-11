//================================================================================================
// Canvas
//================================================================================================

#pragma once

#include "CanvasCore.h"
#include "Gem.hpp"
#include "Timer.h"

namespace Canvas
{

extern std::unique_ptr<QLog::Logger> g_pLogger; 

//------------------------------------------------------------------------------------------------
class CCanvasPlugin
{
public:
    CCanvasPlugin(const char* path)
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

    ~CCanvasPlugin()
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
    UINT m_FrameCounter = 0;

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
    // XGeneric methods
    GEMMETHOD(Initialize)() final;
    GEMMETHOD_(void, Uninitialize)() final;

public:
    // XCanvas methods
    GEMMETHOD(InitGfx)(PCSTR path) final;
    GEMMETHOD(CreateGfxDevice)(XGfxDevice **ppGfxDevice) final;
    GEMMETHOD(FrameTick)() final;

    GEMMETHOD(CreateScene)(XScene **ppScene) final;
    GEMMETHOD(CreateSceneGraphNode)(XSceneGraphNode **ppNode) final;
    GEMMETHOD(CreateCamera)(XCamera **ppCamera) final;
    GEMMETHOD(CreateLight)(XLight **ppLight) final;

public:
    // CCanvas methods
    template<class _Type>
    Gem::Result CreateElement(typename _Type::BaseType **ppElement);

    void CanvasElementDestroyed(XCanvasElement *pElement);

public:
    // IMPORTANT: m_pGfxPlugin must be declared FIRST so it destructs LAST
    // This ensures the DLL stays loaded while the graphics objects are being destroyed
    std::unique_ptr<CCanvasPlugin> m_pGfxPlugin;
    Gem::TGemPtr<XGfxDeviceFactory> m_pGfxDeviceFactory;
};

}