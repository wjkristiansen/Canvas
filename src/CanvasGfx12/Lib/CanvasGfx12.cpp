//================================================================================================
// Instance12
//================================================================================================

#include "pch.h"

namespace Canvas
{

//------------------------------------------------------------------------------------------------
DXGI_FORMAT CanvasFormatToDXGIFormat(Canvas::GfxFormat Fmt)
{
    switch (Fmt)
    {
    case GfxFormat::Unknown: return DXGI_FORMAT_UNKNOWN;
    case GfxFormat::R32G32B32A32_Float: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case GfxFormat::R32G32B32A32_UInt: return DXGI_FORMAT_R32G32B32A32_UINT;
    case GfxFormat::R32G32B32A32_Int: return DXGI_FORMAT_R32G32B32A32_SINT;
    case GfxFormat::R32G32B32_Float: return DXGI_FORMAT_R32G32B32_FLOAT;
    case GfxFormat::R32G32B32_UInt: return DXGI_FORMAT_R32G32B32_UINT;
    case GfxFormat::R32G32B32_Int: return DXGI_FORMAT_R32G32B32_SINT;
    case GfxFormat::R32G32_Float: return DXGI_FORMAT_R32G32_FLOAT;
    case GfxFormat::R32G32_UInt: return DXGI_FORMAT_R32G32_UINT;
    case GfxFormat::R32G32_Int: return DXGI_FORMAT_R32G32_SINT;
    case GfxFormat::D32_Float: return DXGI_FORMAT_D32_FLOAT;
    case GfxFormat::R32_Float: return DXGI_FORMAT_R32_FLOAT;
    case GfxFormat::R32_UInt: return DXGI_FORMAT_R32_UINT;
    case GfxFormat::R32_Int: return DXGI_FORMAT_R32_SINT;
    case GfxFormat::R16G16B16A16_Float: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case GfxFormat::R16G16B16A16_UInt: return DXGI_FORMAT_R16G16B16A16_UINT;
    case GfxFormat::R16G16B16A16_Int: return DXGI_FORMAT_R16G16B16A16_SINT;
    case GfxFormat::R16G16B16A16_UNorm: return DXGI_FORMAT_R16G16B16A16_UNORM;
    case GfxFormat::R16G16B16A16_Norm: return DXGI_FORMAT_R16G16B16A16_SNORM;
    case GfxFormat::R16G16_Float: return DXGI_FORMAT_R16G16_FLOAT;
    case GfxFormat::R16G16_UInt: return DXGI_FORMAT_R16G16_UINT;
    case GfxFormat::R16G16_Int: return DXGI_FORMAT_R16G16_SINT;
    case GfxFormat::R16G16_UNorm: return DXGI_FORMAT_R16G16_UNORM;
    case GfxFormat::R16G16_Norm: return DXGI_FORMAT_R16G16_SNORM;
    case GfxFormat::R16_Float: return DXGI_FORMAT_R16_FLOAT;
    case GfxFormat::R16_UInt: return DXGI_FORMAT_R16_UINT;
    case GfxFormat::R16_Int: return DXGI_FORMAT_R16_SINT;
    case GfxFormat::D16_UNorm: return DXGI_FORMAT_D16_UNORM;
    case GfxFormat::R16_UNorm: return DXGI_FORMAT_R16_UNORM;
    case GfxFormat::R16_Norm: return DXGI_FORMAT_R16_SNORM;
    case GfxFormat::D32_Float_S8_UInt_X24: return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    case GfxFormat::R32_Float_X32: return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    case GfxFormat::D24_Unorm_S8_Uint: return DXGI_FORMAT_D24_UNORM_S8_UINT;
    case GfxFormat::R24_Unorm_X8: return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    case GfxFormat::X24_S8_UInt: return DXGI_FORMAT_X24_TYPELESS_G8_UINT;
    case GfxFormat::R10G10B10A2_UNorm: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case GfxFormat::R10G10B10A2_UInt: return DXGI_FORMAT_R8G8B8A8_UINT;
    case GfxFormat::R8G8B8A8_UNorm: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case GfxFormat::R8G8B8A8_UInt: return DXGI_FORMAT_R8G8B8A8_UINT;
    case GfxFormat::R8G8B8A8_Norm: return DXGI_FORMAT_R8G8B8A8_SNORM;
    case GfxFormat::R8G8B8A8_Int: return DXGI_FORMAT_R8G8B8A8_SINT;
    case GfxFormat::R8G8B8_UNorm: return DXGI_FORMAT_R8G8_UNORM;
    case GfxFormat::R8G8B8_UInt: return DXGI_FORMAT_R8G8_UINT;
    case GfxFormat::R8G8B8_Norm: return DXGI_FORMAT_R8G8_SNORM;
    case GfxFormat::R8G8B8_Int: return DXGI_FORMAT_R8G8_SINT;
    case GfxFormat::BC1_UNorm: return DXGI_FORMAT_BC1_UNORM;
    case GfxFormat::BC2_UNorm: return DXGI_FORMAT_BC2_UNORM;
    case GfxFormat::BC3_UNorm: return DXGI_FORMAT_BC3_UNORM;
    case GfxFormat::BC4_UNorm: return DXGI_FORMAT_BC4_UNORM;
    case GfxFormat::BC4_Norm: return DXGI_FORMAT_BC4_SNORM;
    case GfxFormat::BC5_UNorm: return DXGI_FORMAT_BC5_UNORM;
    case GfxFormat::BC5_Norm: return DXGI_FORMAT_BC5_SNORM;
    case GfxFormat::BC7_UNorm: return DXGI_FORMAT_BC7_UNORM;
    default: return DXGI_FORMAT_UNKNOWN;
    }
}

//------------------------------------------------------------------------------------------------
Gem::Result GEMAPI CreateGfxInstance(_Outptr_result_nullonfailure_ XGfxInstance **ppCanvasGfx, std::shared_ptr<QLog::Logger> pLogger)
{
    try
    {
        Gem::TGemPtr<CInstance12> pCanvasGfx = CInstance12::GetSingleton();
        if (nullptr == pCanvasGfx)
        {
            pCanvasGfx = new Gem::TGeneric<CInstance12>(pLogger); // throw(bad_alloc), throw(GemError)
        }
        return pCanvasGfx->QueryInterface(ppCanvasGfx);
    }
    catch (Gem::GemError &e)
    {
        return e.Result();
    }
    catch (std::bad_alloc &)
    {
        return Gem::Result::OutOfMemory;
    }
}

}