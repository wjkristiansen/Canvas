//================================================================================================
// CanvasGfx
//================================================================================================

#pragma once
#include "Gem.hpp"
#include "CanvasCore.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324)
#endif

namespace Canvas
{

    enum class GfxFormat : int
    {
        Unknown,
        R32G32B32A32_Float,
        R32G32B32A32_UInt,
        R32G32B32A32_Int,
        R32G32B32_Float,
        R32G32B32_UInt,
        R32G32B32_Int,
        R32G32_Float,
        R32G32_UInt,
        R32G32_Int,
        D32_Float,
        R32_Float,
        R32_UInt,
        R32_Int,
        R16G16B16A16_Float,
        R16G16B16A16_UInt,
        R16G16B16A16_Int,
        R16G16B16A16_UNorm,
        R16G16B16A16_Norm,
        R16G16_Float,
        R16G16_UInt,
        R16G16_Int,
        R16G16_UNorm,
        R16G16_Norm,
        R16_Float,
        R16_UInt,
        R16_Int,
        D16_UNorm,
        R16_UNorm,
        R16_Norm,
        D32_Float_S8_UInt_X24,
        R32_Float_X32,
        D24_Unorm_S8_Uint,
        R24_Unorm_X8,
        X24_S8_UInt,
        R10G10B10A2_UNorm,
        R10G10B10A2_UInt,
        R8G8B8A8_UNorm,
        R8G8B8A8_UInt,
        R8G8B8A8_Norm,
        R8G8B8A8_Int,
        R8G8B8_UNorm,
        R8G8B8_UInt,
        R8G8B8_Norm,
        R8G8B8_Int,
        R8_UNorm,
        BC1_UNorm,
        BC2_UNorm,
        BC3_UNorm,
        BC4_UNorm,
        BC4_Norm,
        BC5_UNorm,
        BC5_Norm,
        BC7_UNorm,
    };

    //------------------------------------------------------------------------------------------------
    enum GfxMemoryUsage
    {
        HostRead,               // CPU-writeable, GPU-readable (e.g. vertex uploads)
        HostWrite,              // GPU-writeable, CPU-readable (e.g. query results)
        DeviceLocal,            // GPU-only access (e.g. textures, render targets)
    };

    //------------------------------------------------------------------------------------------------
    // Buffer resource
    struct XGfxSurface : public XCanvasElement
    {
        GEM_INTERFACE_DECLARE(XGfxSurface, 0x2F05FEAC7133843B);
    };

    //------------------------------------------------------------------------------------------------
    enum class MaterialLayerRole
    {
        Albedo,
        Normal,
        Roughness,
        Metallic,
        Emissive,
    };

    //------------------------------------------------------------------------------------------------
    enum MaterialLayerFlags : uint32_t
    {
        None            = 0,
        Decal           = 1 << 0,  // Projected onto surface
        Tiled           = 1 << 1,  // Repeats across UV space
        LODBias         = 1 << 2,  // Applies mip bias
        UVTransform     = 1 << 3,  // Uses custom UV matrix
        Masked          = 1 << 4,  // Uses alpha mask
    };

    //------------------------------------------------------------------------------------------------
    enum class MaterialBlendMode
    {
        Default,
        Additive,
        Multiply,
        AlphaMasked,
        Overlay,
    };

    //------------------------------------------------------------------------------------------------
    struct MaterialLayer
    {
        MaterialLayerRole Role;
        MaterialLayerFlags Flags;
        Math::FloatVector4 BlendFactor;
        Math::FloatVector4 Color;
        XGfxSurface *pSurface;
    };

    //------------------------------------------------------------------------------------------------
    struct
    XGfxMaterial : public XCanvasElement
    {
        GEM_INTERFACE_DECLARE(XGfxMaterial, 0xD6E17B2CB8454154);
    };

