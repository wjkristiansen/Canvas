//================================================================================================
// Instance12
//================================================================================================

#include "pch.h"

#include "Canvas.h"
#include "CanvasGfx12.h"

#if defined(_WIN32)
    #if defined(CANVASGFX12_EXPORTS)
        #define CANVASGFX12_API __declspec(dllexport)
    #else
        #define CANVASGFX12_API __declspec(dllimport)
    #endif
#elif defined(__GNUC__) || defined(__clang__)
    #define CANVASGFX12_API __attribute__((visibility("default")))
#else
    #define CANVASGFX12_API
#endif

//------------------------------------------------------------------------------------------------
DXGI_FORMAT CanvasFormatToDXGIFormat(Canvas::GfxFormat Fmt)
{
    switch (Fmt)
    {
    case Canvas::GfxFormat::Unknown: return DXGI_FORMAT_UNKNOWN;
    case Canvas::GfxFormat::R32G32B32A32_Float: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case Canvas::GfxFormat::R32G32B32A32_UInt: return DXGI_FORMAT_R32G32B32A32_UINT;
    case Canvas::GfxFormat::R32G32B32A32_Int: return DXGI_FORMAT_R32G32B32A32_SINT;
    case Canvas::GfxFormat::R32G32B32_Float: return DXGI_FORMAT_R32G32B32_FLOAT;
    case Canvas::GfxFormat::R32G32B32_UInt: return DXGI_FORMAT_R32G32B32_UINT;
    case Canvas::GfxFormat::R32G32B32_Int: return DXGI_FORMAT_R32G32B32_SINT;
    case Canvas::GfxFormat::R32G32_Float: return DXGI_FORMAT_R32G32_FLOAT;
    case Canvas::GfxFormat::R32G32_UInt: return DXGI_FORMAT_R32G32_UINT;
    case Canvas::GfxFormat::R32G32_Int: return DXGI_FORMAT_R32G32_SINT;
    case Canvas::GfxFormat::D32_Float: return DXGI_FORMAT_D32_FLOAT;
    case Canvas::GfxFormat::R32_Float: return DXGI_FORMAT_R32_FLOAT;
    case Canvas::GfxFormat::R32_UInt: return DXGI_FORMAT_R32_UINT;
    case Canvas::GfxFormat::R32_Int: return DXGI_FORMAT_R32_SINT;
    case Canvas::GfxFormat::R16G16B16A16_Float: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case Canvas::GfxFormat::R16G16B16A16_UInt: return DXGI_FORMAT_R16G16B16A16_UINT;
    case Canvas::GfxFormat::R16G16B16A16_Int: return DXGI_FORMAT_R16G16B16A16_SINT;
    case Canvas::GfxFormat::R16G16B16A16_UNorm: return DXGI_FORMAT_R16G16B16A16_UNORM;
    case Canvas::GfxFormat::R16G16B16A16_Norm: return DXGI_FORMAT_R16G16B16A16_SNORM;
    case Canvas::GfxFormat::R16G16_Float: return DXGI_FORMAT_R16G16_FLOAT;
    case Canvas::GfxFormat::R16G16_UInt: return DXGI_FORMAT_R16G16_UINT;
    case Canvas::GfxFormat::R16G16_Int: return DXGI_FORMAT_R16G16_SINT;
    case Canvas::GfxFormat::R16G16_UNorm: return DXGI_FORMAT_R16G16_UNORM;
    case Canvas::GfxFormat::R16G16_Norm: return DXGI_FORMAT_R16G16_SNORM;
    case Canvas::GfxFormat::R16_Float: return DXGI_FORMAT_R16_FLOAT;
    case Canvas::GfxFormat::R16_UInt: return DXGI_FORMAT_R16_UINT;
    case Canvas::GfxFormat::R16_Int: return DXGI_FORMAT_R16_SINT;
    case Canvas::GfxFormat::D16_UNorm: return DXGI_FORMAT_D16_UNORM;
    case Canvas::GfxFormat::R16_UNorm: return DXGI_FORMAT_R16_UNORM;
    case Canvas::GfxFormat::R16_Norm: return DXGI_FORMAT_R16_SNORM;
    case Canvas::GfxFormat::D32_Float_S8_UInt_X24: return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    case Canvas::GfxFormat::R32_Float_X32: return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    case Canvas::GfxFormat::D24_Unorm_S8_Uint: return DXGI_FORMAT_D24_UNORM_S8_UINT;
    case Canvas::GfxFormat::R24_Unorm_X8: return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    case Canvas::GfxFormat::X24_S8_UInt: return DXGI_FORMAT_X24_TYPELESS_G8_UINT;
    case Canvas::GfxFormat::R10G10B10A2_UNorm: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case Canvas::GfxFormat::R10G10B10A2_UInt: return DXGI_FORMAT_R8G8B8A8_UINT;
    case Canvas::GfxFormat::R11G11B10_Float: return DXGI_FORMAT_R11G11B10_FLOAT;
    case Canvas::GfxFormat::R8G8B8A8_UNorm: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case Canvas::GfxFormat::R8G8B8A8_UInt: return DXGI_FORMAT_R8G8B8A8_UINT;
    case Canvas::GfxFormat::R8G8B8A8_Norm: return DXGI_FORMAT_R8G8B8A8_SNORM;
    case Canvas::GfxFormat::R8G8B8A8_Int: return DXGI_FORMAT_R8G8B8A8_SINT;
    case Canvas::GfxFormat::R8G8B8_UNorm: return DXGI_FORMAT_R8G8_UNORM;
    case Canvas::GfxFormat::R8G8B8_UInt: return DXGI_FORMAT_R8G8_UINT;
    case Canvas::GfxFormat::R8G8B8_Norm: return DXGI_FORMAT_R8G8_SNORM;
    case Canvas::GfxFormat::R8G8B8_Int: return DXGI_FORMAT_R8G8_SINT;
    case Canvas::GfxFormat::R8_UNorm: return DXGI_FORMAT_R8_UNORM;
    case Canvas::GfxFormat::BC1_UNorm: return DXGI_FORMAT_BC1_UNORM;
    case Canvas::GfxFormat::BC2_UNorm: return DXGI_FORMAT_BC2_UNORM;
    case Canvas::GfxFormat::BC3_UNorm: return DXGI_FORMAT_BC3_UNORM;
    case Canvas::GfxFormat::BC4_UNorm: return DXGI_FORMAT_BC4_UNORM;
    case Canvas::GfxFormat::BC4_Norm: return DXGI_FORMAT_BC4_SNORM;
    case Canvas::GfxFormat::BC5_UNorm: return DXGI_FORMAT_BC5_UNORM;
    case Canvas::GfxFormat::BC5_Norm: return DXGI_FORMAT_BC5_SNORM;
    case Canvas::GfxFormat::BC7_UNorm: return DXGI_FORMAT_BC7_UNORM;
    default: return DXGI_FORMAT_UNKNOWN;
    }
}

