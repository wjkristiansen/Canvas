//================================================================================================
// CanvasRender
//================================================================================================

#pragma once
#include <cstdint>
#include <cstring>
#include <QLog.h>
#include "CanvasGfx.h"
#include "CanvasMath.hpp"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324)
#endif

namespace Canvas
{

    //------------------------------------------------------------------------------------------------
    // Camera data for rendering - extends CanvasModel::CAMERA_DATA
    struct CameraData
    {
        // Base camera data from CanvasModel
        float NearClip;
        float FarClip;
        float FovAngle;
        float AspectRatio;  // Additional field for rendering
        
        // Camera world matrix (camera's transform in world space)
        Math::FloatMatrix4x4 WorldMatrix;
        
        // View matrix (inverse of WorldMatrix)
        Math::FloatMatrix4x4 ViewMatrix;
        
        // Projection matrix
        Math::FloatMatrix4x4 ProjectionMatrix;
        
        // View-projection matrix (combined)
        Math::FloatMatrix4x4 ViewProjectionMatrix;
        
        // Constructor
        CameraData()
            : NearClip(0.1f)
            , FarClip(1000.0f)
            , FovAngle(45.0f)
            , AspectRatio(16.0f / 9.0f)
            , WorldMatrix(Math::FloatMatrix4x4::Identity())
            , ViewMatrix(Math::FloatMatrix4x4::Identity())
            , ProjectionMatrix(Math::FloatMatrix4x4::Identity())
            , ViewProjectionMatrix(Math::FloatMatrix4x4::Identity())
        {
        }
    };

    //------------------------------------------------------------------------------------------------
    // Light types - compatible with CanvasModel::LIGHT_TYPE
    enum LightType : UINT
    {
        LIGHT_TYPE_NULL = 0,
        LIGHT_TYPE_AMBIENT = 1,
        LIGHT_TYPE_POINT = 2,
        LIGHT_TYPE_DIRECTIONAL = 3,
        LIGHT_TYPE_SPOT = 4,
        LIGHT_TYPE_AREA = 5,
    };

    //------------------------------------------------------------------------------------------------
    // Light data for rendering - extends CanvasModel::LIGHT_DATA
    struct alignas(16) LightData
    {
        // Base light data from CanvasModel
        LightType Type;
        Math::FloatVector4 Color;  // Compatible with CanvasModel::LIGHT_DATA.Color
        
        // Extended rendering data
        UINT Flags;
        
        // Intensity
        float Intensity;
        
        // Attenuation parameters (for point/spot lights)
        float AttenuationConstant;
        float AttenuationLinear;
        float AttenuationQuadratic;
        float Range;
        
        // Spot light parameters (compatible with CanvasModel::SPOT_LIGHT_INFO)
        float SpotInnerAngle;  // In radians
        float SpotOuterAngle;  // In radians
        
        // Axis-aligned bounding box representing the spatial influence of the light
        Math::AABB InfluenceBounds;
        
        // Constructor
        LightData()
            : Type(LIGHT_TYPE_DIRECTIONAL)
            , Color(1.0f, 1.0f, 1.0f, 1.0f)
            , Flags(0)
            , Intensity(1.0f)
            , AttenuationConstant(1.0f)
            , AttenuationLinear(0.0f)
            , AttenuationQuadratic(0.0f)
            , Range(100.0f)
            , SpotInnerAngle(0.52f)  // ~30 degrees
            , SpotOuterAngle(0.785f) // ~45 degrees
        {
        }
    };

    //------------------------------------------------------------------------------------------------
    // Light flags
    enum LightFlags : UINT
    {
        LIGHT_FLAG_NONE = 0,
        LIGHT_FLAG_CAST_SHADOWS = 1 << 0,
        LIGHT_FLAG_ENABLED = 1 << 1,
    };

    //------------------------------------------------------------------------------------------------
    // Render chunk types - each chunk contains specific renderable data
    enum class RenderChunkType : uint32_t
    {
        Invalid = 0,
        Mesh = 1,           // Static mesh geometry
        SkinnedMesh = 2,    // Skeletal animation mesh
        Particles = 3,      // Particle system
        UI = 4,             // User interface elements
        Terrain = 5,        // Heightfield terrain
        Decal = 6,          // Projected decals
        Volume = 7,         // Volumetric rendering (fog, clouds)
        Instanced = 8,      // GPU-instanced geometry
        Lines = 9,          // Line/wireframe primitives
        Points = 10,        // Point cloud/sprites
        Light = 11,         // Light source data
        Camera = 12,        // Camera data
    };

