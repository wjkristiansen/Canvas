//================================================================================================
// CanvasGfx12
//================================================================================================

#pragma once

extern DXGI_FORMAT CanvasFormatToDXGIFormat(Canvas::GfxFormat Fmt);

//------------------------------------------------------------------------------------------------
inline Gem::Result ResultFromHRESULT(HRESULT hr)
{
    switch (hr)
    {
    case S_OK:
        return Gem::Result::Success;

    case E_FAIL:
        return Gem::Result::Fail;

    case E_OUTOFMEMORY:
        return Gem::Result::OutOfMemory;

    case E_INVALIDARG:
    case DXGI_ERROR_INVALID_CALL:
        return Gem::Result::InvalidArg;

    case DXGI_ERROR_DEVICE_REMOVED:
        // BUGBUG: TODO...
        return Gem::Result::Fail;

    case E_NOINTERFACE:
        return Gem::Result::NoInterface;

    default:
        return Gem::Result::Fail;
    }
}

//================================================================================================
// GfxElement - Template helper for graphics Canvas elements
//================================================================================================

#pragma once

#include <string>
#include "Gem.hpp"
#include "CanvasGfx.h"

//------------------------------------------------------------------------------------------------
// Template helper for graphics objects that need Canvas element functionality
// Usage: class CMyGfxObject : public TGfxElement<XMyGfxInterface>
// This allows external plugins to implement Canvas element functionality
template<class _Base>
class TGfxElement :
    public Gem::TGeneric<_Base>
{
    // Canvas element tracking for external plugins
    Canvas::XCanvas* m_pCanvas = nullptr;
    std::string m_Name;

public:
    using BaseType = _Base;

public:
    TGfxElement() = default;
    ~TGfxElement() { Unregister(); }

    // XCanvasElement interface implementation for external plugins
    GEMMETHOD_(Canvas::XCanvas *, GetCanvas)() { return m_pCanvas; }
    GEMMETHOD_(PCSTR, GetTypeName)() { return _Base::XFaceName; }
    GEMMETHOD_(PCSTR, GetName)() { return m_Name.empty() ? nullptr : m_Name.c_str(); }
    GEMMETHOD_(void, SetName)(PCSTR szName) { m_Name = szName ? szName : ""; }
    
    GEMMETHOD(Register)(Canvas::XCanvas* pCanvas)
    {
        if (!pCanvas) return Gem::Result::BadPointer;
        
        Unregister();
        m_pCanvas = pCanvas;
        
        // External plugin: call canvas->RegisterElement
        return m_pCanvas->RegisterElement(this);
    }

    GEMMETHOD(Unregister)()
    {
        if (m_pCanvas)
        {
            auto result = m_pCanvas->UnregisterElement(this);
            m_pCanvas = nullptr;
            return result;
        }
        return Gem::Result::Success;
    }

    // Static helper for creating and auto-registering Canvas elements
    template<typename _Impl, typename... _Args>
    static Gem::Result CreateAndRegister(_Impl **ppElement, Canvas::XCanvas* pCanvas, _Args&&... args)
    {
        if (!ppElement || !pCanvas) 
            return Gem::Result::BadPointer;

        *ppElement = nullptr;
        
        try
        {
            // Create the object using TGenericImpl pattern
            Gem::TGemPtr<_Impl> pElement = new Gem::TGenericImpl<_Impl>(std::forward<_Args>(args)...);
            
            // Auto-register with Canvas
            Gem::ThrowGemError(pElement->Register(pCanvas));
            
            *ppElement = pElement.Detach();
            return Gem::Result::Success;
        }
        catch (const std::bad_alloc &)
        {
            return Gem::Result::OutOfMemory;
        }
        catch (const Gem::GemError &e)
        {
            return e.Result();
        }
    }
};

class CCanvasPlugin :
    public Gem::TGeneric<Canvas::XCanvasPlugin>
{
public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(Canvas::XCanvasPlugin)
    END_GEM_INTERFACE_MAP()

    CCanvasPlugin();
    Gem::Result Initialize() { return Gem::Result::Success; }
    void Uninitialize() {}

    GEMMETHOD(CreateCanvasElement)(Canvas::XCanvas *pCanvas, uint64_t typeId, const char *name, Gem::InterfaceId iid, void **ppElement) final;
};