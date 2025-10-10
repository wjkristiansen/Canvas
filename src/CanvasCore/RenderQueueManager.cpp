//================================================================================================
// RenderQueueManager.cpp - Implementation of the render queue manager
//================================================================================================

#include "pch.h"
#include "RenderQueueManager.h"
#include "CanvasGfx.h"
#include <algorithm>
#include <chrono>

namespace Canvas
{
    //------------------------------------------------------------------------------------------------
    // QueueEntry implementation
    CRenderQueueManager::QueueEntry::QueueEntry(UINT maxPackets)
        : IsActive(false)
    {
        ChunkBuffer.resize(maxPackets);
        Queue = std::make_unique<Canvas::RenderQueue>();
        Queue->pChunkBuffer = ChunkBuffer.data();
        Queue->MaxSize = maxPackets;
    }

    //------------------------------------------------------------------------------------------------
    // CRenderQueueManager implementation
    CRenderQueueManager::CRenderQueueManager()
        : m_pDevice(nullptr)
        , m_Stats{}
    {
    }

    CRenderQueueManager::~CRenderQueueManager()
    {
        Shutdown();
    }

    Gem::Result CRenderQueueManager::Initialize(XGfxDevice* pDevice)
    {
        if (!pDevice)
            return Gem::Result::InvalidArg;

        m_pDevice = pDevice;
        
        // Initialize statistics
        m_Stats = {};
        
        return Gem::Result::Success;
    }

    void CRenderQueueManager::Shutdown()
    {
        ClearQueues();
        m_Queues.clear();
        m_pDevice = nullptr;
    }

    Gem::Result CRenderQueueManager::CreateRenderQueue(UINT maxPackets, UINT priority, UINT queueFlags, Canvas::RenderQueue** ppQueue)
    {
        if (!ppQueue || maxPackets == 0)
            return Gem::Result::InvalidArg;

        auto queueEntry = std::make_unique<QueueEntry>(maxPackets);
        queueEntry->Queue->Priority = priority;
        queueEntry->Queue->QueueFlags = queueFlags;
        queueEntry->IsActive = true;

        *ppQueue = queueEntry->Queue.get();
        m_Queues.push_back(std::move(queueEntry));
        
        return Gem::Result::Success;
    }

    Gem::Result CRenderQueueManager::SubmitQueue(Canvas::RenderQueue* pQueue)
    {
        if (!pQueue || IsQueueEmpty(pQueue))
            return Gem::Result::InvalidArg;

        // Find the queue in our managed list
        bool found = false;
        for (const auto& entry : m_Queues)
        {
            if (entry->Queue.get() == pQueue && entry->IsActive)
            {
                found = true;
                break;
            }
        }

        if (!found)
            return Gem::Result::InvalidArg;

        m_SubmittedQueues.push_back(pQueue);
        return Gem::Result::Success;
    }

    Gem::Result CRenderQueueManager::ProcessQueues(XGfxGraphicsContext* pContext, const Canvas::RenderContext& renderContext)
    {
        if (!pContext || !m_pDevice)
            return Gem::Result::InvalidArg;

        auto startTime = std::chrono::high_resolution_clock::now();

        // Sort queues by priority (higher priority first)
        SortQueuesByPriority();

        // Reset frame statistics
        m_Stats.ProcessedQueues = 0;
        m_Stats.ProcessedPackets = 0;

        // Process each submitted queue
        for (Canvas::RenderQueue* pQueue : m_SubmittedQueues)
        {
            auto result = ProcessRenderQueue(pContext, renderContext, pQueue);
            if (Gem::Succeeded(result))
            {
                m_Stats.ProcessedQueues++;
                m_Stats.ProcessedPackets += pQueue->ChunkCount;
            }
        }

        // Clear submitted queues for next frame
        m_SubmittedQueues.clear();

        // Calculate frame time
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
        m_Stats.LastFrameTime = duration.count() / 1000.0f; // Convert to milliseconds

        return Gem::Result::Success;
    }

    void CRenderQueueManager::ClearQueues()
    {
        for (auto& entry : m_Queues)
        {
            if (entry->Queue)
                ClearQueue(entry->Queue.get());
        }
        m_SubmittedQueues.clear();
    }

    void CRenderQueueManager::GetStatistics(Statistics& stats) const
    {
        stats = m_Stats;
        stats.TotalQueues = static_cast<UINT>(m_Queues.size());
        
        // Count total packets across all queues
        stats.TotalPackets = 0;
        for (const auto& entry : m_Queues)
        {
            if (entry->Queue)
                stats.TotalPackets += entry->Queue->ChunkCount;
        }
    }

    void CRenderQueueManager::SortQueuesByPriority()
    {
        std::sort(m_SubmittedQueues.begin(), m_SubmittedQueues.end(),
            [](const Canvas::RenderQueue* a, const Canvas::RenderQueue* b)
            {
                return a->Priority > b->Priority; // Higher priority first
            });
    }

    bool CRenderQueueManager::IsQueueEmpty(const Canvas::RenderQueue* pQueue) const
    {
        return !pQueue || pQueue->ChunkCount == 0;
    }

    void CRenderQueueManager::ClearQueue(Canvas::RenderQueue* pQueue)
    {
        if (pQueue)
        {
            pQueue->ChunkCount = 0;
            pQueue->UsedSize = 0;
        }
    }

    void CRenderQueueManager::InitializeRenderContext(RenderContext* /*pContext*/)
    {
    }

    Gem::Result CRenderQueueManager::ProcessRenderQueue(XGfxGraphicsContext* pContext, const Canvas::RenderContext& renderContext, Canvas::RenderQueue* pQueue)
    {
        if (!pContext || !pQueue || IsQueueEmpty(pQueue))
            return Gem::Result::InvalidArg;

        // Process each render packet in the queue
        for (UINT i = 0; i < pQueue->ChunkCount; ++i)
        {
            Gem::Result result = ProcessRenderChunks(pContext, renderContext, *pQueue);
            if (Gem::Failed(result))
            {
                // Log error but continue with other packets
                // TODO: Add proper logging
                continue;
            }
        }

        return Gem::Result::Success;
    }

    Gem::Result CRenderQueueManager::ProcessRenderChunks(XGfxGraphicsContext* pContext, const Canvas::RenderContext& renderContext, const Canvas::RenderQueue& queue)
    {
        if (!pContext)
            return Gem::Result::InvalidArg;

        // Manual iteration through chunks using pointer arithmetic
        const uint8_t* pCurrent = queue.pChunkBuffer;
        const uint8_t* pBufferEnd = queue.pChunkBuffer + queue.UsedSize;
        
        while (pCurrent && pCurrent < pBufferEnd)
        {
            const Canvas::RenderChunkHeader* pChunk = reinterpret_cast<const Canvas::RenderChunkHeader*>(pCurrent);
            
            // Validate chunk bounds
            if (pCurrent + pChunk->Size > pBufferEnd)
                break; // Invalid chunk, stop processing
                
            Gem::Result result = Gem::Result::Success;
            
            // Dispatch based on chunk type
            switch (pChunk->Type)
            {
                case Canvas::RenderChunkType::Mesh:
                {
                    if (pChunk->GetDataSize() >= sizeof(Canvas::MeshChunkData))
                    {
                        const Canvas::MeshChunkData* pMeshData = static_cast<const Canvas::MeshChunkData*>(pChunk->GetData());
                        result = ProcessMeshChunk(pContext, renderContext, *pMeshData);
                    }
                    break;
                }
                
                case Canvas::RenderChunkType::SkinnedMesh:
                {
                    if (pChunk->GetDataSize() >= sizeof(Canvas::SkinnedMeshChunkData))
                    {
                        const Canvas::SkinnedMeshChunkData* pSkinnedMeshData = static_cast<const Canvas::SkinnedMeshChunkData*>(pChunk->GetData());
                        result = ProcessSkinnedMeshChunk(pContext, renderContext, *pSkinnedMeshData);
                    }
                    break;
                }
                
                case Canvas::RenderChunkType::Particles:
                {
                    if (pChunk->GetDataSize() >= sizeof(Canvas::ParticleChunkData))
                    {
                        const Canvas::ParticleChunkData* pParticleData = static_cast<const Canvas::ParticleChunkData*>(pChunk->GetData());
                        result = ProcessParticleChunk(pContext, renderContext, *pParticleData);
                    }
                    break;
                }
                
                case Canvas::RenderChunkType::UI:
                {
                    if (pChunk->GetDataSize() >= sizeof(Canvas::UIChunkData))
                    {
                        const Canvas::UIChunkData* pUIData = static_cast<const Canvas::UIChunkData*>(pChunk->GetData());
                        result = ProcessUIChunk(pContext, renderContext, *pUIData);
                    }
                    break;
                }
                
                case Canvas::RenderChunkType::Instanced:
                {
                    if (pChunk->GetDataSize() >= sizeof(Canvas::InstancedChunkData))
                    {
                        const Canvas::InstancedChunkData* pInstancedData = static_cast<const Canvas::InstancedChunkData*>(pChunk->GetData());
                        result = ProcessInstancedChunk(pContext, renderContext, *pInstancedData);
                    }
                    break;
                }
                
                default:
                    // Unknown chunk type - skip gracefully (this is the beauty of the chunk system!)
                    // Note: Could log this if logger was available in this context
                    result = Gem::Result::Success;
                    break;
            }
            
            // Continue even if individual chunks fail
            if (Gem::Failed(result))
            {
                // Note: Could log this if logger was available in this context
                // For now, just continue processing other chunks
            }
            
            // Move to next chunk
            pCurrent += pChunk->Size;
        }
        
        return Gem::Result::Success;
    }