    //------------------------------------------------------------------------------------------------
    // Render chunk header - appears at the start of every chunk
    struct alignas(16) RenderChunkHeader
    {
        RenderChunkType Type;       // Chunk type identifier
        uint32_t Size;              // Size of entire chunk (including header)
        uint32_t Flags;             // Chunk-specific flags
        uint32_t Reserved;          // Reserved for future use (maintains alignment)

        RenderChunkHeader()
            : Type(RenderChunkType::Invalid)
            , Size(sizeof(RenderChunkHeader))
            , Flags(0)
            , Reserved(0)
        {
        }

        RenderChunkHeader(RenderChunkType type, uint32_t dataSize, uint32_t flags = 0)
            : Type(type)
            , Size(sizeof(RenderChunkHeader) + dataSize)
            , Flags(flags)
            , Reserved(0)
        {
        }

        // Get pointer to chunk data (after header)
        const void* GetData() const
        {
            return reinterpret_cast<const uint8_t*>(this) + sizeof(RenderChunkHeader);
        }

        void* GetData()
        {
            return reinterpret_cast<uint8_t*>(this) + sizeof(RenderChunkHeader);
        }

        // Get data size (excluding header)
        uint32_t GetDataSize() const
        {
            return Size - sizeof(RenderChunkHeader);
        }

        // Get next chunk in buffer
        const RenderChunkHeader* GetNext() const
        {
            return reinterpret_cast<const RenderChunkHeader*>(
                reinterpret_cast<const uint8_t*>(this) + Size
            );
        }

        RenderChunkHeader* GetNext()
        {
            return reinterpret_cast<RenderChunkHeader*>(
                reinterpret_cast<uint8_t*>(this) + Size
            );
        }
    };

    //------------------------------------------------------------------------------------------------
    // Common render data that appears in many chunk types
    struct alignas(16) RenderCommonData
    {
        Math::FloatMatrix4x4 Transform;     // World transform matrix
        Math::AABB BoundingBox;             // World-space bounding box
        float SortDistance;                 // Distance for depth sorting
        uint32_t MaterialId;                // Material identifier/handle
        uint32_t LayerMask;                 // Rendering layer mask
        uint32_t RenderFlags;               // Render state flags

        RenderCommonData()
            : Transform(Math::FloatMatrix4x4::Identity())
            , BoundingBox()
            , SortDistance(0.0f)
            , MaterialId(0)
            , LayerMask(0xFFFFFFFF)
            , RenderFlags(0)
        {
        }
    };

    //------------------------------------------------------------------------------------------------
    // Mesh chunk data - follows RenderChunkHeader with Type=Mesh
    struct alignas(16) MeshChunkData
    {
        RenderCommonData Common;            // Common render data
        XGfxBuffer* pVertexBuffer;          // Vertex data
        XGfxBuffer* pIndexBuffer;           // Index data (optional)
        uint32_t VertexCount;
        uint32_t IndexCount;
        uint32_t StartVertexLocation;
        uint32_t StartIndexLocation;
        uint32_t VertexStride;              // Size of each vertex

        MeshChunkData()
            : Common()
            , pVertexBuffer(nullptr)
            , pIndexBuffer(nullptr)
            , VertexCount(0)
            , IndexCount(0)
            , StartVertexLocation(0)
            , StartIndexLocation(0)
            , VertexStride(0)
        {
        }
    };

    //------------------------------------------------------------------------------------------------
    // Skinned mesh chunk data - follows RenderChunkHeader with Type=SkinnedMesh
    struct alignas(16) SkinnedMeshChunkData
    {
        RenderCommonData Common;            // Common render data
        XGfxBuffer* pVertexBuffer;          // Vertex data
        XGfxBuffer* pIndexBuffer;           // Index data
        XGfxBuffer* pBoneBuffer;            // Bone transformation matrices
        uint32_t VertexCount;
        uint32_t IndexCount;
        uint32_t BoneCount;
        uint32_t VertexStride;
        uint32_t StartVertexLocation;
        uint32_t StartIndexLocation;