CCanvasPlugin::CCanvasPlugin()
{
#if defined(_DEBUG)
    bool enableDebugLayer = IsDebuggerPresent() != FALSE;

    // Allow explicit override for local troubleshooting without requiring a debugger.
    // Values: 0 = force off, 1 = force on.
    if (const char *pOverride = std::getenv("CANVAS_D3D12_DEBUG_LAYER"))
    {
        if (pOverride[0] == '0' && pOverride[1] == '\0')
            enableDebugLayer = false;
        else if (pOverride[0] == '1' && pOverride[1] == '\0')
            enableDebugLayer = true;
    }

    if (enableDebugLayer)
    {
        CComPtr<ID3D12Debug3> pDebug;
        ThrowFailedHResult(D3D12GetDebugInterface(IID_PPV_ARGS(&pDebug)));
        pDebug->EnableDebugLayer();
    }
#endif
}

GEMMETHODIMP CCanvasPlugin::CreateCanvasElement(Canvas::XCanvas *pCanvas, uint64_t typeId, const char *name, Gem::InterfaceId iid, void **ppElement)
{
    *ppElement = nullptr;

    try
    {
        switch(typeId)
        {
        case Canvas::TypeId_GfxDevice:
            {
                Gem::TGemPtr<CDevice12> pDevice;
                Gem::ThrowGemError(TGfxElement<CDevice12>::CreateAndRegister(&pDevice, pCanvas, name)); // throw(Gem::GemError)
                return pDevice->QueryInterface(iid, ppElement);
            }
        }
    }
    catch (const std::bad_alloc &)
    {
        return Gem::Result::OutOfMemory;
    }
    catch (const Gem::GemError &e)
    {
        return e.Result();
    }

    return Gem::Result::Success;
}

extern "C" CANVASGFX12_API Gem::Result CreateCanvasPlugin(Canvas::XCanvasPlugin **ppPlugin)
{
    try
    {
        Gem::TGemPtr<CCanvasPlugin> pPlugin = nullptr;
        Gem::ThrowGemError(Gem::TGenericImpl<CCanvasPlugin>::Create(&pPlugin)); // throw(Gem::GemError)
        *ppPlugin = pPlugin.Detach();
    }
    catch(const Gem::GemError &e)
    {
        return e.Result();
    }

    return Gem::Result::Success;
}