    //------------------------------------------------------------------------------------------------
    enum class GfxVertexBufferType
    {
        Position,           // FloatVector3 array
        Normal,             // FloatVector3 array
        Tangent,            // FloatVector3 array
        Bitangent,          // FloatVector3 array
        SpecularColor,      // FloatVector3 array
        AlbedoColor,        // FloatVector4 array
        AmbientColor,       // FloatVector4 array
        EmissiveColor,      // FloatVector4 array
        U0,                 // Float array
        UV0,                // FloatVector2 array
        UV1,                // FloatVector2 array
        UV2,                // FloatVector2 array
        UV3,                // FloatVector2 array
        UVW0,               // FloatVector3 array
        UVW1,               // FloatVector3 array
        BoneWeights,        // Structured array
    };

    //------------------------------------------------------------------------------------------------
    struct GfxBufferSuballocation
    {
        Gem::TGemPtr<XGfxBuffer> pBuffer;
        uint64_t Offset = 0;
        uint64_t Size = 0;
    };

    //------------------------------------------------------------------------------------------------
    struct
    XGfxMeshData : public XCanvasElement
    {
        GEM_INTERFACE_DECLARE(XGfxMeshData, 0x7EBC2A5A40CC96D3);

        GEMMETHOD_(uint32_t, GetNumMaterialGroups)() = 0;
        GEMMETHOD_(GfxBufferSuballocation *, GetVertexBuffer)(uint32_t materialIndex, GfxVertexBufferType type) = 0;
        GEMMETHOD_(XGfxMaterial *, GetMaterial)(uint32_t materialIndex) = 0;
    };

    //------------------------------------------------------------------------------------------------
    // Buffer resource
    struct XGfxBuffer : public XCanvasElement
    {
        GEM_INTERFACE_DECLARE(XGfxBuffer, 0xA1DF297C8FA4CF13);
    };

    //------------------------------------------------------------------------------------------------
    struct XGfxSwapChain : public XCanvasElement
    {
        GEM_INTERFACE_DECLARE(XGfxSwapChain, 0x1DEDFC0646129850);

        GEMMETHOD(GetSurface)(XGfxSurface **ppSurface) = 0;
        GEMMETHOD(WaitForLastPresent)() = 0;
    };

    //------------------------------------------------------------------------------------------------
    // Submits tasks to the graphics subsystem.
    struct XGfxRenderQueue : public XRenderQueue
    {
        GEM_INTERFACE_DECLARE(XGfxRenderQueue, 0x728AF985153F712D);

        GEMMETHOD(CreateSwapChain)(HWND hWnd, bool Windowed, XGfxSwapChain **ppSwapChain, GfxFormat Format, UINT NumBuffers) = 0;
        GEMMETHOD(FlushAndPresent)(XGfxSwapChain *pSwapChain) = 0;

        // Frame rendering
        GEMMETHOD(BeginFrame)(XGfxSwapChain *pSwapChain) = 0;
        GEMMETHOD(EndFrame)() = 0;
    };

    //================================================================================================
    // UI Graph - Hierarchical UI/HUD elements with dirty tracking
    //================================================================================================

    //------------------------------------------------------------------------------------------------

    struct XGfxUIElement : public Gem::XGeneric
    {
        GEM_INTERFACE_DECLARE(XGfxUIElement, 0xA1B2C3D4E5F60718);

        GEMMETHOD_(UIElementType, GetType)() const = 0;
        GEMMETHOD_(bool, IsVisible)() const = 0;
        GEMMETHOD_(void, SetVisible)(bool visible) = 0;
        GEMMETHOD_(XGfxUIGraphNode*, GetAttachedNode)() = 0;

        // CPU vertex data (for upload)
        GEMMETHOD_(uint32_t, GetVertexCount)() const = 0;
        GEMMETHOD_(const void*, GetVertexData)() const = 0;
        GEMMETHOD_(bool, HasContent)() const = 0;

        // GPU vertex buffer suballocation (assigned by render queue after upload)
        GEMMETHOD_(const GfxBufferSuballocation&, GetVertexBuffer)() const = 0;
        GEMMETHOD_(void, SetVertexBuffer)(const GfxBufferSuballocation& buffer) = 0;
    };

    //------------------------------------------------------------------------------------------------
    struct XGfxUITextElement : public XGfxUIElement
    {
        GEM_INTERFACE_DECLARE(XGfxUITextElement, 0xB2C3D4E5F6071829);

