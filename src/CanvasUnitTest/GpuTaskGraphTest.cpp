#include "pch.h"
#include "CppUnitTest.h"
#include "GpuTask.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Canvas;

namespace CanvasUnitTest
{
    // Helper to create a D3D12 device for GPU task tests
    static HRESULT CreateGpuTaskTestDevice(ID3D12Device** ppDevice)
    {
        HRESULT hr = S_OK;
#if defined(DEBUG)
        CComPtr<ID3D12Debug3> pDebug;
        hr = D3D12GetDebugInterface(IID_PPV_ARGS(&pDebug));
        if (SUCCEEDED(hr) && pDebug)
            pDebug->EnableDebugLayer();
#endif
        CComPtr<ID3D12Device> pDevice;
        hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&pDevice));
        if (SUCCEEDED(hr))
            *ppDevice = pDevice.Detach();
        return hr;
    }

    // Helper to create a committed texture resource
    static HRESULT CreateTestTexture(ID3D12Device* pDevice, ID3D12Resource** ppResource,
        D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
        UINT width = 256, UINT height = 256)
    {
        D3D12_HEAP_PROPERTIES heapProp = {};
        heapProp.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Flags = flags;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

        return pDevice->CreateCommittedResource(
            &heapProp, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COMMON, nullptr,
            IID_PPV_ARGS(ppResource));
    }

    // Helper to create a committed buffer resource
    static HRESULT CreateTestBuffer(ID3D12Device* pDevice, ID3D12Resource** ppResource,
        UINT64 sizeInBytes = 4096)
    {
        D3D12_HEAP_PROPERTIES heapProp = {};
        heapProp.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = sizeInBytes;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        return pDevice->CreateCommittedResource(
            &heapProp, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COMMON, nullptr,
            IID_PPV_ARGS(ppResource));
    }

    TEST_CLASS(GpuTaskGraphTest)
    {
    public:

        //--------------------------------------------------------------------
        // Basic task creation
        //--------------------------------------------------------------------
        TEST_METHOD(EmptyGraphHasNoTasks)
        {
            CGpuTaskGraph graph;
            Assert::AreEqual(0u, graph.GetTaskCount());
        }

        TEST_METHOD(SingleTaskCreated)
        {
            CGpuTaskGraph graph;
            GpuTaskHandle task = graph.CreateTask("TestTask");
            Assert::AreNotEqual(InvalidGpuTaskHandle, task);
            Assert::AreEqual(1u, graph.GetTaskCount());
        }

        TEST_METHOD(TaskNamePreserved)
        {
            CGpuTaskGraph graph;
            GpuTaskHandle task = graph.CreateTask("MyPass");
            Assert::AreEqual(std::string("MyPass"), graph.GetTask(task).Name);
        }

        //--------------------------------------------------------------------
        // Barrier resolution: write hazards produce barriers
        //--------------------------------------------------------------------
        TEST_METHOD(ReadAfterWriteProducesBarrier)
        {
            CComPtr<ID3D12Device> pDevice;
            Assert::IsTrue(SUCCEEDED(CreateGpuTaskTestDevice(&pDevice)));

            CComPtr<ID3D12Resource> pTexture;
            Assert::IsTrue(SUCCEEDED(CreateTestTexture(pDevice, &pTexture)));

            CGpuTaskGraph graph;
            graph.SetInitialLayout(pTexture, D3D12_BARRIER_LAYOUT_COMMON);

            // Task 0 writes to texture
            GpuTaskHandle writer = graph.CreateTask("Writer");
            graph.DeclareTextureUsage(writer, pTexture,
                D3D12_BARRIER_LAYOUT_RENDER_TARGET,
                D3D12_BARRIER_SYNC_RENDER_TARGET,
                D3D12_BARRIER_ACCESS_RENDER_TARGET);
            auto writerBarriers = graph.PrepareTask(writer);

            // Task 1 reads from texture — should get RAW barrier
            GpuTaskHandle reader = graph.CreateTask("Reader");
            graph.DeclareTextureUsage(reader, pTexture,
                D3D12_BARRIER_LAYOUT_SHADER_RESOURCE,
                D3D12_BARRIER_SYNC_PIXEL_SHADING,
                D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            auto readerBarriers = graph.PrepareTask(reader);

            Assert::AreEqual(size_t(1), readerBarriers.TextureBarriers.size());
            Assert::IsTrue(readerBarriers.TextureBarriers[0].LayoutBefore == D3D12_BARRIER_LAYOUT_RENDER_TARGET);
            Assert::IsTrue(readerBarriers.TextureBarriers[0].LayoutAfter == D3D12_BARRIER_LAYOUT_SHADER_RESOURCE);
            Assert::IsTrue(readerBarriers.TextureBarriers[0].SyncBefore == D3D12_BARRIER_SYNC_RENDER_TARGET);
            Assert::IsTrue(readerBarriers.TextureBarriers[0].AccessBefore == D3D12_BARRIER_ACCESS_RENDER_TARGET);
        }

        TEST_METHOD(WriteAfterReadProducesBarrier)
        {
            CComPtr<ID3D12Device> pDevice;
            Assert::IsTrue(SUCCEEDED(CreateGpuTaskTestDevice(&pDevice)));

            CComPtr<ID3D12Resource> pTexture;
            Assert::IsTrue(SUCCEEDED(CreateTestTexture(pDevice, &pTexture)));

            CGpuTaskGraph graph;
            graph.SetInitialLayout(pTexture, D3D12_BARRIER_LAYOUT_SHADER_RESOURCE);

            // Task 0 reads
            GpuTaskHandle reader = graph.CreateTask("Reader");
            graph.DeclareTextureUsage(reader, pTexture,
                D3D12_BARRIER_LAYOUT_SHADER_RESOURCE,
                D3D12_BARRIER_SYNC_PIXEL_SHADING,
                D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            graph.PrepareTask(reader);

            // Task 1 writes — WAR hazard requires barrier
            GpuTaskHandle writer = graph.CreateTask("Writer");
            graph.DeclareTextureUsage(writer, pTexture,
                D3D12_BARRIER_LAYOUT_RENDER_TARGET,
                D3D12_BARRIER_SYNC_RENDER_TARGET,
                D3D12_BARRIER_ACCESS_RENDER_TARGET);
            auto writerBarriers = graph.PrepareTask(writer);

            Assert::AreEqual(size_t(1), writerBarriers.TextureBarriers.size());
            Assert::IsTrue(writerBarriers.TextureBarriers[0].LayoutBefore == D3D12_BARRIER_LAYOUT_SHADER_RESOURCE);
            Assert::IsTrue(writerBarriers.TextureBarriers[0].LayoutAfter == D3D12_BARRIER_LAYOUT_RENDER_TARGET);
        }

        TEST_METHOD(WriteAfterWriteProducesBarrier)
        {
            CComPtr<ID3D12Device> pDevice;
            Assert::IsTrue(SUCCEEDED(CreateGpuTaskTestDevice(&pDevice)));

            CComPtr<ID3D12Resource> pTexture;
            Assert::IsTrue(SUCCEEDED(CreateTestTexture(pDevice, &pTexture)));

            CGpuTaskGraph graph;
            graph.SetInitialLayout(pTexture, D3D12_BARRIER_LAYOUT_COMMON);

            GpuTaskHandle writer1 = graph.CreateTask("Writer1");
            graph.DeclareTextureUsage(writer1, pTexture,
                D3D12_BARRIER_LAYOUT_RENDER_TARGET,
                D3D12_BARRIER_SYNC_RENDER_TARGET,
                D3D12_BARRIER_ACCESS_RENDER_TARGET);
            graph.PrepareTask(writer1);

            GpuTaskHandle writer2 = graph.CreateTask("Writer2");
            graph.DeclareTextureUsage(writer2, pTexture,
                D3D12_BARRIER_LAYOUT_COPY_DEST,
                D3D12_BARRIER_SYNC_COPY,
                D3D12_BARRIER_ACCESS_COPY_DEST);
            auto w2Barriers = graph.PrepareTask(writer2);

            // WAW: barrier from RT -> COPY_DEST
            Assert::AreEqual(size_t(1), w2Barriers.TextureBarriers.size());
            Assert::IsTrue(w2Barriers.TextureBarriers[0].SyncBefore == D3D12_BARRIER_SYNC_RENDER_TARGET);
            Assert::IsTrue(w2Barriers.TextureBarriers[0].AccessBefore == D3D12_BARRIER_ACCESS_RENDER_TARGET);
        }

        TEST_METHOD(IndependentTasksNoBarriers)
        {
            CComPtr<ID3D12Device> pDevice;
            Assert::IsTrue(SUCCEEDED(CreateGpuTaskTestDevice(&pDevice)));

            CComPtr<ID3D12Resource> pTexA;
            CComPtr<ID3D12Resource> pTexB;
            Assert::IsTrue(SUCCEEDED(CreateTestTexture(pDevice, &pTexA)));
            Assert::IsTrue(SUCCEEDED(CreateTestTexture(pDevice, &pTexB)));

            CGpuTaskGraph graph;
            graph.SetInitialLayout(pTexA, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
            graph.SetInitialLayout(pTexB, D3D12_BARRIER_LAYOUT_RENDER_TARGET);

            // Two tasks writing to different textures at same layout — no inter-task barrier needed
            // (each gets its own layout barrier from COMMON if layout differs, or write barrier)
            GpuTaskHandle taskA = graph.CreateTask("TaskA");
            graph.DeclareTextureUsage(taskA, pTexA,
                D3D12_BARRIER_LAYOUT_RENDER_TARGET,
                D3D12_BARRIER_SYNC_RENDER_TARGET,
                D3D12_BARRIER_ACCESS_RENDER_TARGET);
            auto aBarriers = graph.PrepareTask(taskA);
            // First write at same layout from ECL boundary — still gets a barrier because it's a write
            Assert::AreEqual(size_t(1), aBarriers.TextureBarriers.size());

            GpuTaskHandle taskB = graph.CreateTask("TaskB");
            graph.DeclareTextureUsage(taskB, pTexB,
                D3D12_BARRIER_LAYOUT_RENDER_TARGET,
                D3D12_BARRIER_SYNC_RENDER_TARGET,
                D3D12_BARRIER_ACCESS_RENDER_TARGET);
            auto bBarriers = graph.PrepareTask(taskB);
            // TaskB writes to different resource — its barrier is independent of taskA
            Assert::AreEqual(size_t(1), bBarriers.TextureBarriers.size());
        }

        TEST_METHOD(MultipleReadersAfterWriter)
        {
            CComPtr<ID3D12Device> pDevice;
            Assert::IsTrue(SUCCEEDED(CreateGpuTaskTestDevice(&pDevice)));

            CComPtr<ID3D12Resource> pTexture;
            Assert::IsTrue(SUCCEEDED(CreateTestTexture(pDevice, &pTexture)));

            CGpuTaskGraph graph;
            graph.SetInitialLayout(pTexture, D3D12_BARRIER_LAYOUT_COMMON);

            GpuTaskHandle writer = graph.CreateTask("Writer");
            graph.DeclareTextureUsage(writer, pTexture,
                D3D12_BARRIER_LAYOUT_RENDER_TARGET,
                D3D12_BARRIER_SYNC_RENDER_TARGET,
                D3D12_BARRIER_ACCESS_RENDER_TARGET);
            graph.PrepareTask(writer);

            // First reader gets barrier (layout RT -> SR)
            GpuTaskHandle reader1 = graph.CreateTask("Reader1");
            graph.DeclareTextureUsage(reader1, pTexture,
                D3D12_BARRIER_LAYOUT_SHADER_RESOURCE,
                D3D12_BARRIER_SYNC_PIXEL_SHADING,
                D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            auto r1Barriers = graph.PrepareTask(reader1);
            Assert::AreEqual(size_t(1), r1Barriers.TextureBarriers.size());

            // Second reader at same layout — no barrier needed
            GpuTaskHandle reader2 = graph.CreateTask("Reader2");
            graph.DeclareTextureUsage(reader2, pTexture,
                D3D12_BARRIER_LAYOUT_SHADER_RESOURCE,
                D3D12_BARRIER_SYNC_PIXEL_SHADING,
                D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            auto r2Barriers = graph.PrepareTask(reader2);
            Assert::AreEqual(size_t(0), r2Barriers.TextureBarriers.size());
        }

        //--------------------------------------------------------------------
        // Diamond dependency pattern
        //--------------------------------------------------------------------
        TEST_METHOD(DiamondDependencyPattern)
        {
            CComPtr<ID3D12Device> pDevice;
            Assert::IsTrue(SUCCEEDED(CreateGpuTaskTestDevice(&pDevice)));

            CComPtr<ID3D12Resource> pTexA;
            CComPtr<ID3D12Resource> pTexB;
            CComPtr<ID3D12Resource> pTexC;
            Assert::IsTrue(SUCCEEDED(CreateTestTexture(pDevice, &pTexA)));
            Assert::IsTrue(SUCCEEDED(CreateTestTexture(pDevice, &pTexB)));
            Assert::IsTrue(SUCCEEDED(CreateTestTexture(pDevice, &pTexC)));

            CGpuTaskGraph graph;
            graph.SetInitialLayout(pTexA, D3D12_BARRIER_LAYOUT_COMMON);
            graph.SetInitialLayout(pTexB, D3D12_BARRIER_LAYOUT_COMMON);
            graph.SetInitialLayout(pTexC, D3D12_BARRIER_LAYOUT_COMMON);

            // T0 writes A amd B
            GpuTaskHandle t0 = graph.CreateTask("T0");
            graph.DeclareTextureUsage(t0, pTexA,
                D3D12_BARRIER_LAYOUT_RENDER_TARGET, D3D12_BARRIER_SYNC_RENDER_TARGET, D3D12_BARRIER_ACCESS_RENDER_TARGET);
            graph.DeclareTextureUsage(t0, pTexB,
                D3D12_BARRIER_LAYOUT_RENDER_TARGET, D3D12_BARRIER_SYNC_RENDER_TARGET, D3D12_BARRIER_ACCESS_RENDER_TARGET);
            graph.PrepareTask(t0);

            // T1 reads A, writes C
            GpuTaskHandle t1 = graph.CreateTask("T1");
            graph.DeclareTextureUsage(t1, pTexA,
                D3D12_BARRIER_LAYOUT_SHADER_RESOURCE, D3D12_BARRIER_SYNC_PIXEL_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            graph.DeclareTextureUsage(t1, pTexC,
                D3D12_BARRIER_LAYOUT_RENDER_TARGET, D3D12_BARRIER_SYNC_RENDER_TARGET, D3D12_BARRIER_ACCESS_RENDER_TARGET);
            auto t1Barriers = graph.PrepareTask(t1);
            // T1 should get barrier for A (RT->SR) and C (COMMON->RT)
            Assert::AreEqual(size_t(2), t1Barriers.TextureBarriers.size());

            // T2 reads B
            GpuTaskHandle t2 = graph.CreateTask("T2");
            graph.DeclareTextureUsage(t2, pTexB,
                D3D12_BARRIER_LAYOUT_SHADER_RESOURCE, D3D12_BARRIER_SYNC_PIXEL_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            auto t2Barriers = graph.PrepareTask(t2);
            // T2 should get barrier for B (RT->SR)
            Assert::AreEqual(size_t(1), t2Barriers.TextureBarriers.size());

            // T3 reads C and B (B is already SR from T2, C needs RT->SR from T1)
            GpuTaskHandle t3 = graph.CreateTask("T3");
            graph.DeclareTextureUsage(t3, pTexC,
                D3D12_BARRIER_LAYOUT_SHADER_RESOURCE, D3D12_BARRIER_SYNC_PIXEL_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            graph.DeclareTextureUsage(t3, pTexB,
                D3D12_BARRIER_LAYOUT_SHADER_RESOURCE, D3D12_BARRIER_SYNC_PIXEL_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            auto t3Barriers = graph.PrepareTask(t3);
            // C needs barrier (RT->SR), B is already SR (no barrier)
            Assert::AreEqual(size_t(1), t3Barriers.TextureBarriers.size());
            Assert::IsTrue(t3Barriers.TextureBarriers[0].LayoutBefore == D3D12_BARRIER_LAYOUT_RENDER_TARGET);
            Assert::IsTrue(t3Barriers.TextureBarriers[0].LayoutAfter == D3D12_BARRIER_LAYOUT_SHADER_RESOURCE);

            // Verify final layouts
            graph.ComputeFinalLayouts();
            const auto& finals = graph.GetFinalLayouts();
            Assert::IsTrue(finals.at(pTexA).GetLayout(0xFFFFFFFF) == D3D12_BARRIER_LAYOUT_SHADER_RESOURCE);
            Assert::IsTrue(finals.at(pTexB).GetLayout(0xFFFFFFFF) == D3D12_BARRIER_LAYOUT_SHADER_RESOURCE);
            Assert::IsTrue(finals.at(pTexC).GetLayout(0xFFFFFFFF) == D3D12_BARRIER_LAYOUT_SHADER_RESOURCE);
        }

        //--------------------------------------------------------------------
        // Barrier resolution
        //--------------------------------------------------------------------
        TEST_METHOD(BarrierResolvedForLayoutTransition)
        {
            CComPtr<ID3D12Device> pDevice;
            Assert::IsTrue(SUCCEEDED(CreateGpuTaskTestDevice(&pDevice)));

            CComPtr<ID3D12Resource> pTexture;
            Assert::IsTrue(SUCCEEDED(CreateTestTexture(pDevice, &pTexture)));

            CGpuTaskGraph graph;
            graph.SetInitialLayout(pTexture, D3D12_BARRIER_LAYOUT_COMMON);

            GpuTaskHandle writer = graph.CreateTask("Writer");
            graph.DeclareTextureUsage(writer, pTexture,
                D3D12_BARRIER_LAYOUT_RENDER_TARGET,
                D3D12_BARRIER_SYNC_RENDER_TARGET,
                D3D12_BARRIER_ACCESS_RENDER_TARGET);
            auto writerBarriers = graph.PrepareTask(writer);

            // Writer: COMMON -> RENDER_TARGET
            Assert::AreEqual(size_t(1), writerBarriers.TextureBarriers.size());
            Assert::IsTrue(writerBarriers.TextureBarriers[0].LayoutBefore == D3D12_BARRIER_LAYOUT_COMMON);
            Assert::IsTrue(writerBarriers.TextureBarriers[0].LayoutAfter == D3D12_BARRIER_LAYOUT_RENDER_TARGET);

            GpuTaskHandle reader = graph.CreateTask("Reader");
            graph.DeclareTextureUsage(reader, pTexture,
                D3D12_BARRIER_LAYOUT_SHADER_RESOURCE,
                D3D12_BARRIER_SYNC_PIXEL_SHADING,
                D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            auto readerBarriers = graph.PrepareTask(reader);

            // Reader: RENDER_TARGET -> SHADER_RESOURCE
            Assert::AreEqual(size_t(1), readerBarriers.TextureBarriers.size());
            Assert::IsTrue(readerBarriers.TextureBarriers[0].LayoutBefore == D3D12_BARRIER_LAYOUT_RENDER_TARGET);
            Assert::IsTrue(readerBarriers.TextureBarriers[0].LayoutAfter == D3D12_BARRIER_LAYOUT_SHADER_RESOURCE);
        }

        TEST_METHOD(NoBarrierWhenLayoutUnchangedAndReadOnly)
        {
            CComPtr<ID3D12Device> pDevice;
            Assert::IsTrue(SUCCEEDED(CreateGpuTaskTestDevice(&pDevice)));

            CComPtr<ID3D12Resource> pTexture;
            Assert::IsTrue(SUCCEEDED(CreateTestTexture(pDevice, &pTexture)));

            CGpuTaskGraph graph;
            graph.SetInitialLayout(pTexture, D3D12_BARRIER_LAYOUT_SHADER_RESOURCE);

            GpuTaskHandle task = graph.CreateTask("SameLayout");
            graph.DeclareTextureUsage(task, pTexture,
                D3D12_BARRIER_LAYOUT_SHADER_RESOURCE,
                D3D12_BARRIER_SYNC_PIXEL_SHADING,
                D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            auto barriers = graph.PrepareTask(task);

            // No barrier: layout unchanged, read-only, ECL boundary guarantees visibility
            Assert::AreEqual(size_t(0), barriers.TextureBarriers.size());
        }

        //--------------------------------------------------------------------
        // Final layout tracking
        //--------------------------------------------------------------------
        TEST_METHOD(FinalLayoutsReflectLastUsage)
        {
            CComPtr<ID3D12Device> pDevice;
            Assert::IsTrue(SUCCEEDED(CreateGpuTaskTestDevice(&pDevice)));

            CComPtr<ID3D12Resource> pTexture;
            Assert::IsTrue(SUCCEEDED(CreateTestTexture(pDevice, &pTexture)));

            CGpuTaskGraph graph;
            graph.SetInitialLayout(pTexture, D3D12_BARRIER_LAYOUT_COMMON);

            GpuTaskHandle writer = graph.CreateTask("Writer");
            graph.DeclareTextureUsage(writer, pTexture,
                D3D12_BARRIER_LAYOUT_RENDER_TARGET,
                D3D12_BARRIER_SYNC_RENDER_TARGET,
                D3D12_BARRIER_ACCESS_RENDER_TARGET);
            graph.PrepareTask(writer);

            GpuTaskHandle reader = graph.CreateTask("Reader");
            graph.DeclareTextureUsage(reader, pTexture,
                D3D12_BARRIER_LAYOUT_SHADER_RESOURCE,
                D3D12_BARRIER_SYNC_PIXEL_SHADING,
                D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            graph.PrepareTask(reader);

            graph.ComputeFinalLayouts();
            const auto& finalLayouts = graph.GetFinalLayouts();
            auto it = finalLayouts.find(pTexture);
            Assert::IsTrue(it != finalLayouts.end());
            Assert::IsTrue(it->second.GetLayout(0xFFFFFFFF) == D3D12_BARRIER_LAYOUT_SHADER_RESOURCE);
        }

        //--------------------------------------------------------------------
        // Explicit dependencies (reserved for future use)
        //--------------------------------------------------------------------
        TEST_METHOD(ExplicitDependencyAccepted)
        {
            CGpuTaskGraph graph;
            GpuTaskHandle taskA = graph.CreateTask("TaskA");
            GpuTaskHandle taskB = graph.CreateTask("TaskB");
            // Should not throw
            graph.AddExplicitDependency(taskB, taskA);
        }

        //--------------------------------------------------------------------
        // Buffer usage
        //--------------------------------------------------------------------
        TEST_METHOD(BufferReadAfterWriteProducesBarrier)
        {
            CComPtr<ID3D12Device> pDevice;
            Assert::IsTrue(SUCCEEDED(CreateGpuTaskTestDevice(&pDevice)));

            CComPtr<ID3D12Resource> pBuffer;
            Assert::IsTrue(SUCCEEDED(CreateTestBuffer(pDevice, &pBuffer)));

            CGpuTaskGraph graph;

            GpuTaskHandle writer = graph.CreateTask("BufferWriter");
            graph.DeclareBufferUsage(writer, pBuffer,
                D3D12_BARRIER_SYNC_COPY,
                D3D12_BARRIER_ACCESS_COPY_DEST);
            graph.PrepareTask(writer);

            GpuTaskHandle reader = graph.CreateTask("BufferReader");
            graph.DeclareBufferUsage(reader, pBuffer,
                D3D12_BARRIER_SYNC_PIXEL_SHADING,
                D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            auto readerBarriers = graph.PrepareTask(reader);

            Assert::AreEqual(size_t(1), readerBarriers.BufferBarriers.size());
            Assert::IsTrue(readerBarriers.BufferBarriers[0].SyncBefore == D3D12_BARRIER_SYNC_COPY);
            Assert::IsTrue(readerBarriers.BufferBarriers[0].SyncAfter == D3D12_BARRIER_SYNC_PIXEL_SHADING);
            Assert::IsTrue(readerBarriers.BufferBarriers[0].AccessBefore == D3D12_BARRIER_ACCESS_COPY_DEST);
            Assert::IsTrue(readerBarriers.BufferBarriers[0].AccessAfter == D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
        }

        //--------------------------------------------------------------------
        // Reset
        //--------------------------------------------------------------------
        TEST_METHOD(ResetClearsAllState)
        {
            CGpuTaskGraph graph;
            GpuTaskHandle t1 = graph.CreateTask("Task1");
            GpuTaskHandle t2 = graph.CreateTask("Task2");
            graph.PrepareTask(t1);
            graph.PrepareTask(t2);
            Assert::AreEqual(2u, graph.GetTaskCount());

            graph.Reset();
            Assert::AreEqual(0u, graph.GetTaskCount());
            Assert::IsTrue(graph.GetFinalLayouts().empty());
        }

        //--------------------------------------------------------------------
        // Complex multi-pass rendering scenario
        //--------------------------------------------------------------------
        TEST_METHOD(MultiPassRenderingScenario)
        {
            CComPtr<ID3D12Device> pDevice;
            Assert::IsTrue(SUCCEEDED(CreateGpuTaskTestDevice(&pDevice)));

            CComPtr<ID3D12Resource> pShadowMap;
            CComPtr<ID3D12Resource> pGBuffer;
            CComPtr<ID3D12Resource> pBackBuffer;
            Assert::IsTrue(SUCCEEDED(CreateTestTexture(pDevice, &pShadowMap)));
            Assert::IsTrue(SUCCEEDED(CreateTestTexture(pDevice, &pGBuffer)));
            Assert::IsTrue(SUCCEEDED(CreateTestTexture(pDevice, &pBackBuffer)));

            CGpuTaskGraph graph;
            graph.SetInitialLayout(pShadowMap, D3D12_BARRIER_LAYOUT_COMMON);
            graph.SetInitialLayout(pGBuffer, D3D12_BARRIER_LAYOUT_COMMON);
            graph.SetInitialLayout(pBackBuffer, D3D12_BARRIER_LAYOUT_COMMON);

            // Shadow pass: writes shadow map
            GpuTaskHandle shadowPass = graph.CreateTask("ShadowPass");
            graph.DeclareTextureUsage(shadowPass, pShadowMap,
                D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE,
                D3D12_BARRIER_SYNC_DEPTH_STENCIL,
                D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE);
            graph.PrepareTask(shadowPass);

            // GBuffer pass: writes GBuffer
            GpuTaskHandle gbufferPass = graph.CreateTask("GBufferPass");
            graph.DeclareTextureUsage(gbufferPass, pGBuffer,
                D3D12_BARRIER_LAYOUT_RENDER_TARGET,
                D3D12_BARRIER_SYNC_RENDER_TARGET,
                D3D12_BARRIER_ACCESS_RENDER_TARGET);
            graph.PrepareTask(gbufferPass);

            // Lighting pass: reads shadow + GBuffer, writes back buffer
            GpuTaskHandle lightingPass = graph.CreateTask("LightingPass");
            graph.DeclareTextureUsage(lightingPass, pShadowMap,
                D3D12_BARRIER_LAYOUT_SHADER_RESOURCE,
                D3D12_BARRIER_SYNC_PIXEL_SHADING,
                D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            graph.DeclareTextureUsage(lightingPass, pGBuffer,
                D3D12_BARRIER_LAYOUT_SHADER_RESOURCE,
                D3D12_BARRIER_SYNC_PIXEL_SHADING,
                D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            graph.DeclareTextureUsage(lightingPass, pBackBuffer,
                D3D12_BARRIER_LAYOUT_RENDER_TARGET,
                D3D12_BARRIER_SYNC_RENDER_TARGET,
                D3D12_BARRIER_ACCESS_RENDER_TARGET);
            auto lightingBarriers = graph.PrepareTask(lightingPass);
            // Should have barriers for shadow (DSW->SR), GBuffer (RT->SR), backBuffer (COMMON->RT)
            Assert::AreEqual(size_t(3), lightingBarriers.TextureBarriers.size());

            // Post-process: writes back buffer as UAV
            GpuTaskHandle postProcess = graph.CreateTask("PostProcess");
            graph.DeclareTextureUsage(postProcess, pBackBuffer,
                D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS,
                D3D12_BARRIER_SYNC_COMPUTE_SHADING,
                D3D12_BARRIER_ACCESS_UNORDERED_ACCESS);
            auto postBarriers = graph.PrepareTask(postProcess);
            // RT -> UAV barrier on back buffer
            Assert::AreEqual(size_t(1), postBarriers.TextureBarriers.size());

            // Verify final layouts
            graph.ComputeFinalLayouts();
            const auto& finals = graph.GetFinalLayouts();
            Assert::IsTrue(finals.at(pShadowMap).GetLayout(0xFFFFFFFF) == D3D12_BARRIER_LAYOUT_SHADER_RESOURCE);
            Assert::IsTrue(finals.at(pGBuffer).GetLayout(0xFFFFFFFF) == D3D12_BARRIER_LAYOUT_SHADER_RESOURCE);
            Assert::IsTrue(finals.at(pBackBuffer).GetLayout(0xFFFFFFFF) == D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS);
        }

        //--------------------------------------------------------------------
        // Task sync scope accumulation
        //--------------------------------------------------------------------
        TEST_METHOD(TaskSyncScopeAccumulated)
        {
            CComPtr<ID3D12Device> pDevice;
            Assert::IsTrue(SUCCEEDED(CreateGpuTaskTestDevice(&pDevice)));

            CComPtr<ID3D12Resource> pTexture;
            CComPtr<ID3D12Resource> pBuffer;
            Assert::IsTrue(SUCCEEDED(CreateTestTexture(pDevice, &pTexture)));
            Assert::IsTrue(SUCCEEDED(CreateTestBuffer(pDevice, &pBuffer)));

            CGpuTaskGraph graph;
            graph.SetInitialLayout(pTexture, D3D12_BARRIER_LAYOUT_COMMON);

            GpuTaskHandle task = graph.CreateTask("MultiResourceTask");
            graph.DeclareTextureUsage(task, pTexture,
                D3D12_BARRIER_LAYOUT_RENDER_TARGET,
                D3D12_BARRIER_SYNC_RENDER_TARGET,
                D3D12_BARRIER_ACCESS_RENDER_TARGET);
            graph.DeclareBufferUsage(task, pBuffer,
                D3D12_BARRIER_SYNC_VERTEX_SHADING,
                D3D12_BARRIER_ACCESS_VERTEX_BUFFER);
            graph.PrepareTask(task);

            // SyncScope = RENDER_TARGET | VERTEX_SHADING
            const auto& t = graph.GetTask(task);
            Assert::IsTrue((t.SyncScope & D3D12_BARRIER_SYNC_RENDER_TARGET) != 0);
            Assert::IsTrue((t.SyncScope & D3D12_BARRIER_SYNC_VERTEX_SHADING) != 0);

            // AccessScope = RENDER_TARGET | VERTEX_BUFFER
            Assert::IsTrue((t.AccessScope & D3D12_BARRIER_ACCESS_RENDER_TARGET) != 0);
            Assert::IsTrue((t.AccessScope & D3D12_BARRIER_ACCESS_VERTEX_BUFFER) != 0);
        }

        //--------------------------------------------------------------------
        // Concurrent reads at same layout produce no intermediate barrier
        //--------------------------------------------------------------------
        TEST_METHOD(ReadReadSameLayoutNoBarrier)
        {
            CComPtr<ID3D12Device> pDevice;
            Assert::IsTrue(SUCCEEDED(CreateGpuTaskTestDevice(&pDevice)));

            CComPtr<ID3D12Resource> pTexture;
            Assert::IsTrue(SUCCEEDED(CreateTestTexture(pDevice, &pTexture)));

            CGpuTaskGraph graph;
            graph.SetInitialLayout(pTexture, D3D12_BARRIER_LAYOUT_COMMON);

            // Writer transitions to RT
            GpuTaskHandle writer = graph.CreateTask("Writer");
            graph.DeclareTextureUsage(writer, pTexture,
                D3D12_BARRIER_LAYOUT_RENDER_TARGET,
                D3D12_BARRIER_SYNC_RENDER_TARGET,
                D3D12_BARRIER_ACCESS_RENDER_TARGET);
            graph.PrepareTask(writer);

            // First reader: RT -> SR (gets barrier)
            GpuTaskHandle readerPS = graph.CreateTask("ReaderPS");
            graph.DeclareTextureUsage(readerPS, pTexture,
                D3D12_BARRIER_LAYOUT_SHADER_RESOURCE,
                D3D12_BARRIER_SYNC_PIXEL_SHADING,
                D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            auto psBarriers = graph.PrepareTask(readerPS);
            Assert::AreEqual(size_t(1), psBarriers.TextureBarriers.size());
            Assert::IsTrue(psBarriers.TextureBarriers[0].LayoutBefore == D3D12_BARRIER_LAYOUT_RENDER_TARGET);
            Assert::IsTrue(psBarriers.TextureBarriers[0].LayoutAfter == D3D12_BARRIER_LAYOUT_SHADER_RESOURCE);
            Assert::IsTrue(psBarriers.TextureBarriers[0].SyncBefore == D3D12_BARRIER_SYNC_RENDER_TARGET);
            Assert::IsTrue(psBarriers.TextureBarriers[0].AccessBefore == D3D12_BARRIER_ACCESS_RENDER_TARGET);

            // Second reader: same layout, both reads — no barrier
            GpuTaskHandle readerVS = graph.CreateTask("ReaderVS");
            graph.DeclareTextureUsage(readerVS, pTexture,
                D3D12_BARRIER_LAYOUT_SHADER_RESOURCE,
                D3D12_BARRIER_SYNC_VERTEX_SHADING,
                D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            auto vsBarriers = graph.PrepareTask(readerVS);
            Assert::AreEqual(size_t(0), vsBarriers.TextureBarriers.size());
        }

        //--------------------------------------------------------------------
        // Write after multiple reads — barrier SyncBefore unions all readers
        //--------------------------------------------------------------------
        TEST_METHOD(WriteAfterMultipleReadsBarrierUnionsSync)
        {
            CComPtr<ID3D12Device> pDevice;
            Assert::IsTrue(SUCCEEDED(CreateGpuTaskTestDevice(&pDevice)));

            CComPtr<ID3D12Resource> pTexture;
            Assert::IsTrue(SUCCEEDED(CreateTestTexture(pDevice, &pTexture)));

            CGpuTaskGraph graph;
            graph.SetInitialLayout(pTexture, D3D12_BARRIER_LAYOUT_COMMON);

            GpuTaskHandle writer = graph.CreateTask("Writer");
            graph.DeclareTextureUsage(writer, pTexture,
                D3D12_BARRIER_LAYOUT_RENDER_TARGET,
                D3D12_BARRIER_SYNC_RENDER_TARGET,
                D3D12_BARRIER_ACCESS_RENDER_TARGET);
            graph.PrepareTask(writer);

            GpuTaskHandle readerPS = graph.CreateTask("ReaderPS");
            graph.DeclareTextureUsage(readerPS, pTexture,
                D3D12_BARRIER_LAYOUT_SHADER_RESOURCE,
                D3D12_BARRIER_SYNC_PIXEL_SHADING,
                D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            graph.PrepareTask(readerPS);

            GpuTaskHandle readerVS = graph.CreateTask("ReaderVS");
            graph.DeclareTextureUsage(readerVS, pTexture,
                D3D12_BARRIER_LAYOUT_SHADER_RESOURCE,
                D3D12_BARRIER_SYNC_VERTEX_SHADING,
                D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            graph.PrepareTask(readerVS);

            // Second writer must wait for both readers
            GpuTaskHandle writer2 = graph.CreateTask("Writer2");
            graph.DeclareTextureUsage(writer2, pTexture,
                D3D12_BARRIER_LAYOUT_RENDER_TARGET,
                D3D12_BARRIER_SYNC_RENDER_TARGET,
                D3D12_BARRIER_ACCESS_RENDER_TARGET);
            auto w2Barriers = graph.PrepareTask(writer2);

            Assert::AreEqual(size_t(1), w2Barriers.TextureBarriers.size());

            D3D12_BARRIER_SYNC syncBefore = w2Barriers.TextureBarriers[0].SyncBefore;
            Assert::IsTrue((syncBefore & D3D12_BARRIER_SYNC_PIXEL_SHADING) != 0,
                L"SyncBefore must include PIXEL_SHADING from first reader");
            Assert::IsTrue((syncBefore & D3D12_BARRIER_SYNC_VERTEX_SHADING) != 0,
                L"SyncBefore must include VERTEX_SHADING from second reader");

            Assert::IsTrue(w2Barriers.TextureBarriers[0].LayoutBefore == D3D12_BARRIER_LAYOUT_SHADER_RESOURCE);
            Assert::IsTrue(w2Barriers.TextureBarriers[0].LayoutAfter == D3D12_BARRIER_LAYOUT_RENDER_TARGET);
        }

        //--------------------------------------------------------------------
        // First-use write still gets a barrier (layout transition)
        //--------------------------------------------------------------------
        TEST_METHOD(FirstUseWriteGetsLayoutBarrier)
        {
            CComPtr<ID3D12Device> pDevice;
            Assert::IsTrue(SUCCEEDED(CreateGpuTaskTestDevice(&pDevice)));

            CComPtr<ID3D12Resource> pTexture;
            Assert::IsTrue(SUCCEEDED(CreateTestTexture(pDevice, &pTexture)));

            CGpuTaskGraph graph;
            graph.SetInitialLayout(pTexture, D3D12_BARRIER_LAYOUT_COMMON);

            GpuTaskHandle task = graph.CreateTask("FirstWrite");
            graph.DeclareTextureUsage(task, pTexture,
                D3D12_BARRIER_LAYOUT_RENDER_TARGET,
                D3D12_BARRIER_SYNC_RENDER_TARGET,
                D3D12_BARRIER_ACCESS_RENDER_TARGET);
            auto barriers = graph.PrepareTask(task);

            // Barrier needed: layout COMMON -> RT
            Assert::AreEqual(size_t(1), barriers.TextureBarriers.size());
            Assert::IsTrue(barriers.TextureBarriers[0].LayoutBefore == D3D12_BARRIER_LAYOUT_COMMON);
            Assert::IsTrue(barriers.TextureBarriers[0].LayoutAfter == D3D12_BARRIER_LAYOUT_RENDER_TARGET);
            // SyncBefore/AccessBefore are NONE/NO_ACCESS (ECL boundary)
            Assert::IsTrue(barriers.TextureBarriers[0].SyncBefore == D3D12_BARRIER_SYNC_NONE);
            Assert::IsTrue(barriers.TextureBarriers[0].AccessBefore == D3D12_BARRIER_ACCESS_NO_ACCESS);
        }
    };
}