    //---------------------------------------------------------------------------------------------
    Gem::Result CRenderQueueManager::ProcessMeshChunk(XGfxGraphicsContext* pContext, const Canvas::RenderContext& /*renderContext*/, const Canvas::MeshChunkData& meshData)
    {
        if (!pContext)
            return Gem::Result::InvalidArg;
        
        // Basic validation
        if (!meshData.pVertexBuffer || meshData.VertexCount == 0)
            return Gem::Result::InvalidArg;

        // TODO: Implement actual rendering commands
        // This is where you would:
        // 1. Set vertex/index buffers
        // 2. Set render state based on meshData.Common.RenderFlags
        // 3. Set camera matrices from renderContext.Camera
        // 4. Set lighting data from renderContext.pLights
        // 5. Set transform matrix from meshData.Common.Transform
        // 6. Set material properties from meshData.Common.MaterialId
        // 7. Issue draw calls
        // 8. Use meshData.Common.BoundingBox for frustum culling
        
        // For now, this is a placeholder that demonstrates the interface
        // The actual implementation would depend on the specific graphics API
        
        return Gem::Result::Success;
    }

    //---------------------------------------------------------------------------------------------
    Gem::Result CRenderQueueManager::ProcessSkinnedMeshChunk(XGfxGraphicsContext* pContext, const Canvas::RenderContext& /*renderContext*/, const Canvas::SkinnedMeshChunkData& /*skinnedMeshData*/)
    {
        if (!pContext)
            return Gem::Result::InvalidArg;

        // TODO: Implement skinned mesh rendering
        // This would handle bone transformations and skeletal animation
        return Gem::Result::NotImplemented;
    }

    //---------------------------------------------------------------------------------------------
    Gem::Result CRenderQueueManager::ProcessParticleChunk(XGfxGraphicsContext* pContext, const Canvas::RenderContext& /*renderContext*/, const Canvas::ParticleChunkData& /*particleData*/)
    {
        if (!pContext)
            return Gem::Result::InvalidArg;

        // TODO: Implement particle system rendering
        // This would handle GPU-based particle simulation and rendering
        return Gem::Result::NotImplemented;
    }

    //---------------------------------------------------------------------------------------------
    Gem::Result CRenderQueueManager::ProcessUIChunk(XGfxGraphicsContext* pContext, const Canvas::RenderContext& /*renderContext*/, const Canvas::UIChunkData& /*uiData*/)
    {
        if (!pContext)
            return Gem::Result::InvalidArg;

        // TODO: Implement UI element rendering
        // This would handle screen-space UI quads and text
        return Gem::Result::NotImplemented;
    }

    //---------------------------------------------------------------------------------------------
    Gem::Result CRenderQueueManager::ProcessInstancedChunk(XGfxGraphicsContext* pContext, const Canvas::RenderContext& /*renderContext*/, const Canvas::InstancedChunkData& /*instancedData*/)
    {
        if (!pContext)
            return Gem::Result::InvalidArg;

        // TODO: Implement instanced rendering
        // This would handle GPU instancing for multiple objects
        return Gem::Result::NotImplemented;
    }