        SkinnedMeshChunkData()
            : Common()
            , pVertexBuffer(nullptr)
            , pIndexBuffer(nullptr)
            , pBoneBuffer(nullptr)
            , VertexCount(0)
            , IndexCount(0)
            , BoneCount(0)
            , VertexStride(0)
            , StartVertexLocation(0)
            , StartIndexLocation(0)
        {
        }
    };

    //------------------------------------------------------------------------------------------------
    // Particle system chunk data - follows RenderChunkHeader with Type=Particles
    struct alignas(16) ParticleChunkData
    {
        RenderCommonData Common;            // Common render data
        XGfxBuffer* pParticleBuffer;        // Particle data buffer
        XGfxBuffer* pIndirectArgsBuffer;    // Indirect draw arguments
        uint32_t MaxParticles;
        uint32_t ActiveParticles;
        float EmissionRate;
        float LifeTime;
        Math::FloatVector4 StartColor;
        Math::FloatVector4 EndColor;
        float StartSize;
        float EndSize;

        ParticleChunkData()
            : Common()
            , pParticleBuffer(nullptr)
            , pIndirectArgsBuffer(nullptr)
            , MaxParticles(0)
            , ActiveParticles(0)
            , EmissionRate(1.0f)
            , LifeTime(1.0f)
            , StartColor(1.0f, 1.0f, 1.0f, 1.0f)
            , EndColor(1.0f, 1.0f, 1.0f, 0.0f)
            , StartSize(1.0f)
            , EndSize(0.1f)
        {
        }
    };

    //------------------------------------------------------------------------------------------------
    // UI element chunk data - follows RenderChunkHeader with Type=UI
    struct alignas(16) UIChunkData
    {
        RenderCommonData Common;            // Common render data (transform used for screen positioning)
        XGfxBuffer* pVertexBuffer;          // UI quad vertices
        Math::FloatVector4 ScreenRect;      // Screen-space rectangle (x,y,w,h)
        Math::FloatVector4 TexCoords;       // Texture coordinates (u0,v0,u1,v1)
        Math::FloatVector4 Color;           // Tint color
        uint32_t TextureId;                 // Texture handle
        float Depth;                        // UI depth for sorting

        UIChunkData()
            : Common()
            , pVertexBuffer(nullptr)
            , ScreenRect(0.0f, 0.0f, 0.0f, 0.0f)
            , TexCoords(0.0f, 0.0f, 1.0f, 1.0f)
            , Color(1.0f, 1.0f, 1.0f, 1.0f)
            , TextureId(0)
            , Depth(0.0f)
        {
        }
    };

    //------------------------------------------------------------------------------------------------
    // Instanced rendering chunk data - follows RenderChunkHeader with Type=Instanced
    struct alignas(16) InstancedChunkData
    {
        RenderCommonData Common;            // Common render data
        XGfxBuffer* pVertexBuffer;          // Base mesh vertex data
        XGfxBuffer* pIndexBuffer;           // Base mesh index data
        XGfxBuffer* pInstanceBuffer;        // Per-instance data buffer
        uint32_t VertexCount;
        uint32_t IndexCount;
        uint32_t InstanceCount;
        uint32_t VertexStride;
        uint32_t InstanceStride;            // Size of each instance data
        uint32_t StartVertexLocation;
        uint32_t StartIndexLocation;

        InstancedChunkData()
            : Common()
            , pVertexBuffer(nullptr)
            , pIndexBuffer(nullptr)
            , pInstanceBuffer(nullptr)
            , VertexCount(0)
            , IndexCount(0)
            , InstanceCount(0)
            , VertexStride(0)
            , InstanceStride(0)
            , StartVertexLocation(0)
            , StartIndexLocation(0)
        {
        }
    };