        GEMMETHOD_(void, SetText)(PCSTR utf8Text) = 0;
        GEMMETHOD_(PCSTR, GetText)() const = 0;
        GEMMETHOD_(void, SetFont)(XFont* pFont) = 0;
        GEMMETHOD_(void, SetLayoutConfig)(const TextLayoutConfig& config) = 0;
        GEMMETHOD_(const TextLayoutConfig&, GetLayoutConfig)() const = 0;

        // Atlas texture access for render queue
        GEMMETHOD_(XGfxSurface*, GetGlyphAtlasTexture)() = 0;
    };

    //------------------------------------------------------------------------------------------------
    struct XGfxUIRectElement : public XGfxUIElement
    {
        GEM_INTERFACE_DECLARE(XGfxUIRectElement, 0xC3D4E5F607182930);

        GEMMETHOD_(void, SetSize)(const Math::FloatVector2& size) = 0;
        GEMMETHOD_(const Math::FloatVector2&, GetSize)() const = 0;
        GEMMETHOD_(void, SetFillColor)(const Math::FloatVector4& color) = 0;
        GEMMETHOD_(const Math::FloatVector4&, GetFillColor)() const = 0;
    };

    //------------------------------------------------------------------------------------------------
    struct XGfxUIGraphNode : public Gem::XGeneric
    {
        GEM_INTERFACE_DECLARE(XGfxUIGraphNode, 0xE5F6071829304152);

        GEMMETHOD(AddChild)(_In_ XGfxUIGraphNode* pChild) = 0;
        GEMMETHOD(RemoveChild)(_In_ XGfxUIGraphNode* pChild) = 0;
        GEMMETHOD_(XGfxUIGraphNode*, GetParent)() = 0;
        GEMMETHOD_(XGfxUIGraphNode*, GetFirstChild)() = 0;
        GEMMETHOD_(XGfxUIGraphNode*, GetNextSibling)() = 0;

        GEMMETHOD_(const Math::FloatVector2&, GetLocalPosition)() const = 0;
        GEMMETHOD_(void, SetLocalPosition)(const Math::FloatVector2& position) = 0;
        GEMMETHOD_(Math::FloatVector2, GetGlobalPosition)() = 0;

        GEMMETHOD(BindElement)(_In_ XGfxUIElement* pElement) = 0;
        GEMMETHOD_(UINT, GetBoundElementCount)() = 0;
        GEMMETHOD_(XGfxUIElement*, GetBoundElement)(UINT index) = 0;
    };

    //------------------------------------------------------------------------------------------------
    struct XGfxUIGraph : public Gem::XGeneric
    {
        GEM_INTERFACE_DECLARE(XGfxUIGraph, 0xD4E5F60718293041);

        GEMMETHOD(CreateTextElement)(XGfxUIGraphNode* pNode, XGfxUITextElement** ppElement) = 0;
        GEMMETHOD(CreateRectElement)(XGfxUIGraphNode* pNode, XGfxUIRectElement** ppElement) = 0;
        GEMMETHOD(RemoveElement)(XGfxUIElement* pElement) = 0;
        GEMMETHOD(CreateNode)(XGfxUIGraphNode* pParent, XGfxUIGraphNode** ppNode) = 0;
        GEMMETHOD_(XGfxUIGraphNode*, GetRootNode)() = 0;
        GEMMETHOD(Update)() = 0;
        GEMMETHOD(SubmitRenderables)(XRenderQueue* pRenderQueue) = 0;
    };

    enum GfxSurfaceFlags : uint32_t
    {
        SurfaceFlag_None = 0,
        SurfaceFlag_RenderTarget = 1 << 0,
        SurfaceFlag_DepthStencil = 1 << 1,
        SurfaceFlag_ShaderResource = 1 << 2,
        SurfaceFlag_UnorderedAccess = 1 << 3,
        SurfaceFlag_CpuReadback = 1 << 4,
        SurfaceFlag_CpuUpload = 1 << 5,
    };

    enum class GfxSurfaceDimension : uint32_t
    {
        Dimension1D = 0,
        Dimension2D = 1,
        Dimension3D = 2,
        DimensionCube = 3,
    };

