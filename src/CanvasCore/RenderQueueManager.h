//================================================================================================
// RenderQueueManager.h - Internal Canvas class for managing render queues
//================================================================================================

#pragma once
#include "CanvasRender.h"
#include <vector>
#include <memory>

namespace Canvas
{
    //------------------------------------------------------------------------------------------------
    // Forward declarations
    struct XGfxGraphicsContext;
    struct XGfxDevice;

    //------------------------------------------------------------------------------------------------
    // CRenderQueueManager - Internal Canvas class for scheduling and managing renderable things
    // This class is not visible to the Gfx subsystem.
    class CRenderQueueManager
    {
    public:
        CRenderQueueManager();
        ~CRenderQueueManager();

        // Initialize the manager with a graphics device
        Gem::Result Initialize(XGfxDevice* pDevice);
        
        // Shutdown and cleanup
        void Shutdown();

        // Create a new render queue with the specified capacity and priority
        Gem::Result CreateRenderQueue(UINT maxPackets, UINT priority, UINT queueFlags, Canvas::RenderQueue** ppQueue);
        
        // Submit a render queue for processing
        Gem::Result SubmitQueue(Canvas::RenderQueue* pQueue);
        
        // Process all submitted queues (renders them)
        Gem::Result ProcessQueues(XGfxGraphicsContext* pContext, const Canvas::RenderContext& renderContext);
        
        // Clear all queues
        void ClearQueues();
        
        // Chunk creation methods - these handle adding chunks to render queues
        Canvas::MeshChunkData* CreateMeshChunk(
            Canvas::RenderQueue* pQueue,
            XGfxBuffer* pVertexBuffer,
            XGfxBuffer* pIndexBuffer,
            uint32_t vertexCount,
            uint32_t indexCount,
            const Canvas::Math::FloatMatrix4x4& transform,
            const Canvas::Math::AABB& localBounds,
            uint32_t materialId = 0,
            uint32_t renderFlags = 0);
            
        Canvas::UIChunkData* CreateUIChunk(
            Canvas::RenderQueue* pQueue,
            const Canvas::Math::FloatVector4& screenRect,
            const Canvas::Math::FloatVector4& color,
            uint32_t textureId = 0,
            float depth = 0.0f);
            
        Canvas::ParticleChunkData* CreateParticleChunk(
            Canvas::RenderQueue* pQueue,
            XGfxBuffer* pParticleBuffer,
            uint32_t maxParticles,
            uint32_t activeParticles,
            const Canvas::Math::FloatMatrix4x4& transform);
        
        // Get statistics
        struct Statistics
        {
            UINT TotalQueues;
            UINT TotalPackets;
            UINT ProcessedQueues;
            UINT ProcessedPackets;
            float LastFrameTime;
        };
        
        void GetStatistics(Statistics& stats) const;
        
        // RenderContext management methods - these handle POD RenderContext manipulation
        void InitializeRenderContext(
            Canvas::RenderContext* pContext,
            Canvas::LightData* pLights,
            UINT maxLights);
            
        bool AddLightToContext(
            Canvas::RenderContext* pContext,
            const Canvas::LightData& light);
            
        void ClearContextLights(Canvas::RenderContext* pContext);

    private:
        // Internal queue management
        struct QueueEntry
        {
            std::unique_ptr<Canvas::RenderQueue> Queue;
            std::vector<uint8_t> ChunkBuffer;
            bool IsActive;
            
            QueueEntry(UINT maxPackets);
        };
        
        // Managed queues
        std::vector<std::unique_ptr<QueueEntry>> m_Queues;
        
        // Submitted queues for current frame
        std::vector<Canvas::RenderQueue*> m_SubmittedQueues;
        
        // Graphics device reference
        XGfxDevice* m_pDevice;
        
        // Statistics
        mutable Statistics m_Stats;
        
        // Internal helpers
        void SortQueuesByPriority();
        bool IsQueueEmpty(const Canvas::RenderQueue* pQueue) const;
        void ClearQueue(Canvas::RenderQueue* pQueue);
        
        // Template method for adding chunks to a queue
        template<typename ChunkDataType>
        ChunkDataType* AddChunk(Canvas::RenderQueue* pQueue, Canvas::RenderChunkType type, uint32_t flags = 0);
        Gem::Result ProcessRenderQueue(XGfxGraphicsContext* pContext, const Canvas::RenderContext& renderContext, Canvas::RenderQueue* pQueue);
        Gem::Result ProcessRenderChunks(XGfxGraphicsContext* pContext, const Canvas::RenderContext& renderContext, const Canvas::RenderQueue& queue);
        
        // Specific chunk processors
        Gem::Result ProcessMeshChunk(XGfxGraphicsContext* pContext, const Canvas::RenderContext& renderContext, const Canvas::MeshChunkData& meshData);
        Gem::Result ProcessSkinnedMeshChunk(XGfxGraphicsContext* pContext, const Canvas::RenderContext& renderContext, const Canvas::SkinnedMeshChunkData& skinnedMeshData);
        Gem::Result ProcessParticleChunk(XGfxGraphicsContext* pContext, const Canvas::RenderContext& renderContext, const Canvas::ParticleChunkData& particleData);
        Gem::Result ProcessUIChunk(XGfxGraphicsContext* pContext, const Canvas::RenderContext& renderContext, const Canvas::UIChunkData& uiData);
        Gem::Result ProcessInstancedChunk(XGfxGraphicsContext* pContext, const Canvas::RenderContext& renderContext, const Canvas::InstancedChunkData& instancedData);
    };

    //------------------------------------------------------------------------------------------------
    // Utility functions for render queue management
    namespace RenderQueueUtils
    {
        // Create a mesh chunk from mesh data
        Gem::Result CreateMeshChunk(
            XGfxBuffer* pVertexBuffer,
            XGfxBuffer* pIndexBuffer,
            UINT vertexCount,
            UINT indexCount,
            const Math::FloatMatrix4x4& transform,
            const Math::AABB& localBounds,
            Canvas::RenderQueue& queue);
        
        // Copy transform matrix
        void CopyTransform(const Math::FloatMatrix4x4& source, Math::FloatMatrix4x4& dest);
        
        // Create identity transform
        Math::FloatMatrix4x4 CreateIdentityTransform();
        
        // AABB utilities
        Math::AABB TransformAABB(const Math::AABB& localAABB, const Math::FloatMatrix4x4& transform);
        bool AABBIntersects(const Math::AABB& a, const Math::AABB& b);
        bool AABBContainsPoint(const Math::AABB& aabb, const Math::FloatVector4& point);
        Math::AABB CreateLightInfluenceBounds(const Canvas::LightData& light);
        
        // Camera utilities
        Gem::Result UpdateCameraData(XCamera* pCamera, Canvas::CameraData& cameraData);
        void CalculateViewMatrixFromWorld(const Math::FloatMatrix4x4& worldMatrix, Math::FloatMatrix4x4& viewMatrix);
        void CalculateViewMatrix(const Math::FloatVector4& eyePos, const Math::FloatVector4& target, const Math::FloatVector4& up, Math::FloatMatrix4x4& viewMatrix);
        void CalculateProjectionMatrix(float fovAngle, float aspectRatio, float nearClip, float farClip, Math::FloatMatrix4x4& projMatrix);
        void MultiplyMatrices(const Math::FloatMatrix4x4& a, const Math::FloatMatrix4x4& b, Math::FloatMatrix4x4& result);
        
        // Helper functions to extract data from matrices
        Math::FloatVector4 GetCameraPosition(const Math::FloatMatrix4x4& worldMatrix);
        Math::FloatVector4 GetCameraForward(const Math::FloatMatrix4x4& worldMatrix);
        Math::FloatVector4 GetCameraUp(const Math::FloatMatrix4x4& worldMatrix);
        Math::FloatVector4 GetCameraRight(const Math::FloatMatrix4x4& worldMatrix);
        
        // Light utilities
        Gem::Result UpdateLightData(XLight* pLight, Canvas::LightData& lightData);
        void SetDirectionalLight(Canvas::LightData& light, const Math::FloatVector4& direction, const Math::FloatVector4& color, float intensity);
        void SetPointLight(Canvas::LightData& light, const Math::FloatVector4& position, const Math::FloatVector4& color, float intensity, float range);
        void SetSpotLight(Canvas::LightData& light, const Math::FloatVector4& position, const Math::FloatVector4& direction, 
                         const Math::FloatVector4& color, float intensity, float range, float innerAngle, float outerAngle);
    }
}