    //------------------------------------------------------------------------------------------------
    // Render flags for RenderPacket
    enum RenderFlags : uint32_t
    {
        RENDER_FLAG_NONE = 0,
        RENDER_FLAG_WIREFRAME = 1 << 0,
        RENDER_FLAG_NO_CULL = 1 << 1,
        RENDER_FLAG_TRANSPARENT = 1 << 2,
        RENDER_FLAG_CAST_SHADOW = 1 << 3,
        RENDER_FLAG_RECEIVE_SHADOW = 1 << 4,
        RENDER_FLAG_DEPTH_TEST = 1 << 5,
        RENDER_FLAG_DEPTH_WRITE = 1 << 6,
        RENDER_FLAG_ALPHA_BLEND = 1 << 7,
        RENDER_FLAG_ADDITIVE_BLEND = 1 << 8,
        RENDER_FLAG_MULTIPLY_BLEND = 1 << 9,
        RENDER_FLAG_DOUBLE_SIDED = 1 << 10,
        RENDER_FLAG_INSTANCED = 1 << 11,
        RENDER_FLAG_ANIMATED = 1 << 12,
        RENDER_FLAG_UI_ELEMENT = 1 << 13,
        RENDER_FLAG_POST_PROCESS = 1 << 14,
    };

    //------------------------------------------------------------------------------------------------
    // Chunk-based render queue - A buffer containing tightly-packed render chunks.
    // This is a plain-old-data structure for efficient storage and transfer.
    struct alignas(16) RenderQueue
    {
        // Buffer containing packed render chunks
        uint8_t* pChunkBuffer;
        
        // Current used size of the buffer (in bytes)
        uint32_t UsedSize;
        
        // Maximum capacity of the buffer (in bytes)
        uint32_t MaxSize;
        
        // Queue priority (higher values rendered first)
        uint32_t Priority;
        
        // Queue flags
        uint32_t QueueFlags;
        
        // Number of chunks in the queue (for quick iteration)
        uint32_t ChunkCount;
        
        // Default constructor - POD initialization
        RenderQueue()
            : pChunkBuffer(nullptr)
            , UsedSize(0)
            , MaxSize(0)
            , Priority(0)
            , QueueFlags(0)
            , ChunkCount(0)
        {
        }
    };

    //------------------------------------------------------------------------------------------------
    // Queue flags for RenderQueue
    enum QueueFlags : UINT
    {
        QUEUE_FLAG_NONE = 0,
        QUEUE_FLAG_OPAQUE = 1 << 0,
        QUEUE_FLAG_TRANSPARENT = 1 << 1,
        QUEUE_FLAG_SHADOW_CASTER = 1 << 2,
        QUEUE_FLAG_UI = 1 << 3,
        QUEUE_FLAG_POST_PROCESS = 1 << 4,
    };

    //------------------------------------------------------------------------------------------------
    // Rendering context that holds camera and lighting data for a frame
    // This is passed to the graphics subsystem for rendering
    // This is a plain-old-data structure for efficient storage and transfer.
    struct RenderContext
    {
        // Active camera data
        CameraData Camera;
        
        // Array of lights affecting this frame
        LightData* pLights;
        UINT LightCount;
        UINT MaxLights;
        
        // Ambient lighting (w component can be used for ambient intensity)
        Math::FloatVector4 AmbientColor;
        
        // Rendering flags and settings
        UINT ContextFlags;
        
        // Frame information
        UINT FrameNumber;
        float DeltaTime;
        float TotalTime;
        
        // Default constructor - POD initialization
        RenderContext()
            : pLights(nullptr)
            , LightCount(0)
            , MaxLights(0)
            , AmbientColor(0.1f, 0.1f, 0.1f, 1.0f)  // RGB + intensity in w
            , ContextFlags(0)
            , FrameNumber(0)
            , DeltaTime(0.0f)
            , TotalTime(0.0f)
        {
        }
    };

    //------------------------------------------------------------------------------------------------
    // Context flags for RenderContext
    enum ContextFlags : UINT
    {
        CONTEXT_FLAG_NONE = 0,
        CONTEXT_FLAG_WIREFRAME = 1 << 0,
        CONTEXT_FLAG_SHOW_NORMALS = 1 << 1,
        CONTEXT_FLAG_DISABLE_LIGHTING = 1 << 2,
        CONTEXT_FLAG_DISABLE_SHADOWS = 1 << 3,
    };
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif
