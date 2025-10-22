//================================================================================================
// CanvasElement
//================================================================================================

#pragma once

#include "Gem.hpp"
#include "NamedElement.h"

namespace Canvas
{

//------------------------------------------------------------------------------------------------
template<class _Base>
class TCanvasElement :
    public TNamedElement<_Base>
{
protected:
    CCanvas *m_pCanvas = nullptr;

public:
    using BaseType = _Base;

public:
    TCanvasElement() = default;

    TCanvasElement(CCanvas *pCanvas) :
        TNamedElement<_Base>(),
        m_pCanvas(pCanvas)
    {
    }
    
    ~TCanvasElement()
    {
        Unregister();
    }

    // Static helper for creating and auto-registering Canvas elements
    template<typename _Impl, typename... _Args>
    static Gem::Result CreateAndRegister(_Impl **ppElement, Canvas::XCanvas* pCanvas, PCSTR name, _Args&&... args)
    {
        if (!ppElement || !pCanvas) 
            return Gem::Result::BadPointer;

        *ppElement = nullptr;
        
        try
        {
            // Cast XCanvas interface to CCanvas implementation
            CCanvas* pCanvasImpl = CCanvas::CastFrom(pCanvas);
            
            // Create the object using TGenericImpl pattern
            Gem::TGemPtr<_Impl> pElement = new Gem::TGenericImpl<_Impl>(pCanvasImpl, std::forward<_Args>(args)...);
            
            // Set the name if provided
            if (name != nullptr)
            {
                pElement->SetName(name);
            }
            
            // Auto-register with Canvas
            Gem::ThrowGemError(pCanvas->RegisterElement(pElement.Get()));
            
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
    
    // XCanvasElement methods
    GEMMETHOD_(XCanvas *, GetCanvas)() { return m_pCanvas; }
    GEMMETHOD_(PCSTR, GetTypeName)() { return _Base::XFaceName; }
    GEMMETHOD(Register)(XCanvas *pCanvas)
    {
        if (!pCanvas)
            return Gem::Result::BadPointer;
            
        // Unregister from current canvas first
        Unregister();
        
        // Set the canvas and register
        m_pCanvas = CCanvas::CastFrom(pCanvas);
        return m_pCanvas->RegisterElementInternal(this);
    }

    GEMMETHOD(Unregister)()
    {
        if(m_pCanvas)
        {
            auto result = m_pCanvas->UnregisterElementInternal(this);
            m_pCanvas = nullptr;
            return result;
        }
        return Gem::Result::Success;
    }
};

}