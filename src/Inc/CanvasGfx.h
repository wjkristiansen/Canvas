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
        R11G11B10_Float,
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
        AmbientOcclusion,
        Emissive,
    };

    //------------------------------------------------------------------------------------------------
    // Optional displacement extension on a material.  When attached, the
    // engine selects a tessellation+displacement render path for this
    // material instead of the standard lit path.  All shaders, root sigs,
    // and PSOs stay engine-owned; the material only describes intent.
    //
    // Heightmap is a single-channel UNorm texture sampled per displaced
    // vertex.  HeightScale / HeightBias decode the [0,1] sample into world
    // units along the surface normal (for a flat tile mesh this is the
    // local +Z direction).  Tess factors are computed by the engine using
    // a distance + curvature LOD scheme parameterized by the four LOD
    // knobs below; per-edge factors are computed from edge-midpoint
    // quantities so adjacent patches agree on shared edges.
    struct GfxDisplacementDesc
    {
        XGfxSurface *pHeightmap        = nullptr;   // single-channel UNorm
        float        HeightScale       = 1.0f;      // world units per 1.0 sample
        float        HeightBias        = 0.0f;      // world units added to all samples

        // World-space tile extents along the displaced surface's local X / Y
        // axes.  The procedural patch grid mesh emits unit-square [0,1]^2
        // positions; the engine multiplies by these to obtain world-space
        // patch XY.  Kept here (on the material extension) rather than
        // encoded as node scale because scale propagates through the
        // scene-graph hierarchy and would wrongly affect children of
        // the tile node.
        float        TileSizeWorldX    = 1.0f;
        float        TileSizeWorldY    = 1.0f;

        float        MinTessFactor     = 2.0f;
        float        MaxTessFactor     = 32.0f;
        // Distance factor: clamp(scale * edge_world_length / dist_to_midpoint, Min, Max).
        float        DistanceLodScale  = 10.0f;
        // Curvature factor (added on top of distance): meters of 2nd-derivative
        // at a coarse-mip Laplacian, scaled by this constant.
        float        CurvatureLodScale = 0.5f;
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

        // Bind a texture to a specific material role. Pass nullptr to clear.
        // Implementations may reject roles they don't support (e.g. unused
        // roles in a fixed PBR layer set); see backend documentation.
        GEMMETHOD(SetTexture)(MaterialLayerRole role, XGfxSurface *pSurface) = 0;
        GEMMETHOD_(XGfxSurface *, GetTexture)(MaterialLayerRole role) = 0;

        // Constant factors used when no texture is bound (and as a tint when a
        // texture is bound). Linear-space colors. Default = identity (white,1).
        GEMMETHOD_(void, SetBaseColorFactor)(const Math::FloatVector4 &factor) = 0;
        GEMMETHOD_(Math::FloatVector4, GetBaseColorFactor)() = 0;

        GEMMETHOD_(void, SetEmissiveFactor)(const Math::FloatVector4 &factor) = 0;
        GEMMETHOD_(Math::FloatVector4, GetEmissiveFactor)() = 0;

        // R=Roughness, G=Metallic, B=AmbientOcclusion, A=spare. All in [0,1].
        // Default = (1,0,1,0): fully rough, non-metallic, no AO occlusion.
        GEMMETHOD_(void, SetRoughMetalAOFactor)(const Math::FloatVector4 &factor) = 0;
        GEMMETHOD_(Math::FloatVector4, GetRoughMetalAOFactor)() = 0;

        // Attach (or clear) a displacement extension.  Pass nullptr to clear.
        // Stored by value; the surface reference inside is held strongly.
        // When set, the engine routes draws of this material through its
        // built-in tessellation+displacement render path.
        GEMMETHOD_(void, SetDisplacement)(const GfxDisplacementDesc *pDesc) = 0;
        // Returns nullptr when no displacement is attached.
        GEMMETHOD_(const GfxDisplacementDesc *, GetDisplacement)() const = 0;
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
    struct GfxResourceAllocation
    {
        Gem::TGemPtr<XGfxBuffer> pBuffer;
        uint64_t Offset = 0;
        uint64_t Size = 0;
        uint64_t AllocationKey = 0;     // Opaque key for suballocator deallocation
    };

    //------------------------------------------------------------------------------------------------
    // Primitive topology for mesh data.  TriangleList is the standard case
    // (3 verts per triangle, requires position / normal vertex buffers).
    // PatchList4CP marks a "procedural" mesh: no vertex buffers, the
    // engine emits DrawInstanced(VertexCount, 1, 0, 0) with
    // 4-control-point patch list topology and the VS reconstructs the
    // mesh from SV_VertexID.  Used by tessellated displacement materials
    // and by any future SV_VertexID-driven mesh (instanced grids,
    // particle expansion).
    enum class GfxPrimitiveTopology
    {
        TriangleList,
        PatchList4CP,
    };

    //------------------------------------------------------------------------------------------------
    struct
    XGfxMeshData : public XCanvasElement
    {
        GEM_INTERFACE_DECLARE(XGfxMeshData, 0x7EBC2A5A40CC96D3);

        GEMMETHOD_(uint32_t, GetNumMaterialGroups)() = 0;
        GEMMETHOD_(GfxResourceAllocation *, GetVertexBuffer)(uint32_t materialIndex, GfxVertexBufferType type) = 0;
        GEMMETHOD_(XGfxMaterial *, GetMaterial)(uint32_t materialIndex) = 0;

        // Topology shared by all groups of this mesh.  Default constructed
        // mesh data is TriangleList for backward compatibility.
        GEMMETHOD_(GfxPrimitiveTopology, GetTopology)() = 0;

        // Total vertex / control-point count across all groups.  For a
        // procedural mesh this is what the engine passes to DrawInstanced;
        // for a triangle-list mesh it is the sum of per-group VertexCounts.
        GEMMETHOD_(uint32_t, GetTotalVertexCount)() = 0;
    };

    //------------------------------------------------------------------------------------------------
    // One material partition of a mesh. Positions/Normals are required;
    // UV0/Tangents are optional and may be null. pMaterial is optional; when
    // null the backend renders the group with a default/untextured material.
    //
    // All vertex arrays must be VertexCount entries long. Topology is an
    // implicit triangle list; no index buffer in this layout.
    struct MeshDataGroupDesc
    {
        uint32_t                        VertexCount   = 0;
        const Canvas::Math::FloatVector4 *pPositions  = nullptr;   // required, W=1
        const Canvas::Math::FloatVector4 *pNormals    = nullptr;   // required, W=0
        const Canvas::Math::FloatVector2 *pUV0        = nullptr;   // optional
        const Canvas::Math::FloatVector4 *pTangents   = nullptr;   // optional, xyz=T, w=bitangent sign
        XGfxMaterial                    *pMaterial    = nullptr;   // optional
    };

    //------------------------------------------------------------------------------------------------
    struct MeshDataDesc
    {
        const MeshDataGroupDesc        *pGroups       = nullptr;
        uint32_t                        GroupCount    = 0;
        const char                     *pName         = nullptr;

        // When Topology is a procedural type (e.g. PatchList4CP) the
        // group's vertex arrays may be null - the engine will draw
        // VertexCount control points from SV_VertexID with no input
        // assembler bindings.
        GfxPrimitiveTopology            Topology      = GfxPrimitiveTopology::TriangleList;
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
    // Render queue - submits and executes rendering work.
    struct XGfxRenderQueue : public XCanvasElement
    {
        GEM_INTERFACE_DECLARE(XGfxRenderQueue, 0x728AF985153F712D);

        GEMMETHOD(CreateSwapChain)(HWND hWnd, bool Windowed, XGfxSwapChain **ppSwapChain, GfxFormat Format, UINT NumBuffers) = 0;
        GEMMETHOD(FlushAndPresent)(XGfxSwapChain *pSwapChain) = 0;

        // Frame rendering
        GEMMETHOD(BeginFrame)(XGfxSwapChain *pSwapChain) = 0;
        GEMMETHOD(EndFrame)() = 0;

        // Scene/UI graph submission
        GEMMETHOD(SubmitForRender)(XSceneGraphNode *pNode) = 0;
        GEMMETHOD(SubmitForUIRender)(XUIGraphNode *pNode) = 0;
        GEMMETHOD_(void, SetActiveCamera)(XCamera *pCamera) = 0;

        // Debug: force the geometry pass to render as wireframe. UI / composite
        // passes are unaffected. Default false. The PSO variant is created
        // lazily on first enable so the cost is zero until used.
        GEMMETHOD_(void, SetGeometryWireframe)(bool wireframe) = 0;
        GEMMETHOD_(bool, GetGeometryWireframe)() const = 0;

        // Finalize a one-shot texture upload by scheduling a direct-queue
        // barrier that transitions the surface from LAYOUT_COMMON to
        // LAYOUT_SHADER_RESOURCE. Use after XGfxDevice::UploadTextureRegion
        // for textures that will be sampled as static SRVs (e.g. terrain
        // heightmaps, material atlases). The render queue stamps the surface
        // with a fence token so DATA_STATIC consumers can wait for the
        // transition to fully retire before binding the descriptor. Calling
        // this on a surface that is not in LAYOUT_COMMON is a no-op.
        GEMMETHOD(FinalizeUploadAsShaderResource)(XGfxSurface *pSurface) = 0;
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
        GEMMETHOD(CreateMaterial)(XGfxMaterial **ppMaterial) = 0;
        GEMMETHOD(CreateSurface)(const GfxSurfaceDesc &desc, XGfxSurface **ppSurface) = 0;
        GEMMETHOD(CreateBuffer)(uint64_t sizeInBytes, GfxMemoryUsage memoryUsage, XGfxBuffer **ppBuffer) = 0;

        // Primary mesh-data factory: multi-group, supports UV0 / tangents /
        // per-group materials. Group count >= 1.
        GEMMETHOD(CreateMeshData)(const MeshDataDesc &desc, XGfxMeshData **ppMesh) = 0;

        // Convenience factory for a [0,1]^2 unit-square procedural patch
        // grid mesh.  patchesPerSide >= 1.  Result is a single-group
        // XGfxMeshData with PatchList4CP topology, no vertex buffers,
        // VertexCount = patchesPerSide * patchesPerSide * 4, and the
        // supplied material attached to the group.  Intended to be paired
        // with a material that has a displacement extension attached
        // (XGfxMaterial::SetDisplacement); the engine generates per-CP
        // positions / UVs from SV_VertexID.  The mesh instance's world
        // transform should scale this unit square to the tile's world
        // extents and translate to its origin.  pMaterial may be null
        // (rendering will be skipped until a material is attached via
        // some future API).
        GEMMETHOD(CreateProceduralPatchGrid)(
            uint32_t patchesPerSide,
            XGfxMaterial *pMaterial,
            XGfxMeshData **ppMesh,
            const char *name = nullptr) = 0;

        GEMMETHOD(CreateDebugMeshData)(
            uint32_t vertexCount,
            const Canvas::Math::FloatVector4 *positions,
            const Canvas::Math::FloatVector4 *normals,
            XGfxMeshData **ppMesh,
            const char* name = nullptr) = 0;

        // Convenience wrapper around the descriptor-form CreateMeshData for the
        // common single-group, position+normal-only case. Non-virtual; calls
        // through to the primary virtual.
        Gem::Result CreateMeshData(
            uint32_t vertexCount,
            const Canvas::Math::FloatVector4 *positions,
            const Canvas::Math::FloatVector4 *normals,
            XGfxMeshData **ppMesh,
            const char *name = nullptr)
        {
            MeshDataGroupDesc group;
            group.VertexCount = vertexCount;
            group.pPositions  = positions;
            group.pNormals    = normals;

            MeshDataDesc desc;
            desc.pGroups    = &group;
            desc.GroupCount = 1;
            desc.pName      = name;
            return CreateMeshData(desc, ppMesh);
        }

        // Allocate a GPU buffer and optionally upload initial data.
        // If `inOut` already holds a buffer, it is retired to the pool for future reuse.
        // pInitialData may be null to allocate without uploading.
        GEMMETHOD(AllocateStructuredBuffer)(uint32_t elementCount, uint32_t elementStride, const void* pInitialData, XGfxRenderQueue* pRQ, GfxResourceAllocation& inOut) = 0;

        // Force any pending uploads (mesh data, vertex buffers) staged via this
        // device to be submitted to the GPU now.  Uploads run asynchronously on a
        // dedicated copy queue and are normally consumed when the next render
        // submit gates on them; this entry point lets callers publish them
        // eagerly (e.g., at scene-load time, before the first frame).
        GEMMETHOD(FlushUploads)() = 0;

        // Upload CPU data into a sub-region of a GPU surface via a staging copy
        // on the device's copy queue.  Safe to call before the first BeginFrame;
        // the next render submit gates on the copy fence.
        GEMMETHOD(UploadTextureRegion)(
            XGfxSurface *pDstSurface,
            uint32_t dstX, uint32_t dstY,
            uint32_t width, uint32_t height,
            const void *pData,
            uint32_t srcRowPitch) = 0;

        // UI element creation (device wires GPU resources internally)
        GEMMETHOD(CreateTextElement)(XUITextElement **ppElement) = 0;
        GEMMETHOD(CreateRectElement)(XUIRectElement **ppElement) = 0;
    };
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif
