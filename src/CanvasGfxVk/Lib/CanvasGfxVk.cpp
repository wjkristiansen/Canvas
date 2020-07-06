//================================================================================================
// CanvasGfxVk
//================================================================================================

#include "pch.h"
#include "CanvasGfxVk.h"
#include "InstanceVk.h"

using namespace Canvas;
using namespace Gem;

//------------------------------------------------------------------------------------------------
// Define vulkan globals
FOR_EACH_VK_DEVICE_EXTENSION_FUNC(DEFINE_VK_GLOBAL_FUNC)

PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr = nullptr;
PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;

//------------------------------------------------------------------------------------------------
VkFormat CanvasFormatToVkFormat(Canvas::GfxFormat Fmt)
{
    switch (Fmt)
    {
    case GfxFormat::Unknown: return VkFormat::VK_FORMAT_UNDEFINED;
    case GfxFormat::R32G32B32A32_Float: return VkFormat::VK_FORMAT_R32G32B32A32_SFLOAT;
    case GfxFormat::R32G32B32A32_UInt: return VkFormat::VK_FORMAT_R32G32B32A32_UINT;
    case GfxFormat::R32G32B32A32_Int: return VkFormat::VK_FORMAT_R32G32B32A32_SINT;
    case GfxFormat::R32G32B32_Float: return VkFormat::VK_FORMAT_R32G32B32_SFLOAT;
    case GfxFormat::R32G32B32_UInt: return VkFormat::VK_FORMAT_R32G32B32_UINT;
    case GfxFormat::R32G32B32_Int: return VkFormat::VK_FORMAT_R32G32B32_SINT;
    case GfxFormat::R32G32_Float: return VkFormat::VK_FORMAT_R32G32_SFLOAT;
    case GfxFormat::R32G32_UInt: return VkFormat::VK_FORMAT_R32G32_UINT;
    case GfxFormat::R32G32_Int: return VkFormat::VK_FORMAT_R32G32_SINT;
    case GfxFormat::D32_Float: return VkFormat::VK_FORMAT_D32_SFLOAT;
    case GfxFormat::R32_Float: return VkFormat::VK_FORMAT_R32_SFLOAT;
    case GfxFormat::R32_UInt: return VkFormat::VK_FORMAT_R32_UINT;
    case GfxFormat::R32_Int: return VkFormat::VK_FORMAT_R32_SINT;
    case GfxFormat::R16G16B16A16_Float: return VkFormat::VK_FORMAT_R16G16B16A16_SFLOAT;
    case GfxFormat::R16G16B16A16_UInt: return VkFormat::VK_FORMAT_R16G16B16A16_UINT;
    case GfxFormat::R16G16B16A16_Int: return VkFormat::VK_FORMAT_R16G16B16A16_SINT;
    case GfxFormat::R16G16B16A16_UNorm: return VkFormat::VK_FORMAT_R16G16B16A16_UNORM;
    case GfxFormat::R16G16B16A16_Norm: return VkFormat::VK_FORMAT_R16G16B16A16_SNORM;
    case GfxFormat::R16G16_Float: return VkFormat::VK_FORMAT_R16G16_SFLOAT;
    case GfxFormat::R16G16_UInt: return VkFormat::VK_FORMAT_R16G16_UINT;
    case GfxFormat::R16G16_Int: return VkFormat::VK_FORMAT_R16G16_SINT;
    case GfxFormat::R16G16_UNorm: return VkFormat::VK_FORMAT_R16G16_UNORM;
    case GfxFormat::R16G16_Norm: return VkFormat::VK_FORMAT_R16G16_SNORM;
    case GfxFormat::R16_Float: return VkFormat::VK_FORMAT_R16_SFLOAT;
    case GfxFormat::R16_UInt: return VkFormat::VK_FORMAT_R16_UINT;
    case GfxFormat::R16_Int: return VkFormat::VK_FORMAT_R16_SINT;
    case GfxFormat::D16_UNorm: return VkFormat::VK_FORMAT_D16_UNORM;
    case GfxFormat::R16_UNorm: return VkFormat::VK_FORMAT_R16_UNORM;
    case GfxFormat::R16_Norm: return VkFormat::VK_FORMAT_R16_SNORM;
    case GfxFormat::D24_Unorm_S8_Uint: return VkFormat::VK_FORMAT_D24_UNORM_S8_UINT;
    case GfxFormat::R24_Unorm_X8: return VkFormat::VK_FORMAT_D24_UNORM_S8_UINT;
    case GfxFormat::X24_S8_UInt: return VkFormat::VK_FORMAT_D24_UNORM_S8_UINT;
    case GfxFormat::R10G10B10A2_UNorm: return VkFormat::VK_FORMAT_R8G8B8A8_UNORM;
    case GfxFormat::R10G10B10A2_UInt: return VkFormat::VK_FORMAT_R8G8B8A8_UINT;
    case GfxFormat::R8G8B8A8_UNorm: return VkFormat::VK_FORMAT_R8G8B8A8_UNORM;
    case GfxFormat::R8G8B8A8_UInt: return VkFormat::VK_FORMAT_R8G8B8A8_UINT;
    case GfxFormat::R8G8B8A8_Norm: return VkFormat::VK_FORMAT_R8G8B8A8_SNORM;
    case GfxFormat::R8G8B8A8_Int: return VkFormat::VK_FORMAT_R8G8B8A8_SINT;
    case GfxFormat::R8G8B8_UNorm: return VkFormat::VK_FORMAT_R8G8_UNORM;
    case GfxFormat::R8G8B8_UInt: return VkFormat::VK_FORMAT_R8G8_UINT;
    case GfxFormat::R8G8B8_Norm: return VkFormat::VK_FORMAT_R8G8_SNORM;
    case GfxFormat::R8G8B8_Int: return VkFormat::VK_FORMAT_R8G8_SINT;
    case GfxFormat::BC1_UNorm: return VkFormat::VK_FORMAT_BC1_RGB_UNORM_BLOCK;
    case GfxFormat::BC2_UNorm: return VkFormat::VK_FORMAT_BC2_UNORM_BLOCK;
    case GfxFormat::BC3_UNorm: return VkFormat::VK_FORMAT_BC3_UNORM_BLOCK;
    case GfxFormat::BC4_UNorm: return VkFormat::VK_FORMAT_BC4_UNORM_BLOCK;
    case GfxFormat::BC4_Norm: return VkFormat::VK_FORMAT_BC4_SNORM_BLOCK;
    case GfxFormat::BC5_UNorm: return VkFormat::VK_FORMAT_BC5_UNORM_BLOCK;
    case GfxFormat::BC5_Norm: return VkFormat::VK_FORMAT_BC5_SNORM_BLOCK;
    case GfxFormat::BC7_UNorm: return VkFormat::VK_FORMAT_BC7_UNORM_BLOCK;
    default: return VkFormat::VK_FORMAT_UNDEFINED;
    }
}

QLog::CBasicLogger g_Logger(nullptr, nullptr);

//------------------------------------------------------------------------------------------------
Result GEMAPI CreateGfxInstance(_Outptr_result_nullonfailure_ XGfxInstance **ppGfxInstance, QLog::CLogClient *pLogClient)
{
    try
    {
        TGemPtr<CInstanceVk> pInstance = CInstanceVk::GetSingleton();
        if (nullptr == pInstance)
        {
            pInstance = new TGeneric<CInstanceVk>(pLogClient); // throw(bad_alloc), throw(GemError)
            ThrowGemError(pInstance->Initialize());
        }
        return pInstance->QueryInterface(ppGfxInstance);
    }
    catch (GemError &e)
    {
        return e.Result();
    }
    catch (std::bad_alloc &)
    {
        return Result::OutOfMemory;
    }
}