    //---------------------------------------------------------------------------------------------
    // Template method for adding chunks to a queue
    template<typename ChunkDataType>
    ChunkDataType* CRenderQueueManager::AddChunk(Canvas::RenderQueue* pQueue, Canvas::RenderChunkType type, uint32_t flags)
    {
        if (!pQueue)
            return nullptr;
            
        constexpr uint32_t chunkDataSize = sizeof(ChunkDataType);
        const uint32_t totalChunkSize = sizeof(Canvas::RenderChunkHeader) + chunkDataSize;
        
        // Check if we have enough space
        if (pQueue->UsedSize + totalChunkSize > pQueue->MaxSize || !pQueue->pChunkBuffer)
            return nullptr;
            
        // Get pointer to new chunk location
        Canvas::RenderChunkHeader* pHeader = reinterpret_cast<Canvas::RenderChunkHeader*>(pQueue->pChunkBuffer + pQueue->UsedSize);
        
        // Initialize chunk header
        new (pHeader) Canvas::RenderChunkHeader(type, chunkDataSize, flags);
        
        // Update queue state
        pQueue->UsedSize += totalChunkSize;
        ++pQueue->ChunkCount;
        
        // Return pointer to chunk data
        return static_cast<ChunkDataType*>(pHeader->GetData());
    }

    //---------------------------------------------------------------------------------------------
    Canvas::MeshChunkData* CRenderQueueManager::CreateMeshChunk(
        Canvas::RenderQueue* pQueue,
        XGfxBuffer* pVertexBuffer,
        XGfxBuffer* pIndexBuffer,
        uint32_t vertexCount,
        uint32_t indexCount,
        const Canvas::Math::FloatMatrix4x4& transform,
        const Canvas::Math::AABB& localBounds,
        uint32_t materialId,
        uint32_t renderFlags)
    {
        Canvas::MeshChunkData* pChunk = AddChunk<Canvas::MeshChunkData>(pQueue, Canvas::RenderChunkType::Mesh);
        if (pChunk)
        {
            pChunk->Common.Transform = transform;
            pChunk->Common.BoundingBox = localBounds; // TODO: Transform to world space
            pChunk->Common.MaterialId = materialId;
            pChunk->Common.RenderFlags = renderFlags;
            
            pChunk->pVertexBuffer = pVertexBuffer;
            pChunk->pIndexBuffer = pIndexBuffer;
            pChunk->VertexCount = vertexCount;
            pChunk->IndexCount = indexCount;
            // Other fields remain default-initialized
        }
        return pChunk;
    }

    //---------------------------------------------------------------------------------------------
    Canvas::UIChunkData* CRenderQueueManager::CreateUIChunk(
        Canvas::RenderQueue* pQueue,
        const Canvas::Math::FloatVector4& screenRect,
        const Canvas::Math::FloatVector4& color,
        uint32_t textureId,
        float depth)
    {
        Canvas::UIChunkData* pChunk = AddChunk<Canvas::UIChunkData>(pQueue, Canvas::RenderChunkType::UI);
        if (pChunk)
        {
            pChunk->ScreenRect = screenRect;
            pChunk->Color = color;
            pChunk->TextureId = textureId;
            pChunk->Depth = depth;
            // Set appropriate render flags for UI
            pChunk->Common.RenderFlags = Canvas::RENDER_FLAG_UI_ELEMENT | Canvas::RENDER_FLAG_ALPHA_BLEND;
        }
        return pChunk;
    }

    //---------------------------------------------------------------------------------------------
    Canvas::ParticleChunkData* CRenderQueueManager::CreateParticleChunk(
        Canvas::RenderQueue* pQueue,
        XGfxBuffer* pParticleBuffer,
        uint32_t maxParticles,
        uint32_t activeParticles,
        const Canvas::Math::FloatMatrix4x4& transform)
    {
        Canvas::ParticleChunkData* pChunk = AddChunk<Canvas::ParticleChunkData>(pQueue, Canvas::RenderChunkType::Particles);
        if (pChunk)
        {
            pChunk->Common.Transform = transform;
            pChunk->pParticleBuffer = pParticleBuffer;
            pChunk->MaxParticles = maxParticles;
            pChunk->ActiveParticles = activeParticles;
            // Set appropriate render flags for particles
            pChunk->Common.RenderFlags = Canvas::RENDER_FLAG_ALPHA_BLEND | Canvas::RENDER_FLAG_NO_CULL;
        }
        return pChunk;
    }
}