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
            auto& task = graph.CreateTask("TestTask");
            Assert::IsNotNull(&task);
            Assert::AreEqual(1u, graph.GetTaskCount());
        }

        TEST_METHOD(TaskNamePreserved)
        {
            CGpuTaskGraph graph;
            auto& task = graph.CreateTask("MyPass");
            Assert::AreEqual(std::string("MyPass"), task.Name);
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
            auto& writer = graph.CreateTask("Writer");
            graph.DeclareTextureUsage(writer, pTexture,
                D3D12_BARRIER_LAYOUT_RENDER_TARGET,
                D3D12_BARRIER_SYNC_RENDER_TARGET,
                D3D12_BARRIER_ACCESS_RENDER_TARGET);
            auto writerBarriers = graph.PrepareTask(writer);

            // Task 1 reads from texture — depends on writer, should get RAW barrier
            auto& reader = graph.CreateTask("Reader");
            graph.AddDependency(reader, writer);
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
            auto& reader = graph.CreateTask("Reader");
            graph.DeclareTextureUsage(reader, pTexture,
                D3D12_BARRIER_LAYOUT_SHADER_RESOURCE,
                D3D12_BARRIER_SYNC_PIXEL_SHADING,
                D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            graph.PrepareTask(reader);

            // Task 1 writes — WAR hazard requires barrier
            auto& writer = graph.CreateTask("Writer");
            graph.AddDependency(writer, reader);
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

            auto& writer1 = graph.CreateTask("Writer1");
            graph.DeclareTextureUsage(writer1, pTexture,
                D3D12_BARRIER_LAYOUT_RENDER_TARGET,
                D3D12_BARRIER_SYNC_RENDER_TARGET,
                D3D12_BARRIER_ACCESS_RENDER_TARGET);
            graph.PrepareTask(writer1);

            auto& writer2 = graph.CreateTask("Writer2");
            graph.AddDependency(writer2, writer1);
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

            // Two tasks writing to different textures at same layout — no inter-task barrier needed.
            // Same layout from ECL boundary: no barrier needed (ECL boundary provides sync).
            auto& taskA = graph.CreateTask("TaskA");
            graph.DeclareTextureUsage(taskA, pTexA,
                D3D12_BARRIER_LAYOUT_RENDER_TARGET,
                D3D12_BARRIER_SYNC_RENDER_TARGET,
                D3D12_BARRIER_ACCESS_RENDER_TARGET);
            auto aBarriers = graph.PrepareTask(taskA);
            // Same layout, no dep chain → ECL boundary handles sync, no barrier needed
            Assert::AreEqual(size_t(0), aBarriers.TextureBarriers.size());

            auto& taskB = graph.CreateTask("TaskB");
            graph.DeclareTextureUsage(taskB, pTexB,
                D3D12_BARRIER_LAYOUT_RENDER_TARGET,
                D3D12_BARRIER_SYNC_RENDER_TARGET,
                D3D12_BARRIER_ACCESS_RENDER_TARGET);
            auto bBarriers = graph.PrepareTask(taskB);
            // Same layout, no dep chain → ECL boundary handles sync, no barrier needed
            Assert::AreEqual(size_t(0), bBarriers.TextureBarriers.size());
        }

        TEST_METHOD(MultipleReadersAfterWriter)
        {
            CComPtr<ID3D12Device> pDevice;
            Assert::IsTrue(SUCCEEDED(CreateGpuTaskTestDevice(&pDevice)));

            CComPtr<ID3D12Resource> pTexture;
            Assert::IsTrue(SUCCEEDED(CreateTestTexture(pDevice, &pTexture)));

            CGpuTaskGraph graph;
            graph.SetInitialLayout(pTexture, D3D12_BARRIER_LAYOUT_COMMON);

            auto& writer = graph.CreateTask("Writer");
            graph.DeclareTextureUsage(writer, pTexture,
                D3D12_BARRIER_LAYOUT_RENDER_TARGET,
                D3D12_BARRIER_SYNC_RENDER_TARGET,
                D3D12_BARRIER_ACCESS_RENDER_TARGET);
            graph.PrepareTask(writer);

            // First reader gets barrier (layout RT -> SR)
            auto& reader1 = graph.CreateTask("Reader1");
            graph.AddDependency(reader1, writer);
            graph.DeclareTextureUsage(reader1, pTexture,
                D3D12_BARRIER_LAYOUT_SHADER_RESOURCE,
                D3D12_BARRIER_SYNC_PIXEL_SHADING,
                D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            auto r1Barriers = graph.PrepareTask(reader1);
            Assert::AreEqual(size_t(1), r1Barriers.TextureBarriers.size());

            // Second reader at same layout, also depends on writer — no barrier (already transitioned by reader1's dep)
            auto& reader2 = graph.CreateTask("Reader2");
            graph.AddDependency(reader2, reader1);
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
            auto& t0 = graph.CreateTask("T0");
            graph.DeclareTextureUsage(t0, pTexA,
                D3D12_BARRIER_LAYOUT_RENDER_TARGET, D3D12_BARRIER_SYNC_RENDER_TARGET, D3D12_BARRIER_ACCESS_RENDER_TARGET);
            graph.DeclareTextureUsage(t0, pTexB,
                D3D12_BARRIER_LAYOUT_RENDER_TARGET, D3D12_BARRIER_SYNC_RENDER_TARGET, D3D12_BARRIER_ACCESS_RENDER_TARGET);
            graph.PrepareTask(t0);

            // T1 reads A, writes C — depends on T0
            auto& t1 = graph.CreateTask("T1");
            graph.AddDependency(t1, t0);
            graph.DeclareTextureUsage(t1, pTexA,
                D3D12_BARRIER_LAYOUT_SHADER_RESOURCE, D3D12_BARRIER_SYNC_PIXEL_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            graph.DeclareTextureUsage(t1, pTexC,
                D3D12_BARRIER_LAYOUT_RENDER_TARGET, D3D12_BARRIER_SYNC_RENDER_TARGET, D3D12_BARRIER_ACCESS_RENDER_TARGET);
            auto t1Barriers = graph.PrepareTask(t1);
            // T1 should get barrier for A (RT->SR) and C (COMMON->RT)
            Assert::AreEqual(size_t(2), t1Barriers.TextureBarriers.size());

            // T2 reads B — depends on T0
            auto& t2 = graph.CreateTask("T2");
            graph.AddDependency(t2, t0);
            graph.DeclareTextureUsage(t2, pTexB,
                D3D12_BARRIER_LAYOUT_SHADER_RESOURCE, D3D12_BARRIER_SYNC_PIXEL_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            auto t2Barriers = graph.PrepareTask(t2);
            // T2 should get barrier for B (RT->SR)
            Assert::AreEqual(size_t(1), t2Barriers.TextureBarriers.size());

            // T3 reads C and B — depends on T1 and T2
            auto& t3 = graph.CreateTask("T3");
            graph.AddDependency(t3, t1);
            graph.AddDependency(t3, t2);
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

            auto& writer = graph.CreateTask("Writer");
            graph.DeclareTextureUsage(writer, pTexture,
                D3D12_BARRIER_LAYOUT_RENDER_TARGET,
                D3D12_BARRIER_SYNC_RENDER_TARGET,
                D3D12_BARRIER_ACCESS_RENDER_TARGET);
            auto writerBarriers = graph.PrepareTask(writer);

            // Writer: COMMON -> RENDER_TARGET
            Assert::AreEqual(size_t(1), writerBarriers.TextureBarriers.size());
            Assert::IsTrue(writerBarriers.TextureBarriers[0].LayoutBefore == D3D12_BARRIER_LAYOUT_COMMON);
            Assert::IsTrue(writerBarriers.TextureBarriers[0].LayoutAfter == D3D12_BARRIER_LAYOUT_RENDER_TARGET);

            auto& reader = graph.CreateTask("Reader");
            graph.AddDependency(reader, writer);
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

            auto& task = graph.CreateTask("SameLayout");
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

            auto& writer = graph.CreateTask("Writer");
            graph.DeclareTextureUsage(writer, pTexture,
                D3D12_BARRIER_LAYOUT_RENDER_TARGET,
                D3D12_BARRIER_SYNC_RENDER_TARGET,
                D3D12_BARRIER_ACCESS_RENDER_TARGET);
            graph.PrepareTask(writer);

            auto& reader = graph.CreateTask("Reader");
            graph.AddDependency(reader, writer);
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
        // Dependencies
        //--------------------------------------------------------------------
        TEST_METHOD(DependencyAccepted)
        {
            CGpuTaskGraph graph;
            auto& taskA = graph.CreateTask("TaskA");
            auto& taskB = graph.CreateTask("TaskB");
            graph.AddDependency(taskB, taskA);
            Assert::AreEqual(size_t(1), taskB.Dependencies.size());
            Assert::IsTrue(taskB.Dependencies[0] == &taskA);
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

            auto& writer = graph.CreateTask("BufferWriter");
            graph.DeclareBufferUsage(writer, pBuffer,
                D3D12_BARRIER_SYNC_COPY,
                D3D12_BARRIER_ACCESS_COPY_DEST);
            graph.PrepareTask(writer);

            auto& reader = graph.CreateTask("BufferReader");
            graph.AddDependency(reader, writer);
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
            auto& t1 = graph.CreateTask("Task1");
            auto& t2 = graph.CreateTask("Task2");
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
            auto& shadowPass = graph.CreateTask("ShadowPass");
            graph.DeclareTextureUsage(shadowPass, pShadowMap,
                D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE,
                D3D12_BARRIER_SYNC_DEPTH_STENCIL,
                D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE);
            graph.PrepareTask(shadowPass);

            // GBuffer pass: writes GBuffer
            auto& gbufferPass = graph.CreateTask("GBufferPass");
            graph.DeclareTextureUsage(gbufferPass, pGBuffer,
                D3D12_BARRIER_LAYOUT_RENDER_TARGET,
                D3D12_BARRIER_SYNC_RENDER_TARGET,
                D3D12_BARRIER_ACCESS_RENDER_TARGET);
            graph.PrepareTask(gbufferPass);

            // Lighting pass: reads shadow + GBuffer, writes back buffer — depends on both
            auto& lightingPass = graph.CreateTask("LightingPass");
            graph.AddDependency(lightingPass, shadowPass);
            graph.AddDependency(lightingPass, gbufferPass);
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

            // Post-process: writes back buffer as UAV — depends on lighting
            auto& postProcess = graph.CreateTask("PostProcess");
            graph.AddDependency(postProcess, lightingPass);
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
            auto& writer = graph.CreateTask("Writer");
            graph.DeclareTextureUsage(writer, pTexture,
                D3D12_BARRIER_LAYOUT_RENDER_TARGET,
                D3D12_BARRIER_SYNC_RENDER_TARGET,
                D3D12_BARRIER_ACCESS_RENDER_TARGET);
            graph.PrepareTask(writer);

            // First reader: RT -> SR (gets barrier) — depends on writer
            auto& readerPS = graph.CreateTask("ReaderPS");
            graph.AddDependency(readerPS, writer);
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

            // Second reader: same layout, both reads — depends on first reader, no barrier
            auto& readerVS = graph.CreateTask("ReaderVS");
            graph.AddDependency(readerVS, readerPS);
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

            auto& writer = graph.CreateTask("Writer");
            graph.DeclareTextureUsage(writer, pTexture,
                D3D12_BARRIER_LAYOUT_RENDER_TARGET,
                D3D12_BARRIER_SYNC_RENDER_TARGET,
                D3D12_BARRIER_ACCESS_RENDER_TARGET);
            graph.PrepareTask(writer);

            auto& readerPS = graph.CreateTask("ReaderPS");
            graph.AddDependency(readerPS, writer);
            graph.DeclareTextureUsage(readerPS, pTexture,
                D3D12_BARRIER_LAYOUT_SHADER_RESOURCE,
                D3D12_BARRIER_SYNC_PIXEL_SHADING,
                D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            graph.PrepareTask(readerPS);

            auto& readerVS = graph.CreateTask("ReaderVS");
            graph.AddDependency(readerVS, writer);
            graph.DeclareTextureUsage(readerVS, pTexture,
                D3D12_BARRIER_LAYOUT_SHADER_RESOURCE,
                D3D12_BARRIER_SYNC_VERTEX_SHADING,
                D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            graph.PrepareTask(readerVS);

            // Second writer must wait for both readers
            auto& writer2 = graph.CreateTask("Writer2");
            graph.AddDependency(writer2, readerPS);
            graph.AddDependency(writer2, readerVS);
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

            auto& task = graph.CreateTask("FirstWrite");
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

        //====================================================================
        // DAG-specific tests
        //====================================================================

        //--------------------------------------------------------------------
        // Task Ids are assigned sequentially starting from 0
        //--------------------------------------------------------------------
        TEST_METHOD(TaskAddressesStableAcrossCreation)
        {
            CGpuTaskGraph graph;
            auto& t0 = graph.CreateTask("T0");
            auto& t1 = graph.CreateTask("T1");
            auto& t2 = graph.CreateTask("T2");
            // deque guarantees these addresses remain valid
            Assert::AreEqual(std::string("T0"), t0.Name);
            Assert::AreEqual(std::string("T1"), t1.Name);
            Assert::AreEqual(std::string("T2"), t2.Name);
        }

        //--------------------------------------------------------------------
        // Task Ids reset to 0 after Reset(), pool reused
        //--------------------------------------------------------------------
        TEST_METHOD(TaskRefsStableAfterReuse)
        {
            CGpuTaskGraph graph;
            graph.CreateTask("Frame1_T0");
            graph.CreateTask("Frame1_T1");
            Assert::AreEqual(2u, graph.GetTaskCount());

            graph.Reset();
            Assert::AreEqual(0u, graph.GetTaskCount());

            auto& t0 = graph.CreateTask("Frame2_T0");
            auto& t1 = graph.CreateTask("Frame2_T1");
            Assert::AreEqual(std::string("Frame2_T0"), t0.Name);
            Assert::AreEqual(std::string("Frame2_T1"), t1.Name);
            Assert::IsTrue(t0.Dependencies.empty());
        }

        //--------------------------------------------------------------------
        // Multiple dependencies on a single task
        //--------------------------------------------------------------------
        TEST_METHOD(MultipleDependenciesStored)
        {
            CGpuTaskGraph graph;
            auto& t0 = graph.CreateTask("T0");
            auto& t1 = graph.CreateTask("T1");
            auto& t2 = graph.CreateTask("T2");
            graph.AddDependency(t2, t0);
            graph.AddDependency(t2, t1);

            Assert::AreEqual(size_t(2), t2.Dependencies.size());
            Assert::IsTrue(t2.Dependencies[0] == &t0);
            Assert::IsTrue(t2.Dependencies[1] == &t1);
        }

        //--------------------------------------------------------------------
        // Fan-out: one writer, two independent readers on different resources
        // No conflicting deps because each reader touches a different resource
        //--------------------------------------------------------------------
        TEST_METHOD(FanOutNoConflict)
        {
            CComPtr<ID3D12Device> pDevice;
            Assert::IsTrue(SUCCEEDED(CreateGpuTaskTestDevice(&pDevice)));

            CComPtr<ID3D12Resource> pTexA;
            CComPtr<ID3D12Resource> pTexB;
            Assert::IsTrue(SUCCEEDED(CreateTestTexture(pDevice, &pTexA)));
            Assert::IsTrue(SUCCEEDED(CreateTestTexture(pDevice, &pTexB)));

            CGpuTaskGraph graph;
            graph.SetInitialLayout(pTexA, D3D12_BARRIER_LAYOUT_COMMON);
            graph.SetInitialLayout(pTexB, D3D12_BARRIER_LAYOUT_COMMON);

            // T0: writes both A and B
            auto& t0 = graph.CreateTask("Producer");
            graph.DeclareTextureUsage(t0, pTexA,
                D3D12_BARRIER_LAYOUT_RENDER_TARGET, D3D12_BARRIER_SYNC_RENDER_TARGET, D3D12_BARRIER_ACCESS_RENDER_TARGET);
            graph.DeclareTextureUsage(t0, pTexB,
                D3D12_BARRIER_LAYOUT_RENDER_TARGET, D3D12_BARRIER_SYNC_RENDER_TARGET, D3D12_BARRIER_ACCESS_RENDER_TARGET);
            graph.PrepareTask(t0);

            // T1: reads A only, depends on T0
            auto& t1 = graph.CreateTask("ConsumerA");
            graph.AddDependency(t1, t0);
            graph.DeclareTextureUsage(t1, pTexA,
                D3D12_BARRIER_LAYOUT_SHADER_RESOURCE, D3D12_BARRIER_SYNC_PIXEL_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            auto t1b = graph.PrepareTask(t1);
            Assert::AreEqual(size_t(1), t1b.TextureBarriers.size());
            Assert::IsTrue(t1b.TextureBarriers[0].pResource == pTexA);

            // T2: reads B only, depends on T0
            auto& t2 = graph.CreateTask("ConsumerB");
            graph.AddDependency(t2, t0);
            graph.DeclareTextureUsage(t2, pTexB,
                D3D12_BARRIER_LAYOUT_SHADER_RESOURCE, D3D12_BARRIER_SYNC_PIXEL_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            auto t2b = graph.PrepareTask(t2);
            Assert::AreEqual(size_t(1), t2b.TextureBarriers.size());
            Assert::IsTrue(t2b.TextureBarriers[0].pResource == pTexB);
        }

        //--------------------------------------------------------------------
        // Fan-in: two writers to different resources, one reader of both
        //--------------------------------------------------------------------
        TEST_METHOD(FanInDependenciesBarriersCorrect)
        {
            CComPtr<ID3D12Device> pDevice;
            Assert::IsTrue(SUCCEEDED(CreateGpuTaskTestDevice(&pDevice)));

            CComPtr<ID3D12Resource> pTexA;
            CComPtr<ID3D12Resource> pTexB;
            Assert::IsTrue(SUCCEEDED(CreateTestTexture(pDevice, &pTexA)));
            Assert::IsTrue(SUCCEEDED(CreateTestTexture(pDevice, &pTexB)));

            CGpuTaskGraph graph;
            graph.SetInitialLayout(pTexA, D3D12_BARRIER_LAYOUT_COMMON);
            graph.SetInitialLayout(pTexB, D3D12_BARRIER_LAYOUT_COMMON);

            // T0: writes A
            auto& t0 = graph.CreateTask("WriteA");
            graph.DeclareTextureUsage(t0, pTexA,
                D3D12_BARRIER_LAYOUT_RENDER_TARGET, D3D12_BARRIER_SYNC_RENDER_TARGET, D3D12_BARRIER_ACCESS_RENDER_TARGET);
            graph.PrepareTask(t0);

            // T1: writes B
            auto& t1 = graph.CreateTask("WriteB");
            graph.DeclareTextureUsage(t1, pTexB,
                D3D12_BARRIER_LAYOUT_RENDER_TARGET, D3D12_BARRIER_SYNC_RENDER_TARGET, D3D12_BARRIER_ACCESS_RENDER_TARGET);
            graph.PrepareTask(t1);

            // T2: reads both A and B, depends on both T0 and T1
            auto& t2 = graph.CreateTask("ReadBoth");
            graph.AddDependency(t2, t0);
            graph.AddDependency(t2, t1);
            graph.DeclareTextureUsage(t2, pTexA,
                D3D12_BARRIER_LAYOUT_SHADER_RESOURCE, D3D12_BARRIER_SYNC_PIXEL_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            graph.DeclareTextureUsage(t2, pTexB,
                D3D12_BARRIER_LAYOUT_SHADER_RESOURCE, D3D12_BARRIER_SYNC_PIXEL_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            auto t2b = graph.PrepareTask(t2);

            // Two barriers: A (RT->SR) and B (RT->SR)
            Assert::AreEqual(size_t(2), t2b.TextureBarriers.size());
        }

        //--------------------------------------------------------------------
        // Long chain: A→B→C→D, each writes same resource in different layout.
        // Each task should barrier against its immediate predecessor only.
        //--------------------------------------------------------------------
        TEST_METHOD(LongChainBarriersFromPredecessor)
        {
            CComPtr<ID3D12Device> pDevice;
            Assert::IsTrue(SUCCEEDED(CreateGpuTaskTestDevice(&pDevice)));

            CComPtr<ID3D12Resource> pTex;
            Assert::IsTrue(SUCCEEDED(CreateTestTexture(pDevice, &pTex,
                D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)));

            CGpuTaskGraph graph;
            graph.SetInitialLayout(pTex, D3D12_BARRIER_LAYOUT_COMMON);

            // A: COMMON -> RT
            auto& a = graph.CreateTask("A");
            graph.DeclareTextureUsage(a, pTex,
                D3D12_BARRIER_LAYOUT_RENDER_TARGET, D3D12_BARRIER_SYNC_RENDER_TARGET, D3D12_BARRIER_ACCESS_RENDER_TARGET);
            auto ab = graph.PrepareTask(a);
            Assert::AreEqual(size_t(1), ab.TextureBarriers.size());
            Assert::IsTrue(ab.TextureBarriers[0].LayoutBefore == D3D12_BARRIER_LAYOUT_COMMON);

            // B: RT -> SR, depends on A
            auto& b = graph.CreateTask("B");
            graph.AddDependency(b, a);
            graph.DeclareTextureUsage(b, pTex,
                D3D12_BARRIER_LAYOUT_SHADER_RESOURCE, D3D12_BARRIER_SYNC_PIXEL_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            auto bb = graph.PrepareTask(b);
            Assert::AreEqual(size_t(1), bb.TextureBarriers.size());
            Assert::IsTrue(bb.TextureBarriers[0].LayoutBefore == D3D12_BARRIER_LAYOUT_RENDER_TARGET);
            Assert::IsTrue(bb.TextureBarriers[0].SyncBefore == D3D12_BARRIER_SYNC_RENDER_TARGET);

            // C: SR -> UAV, depends on B
            auto& c = graph.CreateTask("C");
            graph.AddDependency(c, b);
            graph.DeclareTextureUsage(c, pTex,
                D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS, D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_UNORDERED_ACCESS);
            auto cb = graph.PrepareTask(c);
            Assert::AreEqual(size_t(1), cb.TextureBarriers.size());
            Assert::IsTrue(cb.TextureBarriers[0].LayoutBefore == D3D12_BARRIER_LAYOUT_SHADER_RESOURCE);
            Assert::IsTrue(cb.TextureBarriers[0].SyncBefore == D3D12_BARRIER_SYNC_PIXEL_SHADING);

            // D: UAV -> COPY_DEST, depends on C
            auto& d = graph.CreateTask("D");
            graph.AddDependency(d, c);
            graph.DeclareTextureUsage(d, pTex,
                D3D12_BARRIER_LAYOUT_COPY_DEST, D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_ACCESS_COPY_DEST);
            auto db = graph.PrepareTask(d);
            Assert::AreEqual(size_t(1), db.TextureBarriers.size());
            Assert::IsTrue(db.TextureBarriers[0].LayoutBefore == D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS);
            Assert::IsTrue(db.TextureBarriers[0].SyncBefore == D3D12_BARRIER_SYNC_COMPUTE_SHADING);

            // Final layout = COPY_DEST
            graph.ComputeFinalLayouts();
            Assert::IsTrue(graph.GetFinalLayouts().at(pTex).GetLayout(0xFFFFFFFF) == D3D12_BARRIER_LAYOUT_COPY_DEST);
        }

        //--------------------------------------------------------------------
        // Diamond with explicit dependencies and mixed resources
        //
        //   T0 (writes A+B)
        //  /                \
        // T1 (reads A)    T2 (reads B)
        //  \                /
        //   T3 (reads A+B, writes C)
        //--------------------------------------------------------------------
        TEST_METHOD(DiamondWithExplicitDeps)
        {
            CComPtr<ID3D12Device> pDevice;
            Assert::IsTrue(SUCCEEDED(CreateGpuTaskTestDevice(&pDevice)));

            CComPtr<ID3D12Resource> pTexA, pTexB, pTexC;
            Assert::IsTrue(SUCCEEDED(CreateTestTexture(pDevice, &pTexA)));
            Assert::IsTrue(SUCCEEDED(CreateTestTexture(pDevice, &pTexB)));
            Assert::IsTrue(SUCCEEDED(CreateTestTexture(pDevice, &pTexC)));

            CGpuTaskGraph graph;
            graph.SetInitialLayout(pTexA, D3D12_BARRIER_LAYOUT_COMMON);
            graph.SetInitialLayout(pTexB, D3D12_BARRIER_LAYOUT_COMMON);
            graph.SetInitialLayout(pTexC, D3D12_BARRIER_LAYOUT_COMMON);

            auto& t0 = graph.CreateTask("T0_WriteAB");
            graph.DeclareTextureUsage(t0, pTexA,
                D3D12_BARRIER_LAYOUT_RENDER_TARGET, D3D12_BARRIER_SYNC_RENDER_TARGET, D3D12_BARRIER_ACCESS_RENDER_TARGET);
            graph.DeclareTextureUsage(t0, pTexB,
                D3D12_BARRIER_LAYOUT_RENDER_TARGET, D3D12_BARRIER_SYNC_RENDER_TARGET, D3D12_BARRIER_ACCESS_RENDER_TARGET);
            graph.PrepareTask(t0);

            auto& t1 = graph.CreateTask("T1_ReadA");
            graph.AddDependency(t1, t0);
            graph.DeclareTextureUsage(t1, pTexA,
                D3D12_BARRIER_LAYOUT_SHADER_RESOURCE, D3D12_BARRIER_SYNC_PIXEL_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            auto t1b = graph.PrepareTask(t1);
            // A: RT->SR
            Assert::AreEqual(size_t(1), t1b.TextureBarriers.size());

            auto& t2 = graph.CreateTask("T2_ReadB");
            graph.AddDependency(t2, t0);
            graph.DeclareTextureUsage(t2, pTexB,
                D3D12_BARRIER_LAYOUT_SHADER_RESOURCE, D3D12_BARRIER_SYNC_PIXEL_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            auto t2b = graph.PrepareTask(t2);
            // B: RT->SR
            Assert::AreEqual(size_t(1), t2b.TextureBarriers.size());

            // T3 depends on T1 and T2 — both leave their respective textures in SR
            auto& t3 = graph.CreateTask("T3_ReadAB_WriteC");
            graph.AddDependency(t3, t1);
            graph.AddDependency(t3, t2);
            graph.DeclareTextureUsage(t3, pTexA,
                D3D12_BARRIER_LAYOUT_SHADER_RESOURCE, D3D12_BARRIER_SYNC_PIXEL_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            graph.DeclareTextureUsage(t3, pTexB,
                D3D12_BARRIER_LAYOUT_SHADER_RESOURCE, D3D12_BARRIER_SYNC_PIXEL_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            graph.DeclareTextureUsage(t3, pTexC,
                D3D12_BARRIER_LAYOUT_RENDER_TARGET, D3D12_BARRIER_SYNC_RENDER_TARGET, D3D12_BARRIER_ACCESS_RENDER_TARGET);
            auto t3b = graph.PrepareTask(t3);

            // A and B are already in SR from T1/T2 (read-read, no barrier).
            // C goes COMMON->RT (write, layout change = 1 barrier).
            Assert::AreEqual(size_t(1), t3b.TextureBarriers.size());
            Assert::IsTrue(t3b.TextureBarriers[0].pResource == pTexC);
            Assert::IsTrue(t3b.TextureBarriers[0].LayoutBefore == D3D12_BARRIER_LAYOUT_COMMON);
            Assert::IsTrue(t3b.TextureBarriers[0].LayoutAfter == D3D12_BARRIER_LAYOUT_RENDER_TARGET);

            graph.ComputeFinalLayouts();
            Assert::IsTrue(graph.GetFinalLayouts().at(pTexA).GetLayout(0xFFFFFFFF) == D3D12_BARRIER_LAYOUT_SHADER_RESOURCE);
            Assert::IsTrue(graph.GetFinalLayouts().at(pTexB).GetLayout(0xFFFFFFFF) == D3D12_BARRIER_LAYOUT_SHADER_RESOURCE);
            Assert::IsTrue(graph.GetFinalLayouts().at(pTexC).GetLayout(0xFFFFFFFF) == D3D12_BARRIER_LAYOUT_RENDER_TARGET);
        }

        //--------------------------------------------------------------------
        // Mixed texture + buffer in a dependency chain
        //--------------------------------------------------------------------
        TEST_METHOD(MixedTextureBufferChain)
        {
            CComPtr<ID3D12Device> pDevice;
            Assert::IsTrue(SUCCEEDED(CreateGpuTaskTestDevice(&pDevice)));

            CComPtr<ID3D12Resource> pTex;
            CComPtr<ID3D12Resource> pBuf;
            Assert::IsTrue(SUCCEEDED(CreateTestTexture(pDevice, &pTex)));
            Assert::IsTrue(SUCCEEDED(CreateTestBuffer(pDevice, &pBuf)));

            CGpuTaskGraph graph;
            graph.SetInitialLayout(pTex, D3D12_BARRIER_LAYOUT_COMMON);

            // T0: compute shader writes buffer as UAV + writes texture as UAV
            auto& t0 = graph.CreateTask("Compute");
            graph.DeclareBufferUsage(t0, pBuf,
                D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_UNORDERED_ACCESS);
            graph.DeclareTextureUsage(t0, pTex,
                D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS, D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_UNORDERED_ACCESS);
            graph.PrepareTask(t0);

            // T1: reads buffer as VB, reads texture as SRV, depends on T0
            auto& t1 = graph.CreateTask("Draw");
            graph.AddDependency(t1, t0);
            graph.DeclareBufferUsage(t1, pBuf,
                D3D12_BARRIER_SYNC_VERTEX_SHADING, D3D12_BARRIER_ACCESS_VERTEX_BUFFER);
            graph.DeclareTextureUsage(t1, pTex,
                D3D12_BARRIER_LAYOUT_SHADER_RESOURCE, D3D12_BARRIER_SYNC_PIXEL_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            auto t1b = graph.PrepareTask(t1);

            // Should get both a buffer barrier (UAV->VB) and a texture barrier (UAV->SR)
            Assert::AreEqual(size_t(1), t1b.TextureBarriers.size());
            Assert::AreEqual(size_t(1), t1b.BufferBarriers.size());

            Assert::IsTrue(t1b.TextureBarriers[0].LayoutBefore == D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS);
            Assert::IsTrue(t1b.TextureBarriers[0].LayoutAfter == D3D12_BARRIER_LAYOUT_SHADER_RESOURCE);
            Assert::IsTrue(t1b.TextureBarriers[0].SyncBefore == D3D12_BARRIER_SYNC_COMPUTE_SHADING);

            Assert::IsTrue(t1b.BufferBarriers[0].SyncBefore == D3D12_BARRIER_SYNC_COMPUTE_SHADING);
            Assert::IsTrue(t1b.BufferBarriers[0].SyncAfter == D3D12_BARRIER_SYNC_VERTEX_SHADING);
            Assert::IsTrue(t1b.BufferBarriers[0].AccessBefore == D3D12_BARRIER_ACCESS_UNORDERED_ACCESS);
            Assert::IsTrue(t1b.BufferBarriers[0].AccessAfter == D3D12_BARRIER_ACCESS_VERTEX_BUFFER);
        }

        //--------------------------------------------------------------------
        // Present transition pattern: RT -> COMMON with SYNC_NONE/NO_ACCESS
        //--------------------------------------------------------------------
        TEST_METHOD(PresentTransitionToCommon)
        {
            CComPtr<ID3D12Device> pDevice;
            Assert::IsTrue(SUCCEEDED(CreateGpuTaskTestDevice(&pDevice)));

            CComPtr<ID3D12Resource> pBackBuffer;
            Assert::IsTrue(SUCCEEDED(CreateTestTexture(pDevice, &pBackBuffer)));

            CGpuTaskGraph graph;
            graph.SetInitialLayout(pBackBuffer, D3D12_BARRIER_LAYOUT_COMMON);

            // Render task writes to back buffer
            auto& renderTask = graph.CreateTask("Render");
            graph.DeclareTextureUsage(renderTask, pBackBuffer,
                D3D12_BARRIER_LAYOUT_RENDER_TARGET, D3D12_BARRIER_SYNC_RENDER_TARGET, D3D12_BARRIER_ACCESS_RENDER_TARGET);
            graph.PrepareTask(renderTask);

            // Present task: transition to COMMON
            auto& presentTask = graph.CreateTask("Present");
            graph.AddDependency(presentTask, renderTask);
            graph.DeclareTextureUsage(presentTask, pBackBuffer,
                D3D12_BARRIER_LAYOUT_COMMON, D3D12_BARRIER_SYNC_NONE, D3D12_BARRIER_ACCESS_NO_ACCESS);
            auto pb = graph.PrepareTask(presentTask);

            Assert::AreEqual(size_t(1), pb.TextureBarriers.size());
            Assert::IsTrue(pb.TextureBarriers[0].LayoutBefore == D3D12_BARRIER_LAYOUT_RENDER_TARGET);
            Assert::IsTrue(pb.TextureBarriers[0].LayoutAfter == D3D12_BARRIER_LAYOUT_COMMON);
            Assert::IsTrue(pb.TextureBarriers[0].SyncBefore == D3D12_BARRIER_SYNC_RENDER_TARGET);
            Assert::IsTrue(pb.TextureBarriers[0].AccessBefore == D3D12_BARRIER_ACCESS_RENDER_TARGET);

            graph.ComputeFinalLayouts();
            Assert::IsTrue(graph.GetFinalLayouts().at(pBackBuffer).GetLayout(0xFFFFFFFF) == D3D12_BARRIER_LAYOUT_COMMON);
        }

        //--------------------------------------------------------------------
        // First-use semantics: no SetInitialLayout, first task assumes layout
        //--------------------------------------------------------------------
        TEST_METHOD(FirstUseSemanticsNoInitialLayout)
        {
            CComPtr<ID3D12Device> pDevice;
            Assert::IsTrue(SUCCEEDED(CreateGpuTaskTestDevice(&pDevice)));

            CComPtr<ID3D12Resource> pTex;
            Assert::IsTrue(SUCCEEDED(CreateTestTexture(pDevice, &pTex)));

            CGpuTaskGraph graph;
            // Deliberately NOT calling SetInitialLayout

            auto& task = graph.CreateTask("FirstUse");
            graph.DeclareTextureUsage(task, pTex,
                D3D12_BARRIER_LAYOUT_RENDER_TARGET, D3D12_BARRIER_SYNC_RENDER_TARGET, D3D12_BARRIER_ACCESS_RENDER_TARGET);
            auto barriers = graph.PrepareTask(task);

            // No barrier: first-use assumes the required layout
            Assert::AreEqual(size_t(0), barriers.TextureBarriers.size());

            // InitialLayouts should record the assumed layout for fixup CL
            const auto& initials = graph.GetInitialLayouts();
            auto it = initials.find(pTex);
            Assert::IsTrue(it != initials.end());
            Assert::IsTrue(it->second.GetLayout(0xFFFFFFFF) == D3D12_BARRIER_LAYOUT_RENDER_TARGET);
        }

        //--------------------------------------------------------------------
        // Pool reuse preserves barrier correctness across frames
        //--------------------------------------------------------------------
        TEST_METHOD(PoolReuseBarriersCorrectAcrossFrames)
        {
            CComPtr<ID3D12Device> pDevice;
            Assert::IsTrue(SUCCEEDED(CreateGpuTaskTestDevice(&pDevice)));

            CComPtr<ID3D12Resource> pTex;
            Assert::IsTrue(SUCCEEDED(CreateTestTexture(pDevice, &pTex)));

            CGpuTaskGraph graph;

            // Frame 1: write then read
            graph.SetInitialLayout(pTex, D3D12_BARRIER_LAYOUT_COMMON);
            auto& f1w = graph.CreateTask("F1_Write");
            graph.DeclareTextureUsage(f1w, pTex,
                D3D12_BARRIER_LAYOUT_RENDER_TARGET, D3D12_BARRIER_SYNC_RENDER_TARGET, D3D12_BARRIER_ACCESS_RENDER_TARGET);
            graph.PrepareTask(f1w);

            auto& f1r = graph.CreateTask("F1_Read");
            graph.AddDependency(f1r, f1w);
            graph.DeclareTextureUsage(f1r, pTex,
                D3D12_BARRIER_LAYOUT_SHADER_RESOURCE, D3D12_BARRIER_SYNC_PIXEL_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            auto f1rb = graph.PrepareTask(f1r);
            Assert::AreEqual(size_t(1), f1rb.TextureBarriers.size());

            graph.ComputeFinalLayouts();
            graph.Reset();

            // Frame 2: same pattern, pool reused — should still work
            graph.SetInitialLayout(pTex, D3D12_BARRIER_LAYOUT_SHADER_RESOURCE);
            auto& f2w = graph.CreateTask("F2_Write");
            graph.DeclareTextureUsage(f2w, pTex,
                D3D12_BARRIER_LAYOUT_RENDER_TARGET, D3D12_BARRIER_SYNC_RENDER_TARGET, D3D12_BARRIER_ACCESS_RENDER_TARGET);
            auto f2wb = graph.PrepareTask(f2w);

            // SR->RT layout transition
            Assert::AreEqual(size_t(1), f2wb.TextureBarriers.size());
            Assert::IsTrue(f2wb.TextureBarriers[0].LayoutBefore == D3D12_BARRIER_LAYOUT_SHADER_RESOURCE);
            Assert::IsTrue(f2wb.TextureBarriers[0].LayoutAfter == D3D12_BARRIER_LAYOUT_RENDER_TARGET);

            auto& f2r = graph.CreateTask("F2_Read");
            graph.AddDependency(f2r, f2w);
            graph.DeclareTextureUsage(f2r, pTex,
                D3D12_BARRIER_LAYOUT_SHADER_RESOURCE, D3D12_BARRIER_SYNC_PIXEL_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            auto f2rb = graph.PrepareTask(f2r);
            Assert::AreEqual(size_t(1), f2rb.TextureBarriers.size());

            // Dependencies should be clean — no stale data from frame 1
            Assert::AreEqual(size_t(1), f2r.Dependencies.size());
            Assert::IsTrue(f2r.Dependencies[0] == &f2w);
        }

        //--------------------------------------------------------------------
        // Buffer write-after-write with dependency chain
        //--------------------------------------------------------------------
        TEST_METHOD(BufferWriteAfterWriteChain)
        {
            CComPtr<ID3D12Device> pDevice;
            Assert::IsTrue(SUCCEEDED(CreateGpuTaskTestDevice(&pDevice)));

            CComPtr<ID3D12Resource> pBuf;
            Assert::IsTrue(SUCCEEDED(CreateTestBuffer(pDevice, &pBuf)));

            CGpuTaskGraph graph;

            auto& t0 = graph.CreateTask("CopyWrite");
            graph.DeclareBufferUsage(t0, pBuf, D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_ACCESS_COPY_DEST);
            graph.PrepareTask(t0);

            auto& t1 = graph.CreateTask("UAVWrite");
            graph.AddDependency(t1, t0);
            graph.DeclareBufferUsage(t1, pBuf, D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_UNORDERED_ACCESS);
            auto t1b = graph.PrepareTask(t1);

            Assert::AreEqual(size_t(1), t1b.BufferBarriers.size());
            Assert::IsTrue(t1b.BufferBarriers[0].SyncBefore == D3D12_BARRIER_SYNC_COPY);
            Assert::IsTrue(t1b.BufferBarriers[0].AccessBefore == D3D12_BARRIER_ACCESS_COPY_DEST);
            Assert::IsTrue(t1b.BufferBarriers[0].SyncAfter == D3D12_BARRIER_SYNC_COMPUTE_SHADING);
            Assert::IsTrue(t1b.BufferBarriers[0].AccessAfter == D3D12_BARRIER_ACCESS_UNORDERED_ACCESS);

            auto& t2 = graph.CreateTask("ReadResult");
            graph.AddDependency(t2, t1);
            graph.DeclareBufferUsage(t2, pBuf, D3D12_BARRIER_SYNC_PIXEL_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            auto t2b = graph.PrepareTask(t2);

            Assert::AreEqual(size_t(1), t2b.BufferBarriers.size());
            Assert::IsTrue(t2b.BufferBarriers[0].SyncBefore == D3D12_BARRIER_SYNC_COMPUTE_SHADING);
            Assert::IsTrue(t2b.BufferBarriers[0].AccessBefore == D3D12_BARRIER_ACCESS_UNORDERED_ACCESS);
        }

        //--------------------------------------------------------------------
        // Realistic deferred rendering pipeline with deps:
        //   GBuffer → Composite → UI → Present
        //--------------------------------------------------------------------
        TEST_METHOD(DeferredPipelineWithDependencies)
        {
            CComPtr<ID3D12Device> pDevice;
            Assert::IsTrue(SUCCEEDED(CreateGpuTaskTestDevice(&pDevice)));

            CComPtr<ID3D12Resource> pNormals, pDiffuse, pDepth, pBackBuffer;
            Assert::IsTrue(SUCCEEDED(CreateTestTexture(pDevice, &pNormals)));
            Assert::IsTrue(SUCCEEDED(CreateTestTexture(pDevice, &pDiffuse)));
            Assert::IsTrue(SUCCEEDED(CreateTestTexture(pDevice, &pDepth)));
            Assert::IsTrue(SUCCEEDED(CreateTestTexture(pDevice, &pBackBuffer)));

            CGpuTaskGraph graph;
            graph.SetInitialLayout(pNormals, D3D12_BARRIER_LAYOUT_COMMON);
            graph.SetInitialLayout(pDiffuse, D3D12_BARRIER_LAYOUT_COMMON);
            graph.SetInitialLayout(pDepth, D3D12_BARRIER_LAYOUT_COMMON);
            graph.SetInitialLayout(pBackBuffer, D3D12_BARRIER_LAYOUT_COMMON);

            // GBuffer pass: write normals + diffuse + depth
            auto& gbuffer = graph.CreateTask("GBuffer");
            graph.DeclareTextureUsage(gbuffer, pNormals,
                D3D12_BARRIER_LAYOUT_RENDER_TARGET, D3D12_BARRIER_SYNC_RENDER_TARGET, D3D12_BARRIER_ACCESS_RENDER_TARGET);
            graph.DeclareTextureUsage(gbuffer, pDiffuse,
                D3D12_BARRIER_LAYOUT_RENDER_TARGET, D3D12_BARRIER_SYNC_RENDER_TARGET, D3D12_BARRIER_ACCESS_RENDER_TARGET);
            graph.DeclareTextureUsage(gbuffer, pDepth,
                D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE, D3D12_BARRIER_SYNC_DEPTH_STENCIL, D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE);
            graph.PrepareTask(gbuffer);

            // Composite: read normals + diffuse, write back buffer
            auto& composite = graph.CreateTask("Composite");
            graph.AddDependency(composite, gbuffer);
            graph.DeclareTextureUsage(composite, pNormals,
                D3D12_BARRIER_LAYOUT_SHADER_RESOURCE, D3D12_BARRIER_SYNC_PIXEL_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            graph.DeclareTextureUsage(composite, pDiffuse,
                D3D12_BARRIER_LAYOUT_SHADER_RESOURCE, D3D12_BARRIER_SYNC_PIXEL_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            graph.DeclareTextureUsage(composite, pBackBuffer,
                D3D12_BARRIER_LAYOUT_RENDER_TARGET, D3D12_BARRIER_SYNC_RENDER_TARGET, D3D12_BARRIER_ACCESS_RENDER_TARGET);
            auto compBarriers = graph.PrepareTask(composite);
            // 3 barriers: normals RT->SR, diffuse RT->SR, backbuffer COMMON->RT
            Assert::AreEqual(size_t(3), compBarriers.TextureBarriers.size());

            // UI overlay: reads back buffer as render target (same layout, write)
            auto& ui = graph.CreateTask("UI");
            graph.AddDependency(ui, composite);
            graph.DeclareTextureUsage(ui, pBackBuffer,
                D3D12_BARRIER_LAYOUT_RENDER_TARGET, D3D12_BARRIER_SYNC_RENDER_TARGET, D3D12_BARRIER_ACCESS_RENDER_TARGET);
            auto uiBarriers = graph.PrepareTask(ui);
            // WAW on back buffer at same layout — barrier needed for sync
            Assert::AreEqual(size_t(1), uiBarriers.TextureBarriers.size());

            // Present: RT -> COMMON
            auto& present = graph.CreateTask("Present");
            graph.AddDependency(present, ui);
            graph.DeclareTextureUsage(present, pBackBuffer,
                D3D12_BARRIER_LAYOUT_COMMON, D3D12_BARRIER_SYNC_NONE, D3D12_BARRIER_ACCESS_NO_ACCESS);
            auto presBarriers = graph.PrepareTask(present);
            Assert::AreEqual(size_t(1), presBarriers.TextureBarriers.size());
            Assert::IsTrue(presBarriers.TextureBarriers[0].LayoutBefore == D3D12_BARRIER_LAYOUT_RENDER_TARGET);
            Assert::IsTrue(presBarriers.TextureBarriers[0].LayoutAfter == D3D12_BARRIER_LAYOUT_COMMON);

            graph.ComputeFinalLayouts();
            Assert::IsTrue(graph.GetFinalLayouts().at(pBackBuffer).GetLayout(0xFFFFFFFF) == D3D12_BARRIER_LAYOUT_COMMON);
            Assert::IsTrue(graph.GetFinalLayouts().at(pNormals).GetLayout(0xFFFFFFFF) == D3D12_BARRIER_LAYOUT_SHADER_RESOURCE);
            Assert::IsTrue(graph.GetFinalLayouts().at(pDiffuse).GetLayout(0xFFFFFFFF) == D3D12_BARRIER_LAYOUT_SHADER_RESOURCE);
            Assert::IsTrue(graph.GetFinalLayouts().at(pDepth).GetLayout(0xFFFFFFFF) == D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE);
        }

        //====================================================================
        // Negative tests — invalid usage detected via GetLastError()
        //====================================================================

        //--------------------------------------------------------------------
        // AddDependency: self-dependency
        //--------------------------------------------------------------------
        TEST_METHOD(ErrorOnSelfDependency)
        {
            CGpuTaskGraph graph;
            auto& t0 = graph.CreateTask("Self");

            graph.AddDependency(t0, t0);
            Assert::IsFalse(graph.GetLastError().empty(),
                L"AddDependency should report an error for self-dependency");
            Assert::IsTrue(t0.Dependencies.empty());
        }

        //--------------------------------------------------------------------
        // DAG conflict: two deps both write same texture
        //--------------------------------------------------------------------
        TEST_METHOD(ErrorOnMultipleWriterDepsTexture)
        {
            CComPtr<ID3D12Device> pDevice;
            Assert::IsTrue(SUCCEEDED(CreateGpuTaskTestDevice(&pDevice)));

            CComPtr<ID3D12Resource> pTex;
            Assert::IsTrue(SUCCEEDED(CreateTestTexture(pDevice, &pTex)));

            CGpuTaskGraph graph;
            graph.SetInitialLayout(pTex, D3D12_BARRIER_LAYOUT_COMMON);

            auto& writerA = graph.CreateTask("WriterA");
            graph.DeclareTextureUsage(writerA, pTex,
                D3D12_BARRIER_LAYOUT_RENDER_TARGET, D3D12_BARRIER_SYNC_RENDER_TARGET, D3D12_BARRIER_ACCESS_RENDER_TARGET);
            graph.PrepareTask(writerA);

            auto& writerB = graph.CreateTask("WriterB");
            graph.DeclareTextureUsage(writerB, pTex,
                D3D12_BARRIER_LAYOUT_RENDER_TARGET, D3D12_BARRIER_SYNC_RENDER_TARGET, D3D12_BARRIER_ACCESS_RENDER_TARGET);
            graph.PrepareTask(writerB);

            // Join task depends on both writers of the same texture
            auto& join = graph.CreateTask("Join");
            graph.AddDependency(join, writerA);
            graph.AddDependency(join, writerB);
            graph.DeclareTextureUsage(join, pTex,
                D3D12_BARRIER_LAYOUT_SHADER_RESOURCE, D3D12_BARRIER_SYNC_PIXEL_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            graph.PrepareTask(join);

            Assert::IsFalse(graph.GetLastError().empty(),
                L"PrepareTask should report DAG conflict for multiple writer deps");
            // Verify error message names the conflicting tasks
            Assert::IsTrue(graph.GetLastError().find("WriterA") != std::string::npos);
            Assert::IsTrue(graph.GetLastError().find("WriterB") != std::string::npos);
        }

        //--------------------------------------------------------------------
        // DAG conflict: two deps leave same texture in different layouts
        //--------------------------------------------------------------------
        TEST_METHOD(ErrorOnConflictingLayoutDeps)
        {
            CComPtr<ID3D12Device> pDevice;
            Assert::IsTrue(SUCCEEDED(CreateGpuTaskTestDevice(&pDevice)));

            CComPtr<ID3D12Resource> pTex;
            Assert::IsTrue(SUCCEEDED(CreateTestTexture(pDevice, &pTex,
                D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)));

            CGpuTaskGraph graph;
            graph.SetInitialLayout(pTex, D3D12_BARRIER_LAYOUT_COMMON);

            // depA reads texture as SHADER_RESOURCE
            auto& depA = graph.CreateTask("DepA_SR");
            graph.DeclareTextureUsage(depA, pTex,
                D3D12_BARRIER_LAYOUT_SHADER_RESOURCE, D3D12_BARRIER_SYNC_PIXEL_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            graph.PrepareTask(depA);

            // depB reads texture as UNORDERED_ACCESS (different layout)
            auto& depB = graph.CreateTask("DepB_UAV");
            graph.DeclareTextureUsage(depB, pTex,
                D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS, D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_UNORDERED_ACCESS);
            graph.PrepareTask(depB);

            // Join depends on both — conflicting layouts
            auto& join = graph.CreateTask("Join");
            graph.AddDependency(join, depA);
            graph.AddDependency(join, depB);
            graph.DeclareTextureUsage(join, pTex,
                D3D12_BARRIER_LAYOUT_SHADER_RESOURCE, D3D12_BARRIER_SYNC_PIXEL_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            graph.PrepareTask(join);

            Assert::IsFalse(graph.GetLastError().empty(),
                L"PrepareTask should report DAG conflict for conflicting layout deps");
            Assert::IsTrue(graph.GetLastError().find("conflicting layouts") != std::string::npos);
        }

        //--------------------------------------------------------------------
        // DAG conflict: two deps both write same buffer
        //--------------------------------------------------------------------
        TEST_METHOD(ErrorOnMultipleWriterDepsBuffer)
        {
            CComPtr<ID3D12Device> pDevice;
            Assert::IsTrue(SUCCEEDED(CreateGpuTaskTestDevice(&pDevice)));

            CComPtr<ID3D12Resource> pBuf;
            Assert::IsTrue(SUCCEEDED(CreateTestBuffer(pDevice, &pBuf)));

            CGpuTaskGraph graph;

            auto& writerA = graph.CreateTask("BufWriterA");
            graph.DeclareBufferUsage(writerA, pBuf,
                D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_ACCESS_COPY_DEST);
            graph.PrepareTask(writerA);

            auto& writerB = graph.CreateTask("BufWriterB");
            graph.DeclareBufferUsage(writerB, pBuf,
                D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_UNORDERED_ACCESS);
            graph.PrepareTask(writerB);

            auto& join = graph.CreateTask("BufJoin");
            graph.AddDependency(join, writerA);
            graph.AddDependency(join, writerB);
            graph.DeclareBufferUsage(join, pBuf,
                D3D12_BARRIER_SYNC_PIXEL_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            graph.PrepareTask(join);

            Assert::IsFalse(graph.GetLastError().empty(),
                L"PrepareTask should report DAG conflict for multiple buffer writer deps");
            Assert::IsTrue(graph.GetLastError().find("BufWriterA") != std::string::npos);
            Assert::IsTrue(graph.GetLastError().find("BufWriterB") != std::string::npos);
        }

        //--------------------------------------------------------------------
        // No error when deps are valid — GetLastError() stays empty
        //--------------------------------------------------------------------
        TEST_METHOD(NoErrorOnValidDependencies)
        {
            CComPtr<ID3D12Device> pDevice;
            Assert::IsTrue(SUCCEEDED(CreateGpuTaskTestDevice(&pDevice)));

            CComPtr<ID3D12Resource> pTexA, pTexB;
            Assert::IsTrue(SUCCEEDED(CreateTestTexture(pDevice, &pTexA)));
            Assert::IsTrue(SUCCEEDED(CreateTestTexture(pDevice, &pTexB)));

            CGpuTaskGraph graph;
            graph.SetInitialLayout(pTexA, D3D12_BARRIER_LAYOUT_COMMON);
            graph.SetInitialLayout(pTexB, D3D12_BARRIER_LAYOUT_COMMON);

            // Two deps write DIFFERENT resources — no conflict
            auto& writerA = graph.CreateTask("WriteA");
            graph.DeclareTextureUsage(writerA, pTexA,
                D3D12_BARRIER_LAYOUT_RENDER_TARGET, D3D12_BARRIER_SYNC_RENDER_TARGET, D3D12_BARRIER_ACCESS_RENDER_TARGET);
            graph.PrepareTask(writerA);

            auto& writerB = graph.CreateTask("WriteB");
            graph.DeclareTextureUsage(writerB, pTexB,
                D3D12_BARRIER_LAYOUT_RENDER_TARGET, D3D12_BARRIER_SYNC_RENDER_TARGET, D3D12_BARRIER_ACCESS_RENDER_TARGET);
            graph.PrepareTask(writerB);

            auto& join = graph.CreateTask("ReadBoth");
            graph.AddDependency(join, writerA);
            graph.AddDependency(join, writerB);
            graph.DeclareTextureUsage(join, pTexA,
                D3D12_BARRIER_LAYOUT_SHADER_RESOURCE, D3D12_BARRIER_SYNC_PIXEL_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            graph.DeclareTextureUsage(join, pTexB,
                D3D12_BARRIER_LAYOUT_SHADER_RESOURCE, D3D12_BARRIER_SYNC_PIXEL_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
            graph.PrepareTask(join);

            Assert::IsTrue(graph.GetLastError().empty(),
                L"No error expected when deps write different resources");
        }

        //--------------------------------------------------------------------
        // GetLastError cleared after Reset
        //--------------------------------------------------------------------
        TEST_METHOD(ErrorClearedAfterReset)
        {
            CGpuTaskGraph graph;
            auto& t0 = graph.CreateTask("A");
            auto& t1 = graph.CreateTask("B");
            graph.AddDependency(t0, t1);  // Invalid: wrong order
            Assert::IsFalse(graph.GetLastError().empty());

            graph.Reset();
            Assert::IsTrue(graph.GetLastError().empty(),
                L"Reset should clear error state");
        }
    };
}