    struct GfxSurfaceDesc
    {
        GfxFormat Format;
        GfxSurfaceDimension Dimension;
        GfxSurfaceFlags Flags;
        UINT Width;
        UINT Height;
        UINT Depth;
        UINT ArraySize;
        UINT MipLevels;

        GfxSurfaceDesc()
            : Format(GfxFormat::Unknown)
            , Dimension(GfxSurfaceDimension::Dimension2D)
            , Flags(SurfaceFlag_None)
            , Width(0)
            , Height(0)
            , Depth(1)
            , ArraySize(1)
            , MipLevels(1)
        {
        }

        static GfxSurfaceDesc SurfaceDesc1D(GfxFormat format, UINT width, GfxSurfaceFlags flags, UINT mipLevels = 1)
        {
            GfxSurfaceDesc desc;
            desc.Format = format;
            desc.Dimension = GfxSurfaceDimension::Dimension1D;
            desc.Flags = flags;
            desc.Width = width;
            desc.MipLevels = mipLevels;
            return desc;
        }   

        static GfxSurfaceDesc SurfaceDesc2D(GfxFormat format, UINT width, UINT height, GfxSurfaceFlags flags, UINT mipLevels = 1)
        {
            GfxSurfaceDesc desc;
            desc.Format = format;
            desc.Dimension = GfxSurfaceDimension::Dimension2D;
            desc.Flags = flags;
            desc.Width = width;
            desc.Height = height;
            desc.MipLevels = mipLevels;
            return desc;
        }

        static GfxSurfaceDesc SurfaceDesc3D(GfxFormat format, UINT width, UINT height, UINT depth, GfxSurfaceFlags flags, UINT mipLevels = 1)
        {
            GfxSurfaceDesc desc;
            desc.Format = format;
            desc.Dimension = GfxSurfaceDimension::Dimension3D;
            desc.Flags = flags;
            desc.Width = width;
            desc.Height = height;
            desc.Depth = depth;
            desc.MipLevels = mipLevels;
            return desc;
        }

        static GfxSurfaceDesc SurfaceDescCube(GfxFormat format, UINT size, UINT arraySize, GfxSurfaceFlags flags, UINT mipLevels = 1)
        {
            GfxSurfaceDesc desc;
            desc.Format = format;
            desc.Dimension = GfxSurfaceDimension::DimensionCube;
            desc.Flags = flags;
            desc.Width = size;
            desc.Height = size;
            desc.ArraySize = arraySize;
            desc.MipLevels = mipLevels;
            return desc;
        }   
    };

    //------------------------------------------------------------------------------------------------
    // Interface to a graphics device
    struct XGfxDevice : public XCanvasElement
    {
        GEM_INTERFACE_DECLARE(XGfxDevice, 0x86D4ABCCCD5FB6EE);

        GEMMETHOD(CreateRenderQueue)(Canvas::XGfxRenderQueue **ppRenderQueue) = 0;
        GEMMETHOD(CreateMaterial)() = 0;
        GEMMETHOD(CreateSurface)(const GfxSurfaceDesc &desc, XGfxSurface **ppSurface) = 0;
        GEMMETHOD(CreateBuffer)(uint64_t sizeInBytes, GfxMemoryUsage memoryUsage, XGfxBuffer **ppBuffer) = 0;
        GEMMETHOD(AllocateHostWriteRegion)(uint64_t sizeInBytes, GfxBufferSuballocation &suballocationInfo) = 0;
        GEMMETHOD_(void, FreeHostWriteRegion)(GfxBufferSuballocation &suballocationInfo) = 0;
        GEMMETHOD(CreateMeshData)(
            uint32_t vertexCount,
            const Canvas::Math::FloatVector4 *positions,
            const Canvas::Math::FloatVector4 *normals,
            XGfxRenderQueue *pRenderQueue,
            XGfxMeshData **ppMesh) = 0;
        GEMMETHOD(CreateDebugMeshData)(
            uint32_t vertexCount,
            const Canvas::Math::FloatVector4 *positions,
            const Canvas::Math::FloatVector4 *normals,
            XGfxRenderQueue *pRenderQueue,
            XGfxMeshData **ppMesh) = 0;
    };
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif
