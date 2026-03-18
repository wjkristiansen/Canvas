#include "pch.h"
#include "CppUnitTest.h"
#include "D3D12ResourceUtils.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

using namespace Canvas;

namespace CanvasUnitTest
{
    HRESULT CreateTestDevice(ID3D12Device **ppDevice)
    {
        HRESULT hr = S_OK;
#if defined(DEBUG)
        CComPtr<ID3D12Debug3> pDebug;
        hr = D3D12GetDebugInterface(IID_PPV_ARGS(&pDebug));
        pDebug->EnableDebugLayer();
        Assert::IsTrue(SUCCEEDED(hr));
#endif
        CComPtr<ID3D12Device> pDevice;
        hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&pDevice));
        if (SUCCEEDED(hr))
        {
            *ppDevice = pDevice.Detach();
        }
        return hr;
    }

    TEST_CLASS(D3D12ResourceUtilsTest)
    {
    public:

        TEST_METHOD(SimpleLayoutTransitions)
        {
            CResourceStateManager StateManager;
            std::vector<D3D12_TEXTURE_BARRIER> barriers;

            CD3DX12_RESOURCE_DESC1 ResourceDescs[3] = {};
            D3D12_BARRIER_LAYOUT InitLayouts[3] =
            {
                D3D12_BARRIER_LAYOUT_COMMON,
                D3D12_BARRIER_LAYOUT_RENDER_TARGET,
                D3D12_BARRIER_LAYOUT_COPY_DEST,
            };

            // Single-subresource Texture
            ResourceDescs[0] = CD3DX12_RESOURCE_DESC1::Tex2D(
                DXGI_FORMAT_R8G8B8A8_UNORM,
                256, 256,
                1, 1, 1, 0,
                D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

            // Multi-subresource Texture (2 array slices * 2 mip levels = 4 subresources)
            ResourceDescs[1] = CD3DX12_RESOURCE_DESC1::Tex2D(
                DXGI_FORMAT_R8G8B8A8_UNORM,
                256, 256,
                2, 2,
                1, 0,
                D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

            // Another single Texture
            ResourceDescs[2] = CD3DX12_RESOURCE_DESC1::Tex2D(
                DXGI_FORMAT_R8G8B8A8_UNORM,
                256, 256,
                1, 1, 1, 0,
                D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

            CComPtr<ID3D12Device> pDevice;
            Assert::IsTrue(SUCCEEDED(CreateTestDevice(&pDevice)));

            CComPtr<ID3D12Device10> pDevice10;
            Assert::IsTrue(SUCCEEDED(pDevice->QueryInterface(IID_PPV_ARGS(&pDevice10))));

            CD3DX12_HEAP_PROPERTIES heapProp(D3D12_HEAP_TYPE_DEFAULT);

            // Create texture resources
            std::unique_ptr<CTextureResource> pResources[3];
            for (size_t i = 0; i < 3; ++i)
            {
                CComPtr<ID3D12Resource> pD3DResource;
                HRESULT hr = pDevice10->CreateCommittedResource3(
                    &heapProp,
                    D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES,
                    &ResourceDescs[i],
                    InitLayouts[i],
                    nullptr,
                    nullptr, 0, nullptr,
                    IID_PPV_ARGS(&pD3DResource));
                Assert::IsTrue(SUCCEEDED(hr));
                pResources[i] = std::make_unique<CTextureResource>(pD3DResource, InitLayouts[i]);
            }

            // Basic single-texture uniform transition
            pResources[0]->SetDesiredResourceLayout(StateManager, D3D12_BARRIER_LAYOUT_SHADER_RESOURCE);
            StateManager.ResolveResourceBarriers(barriers);
            Assert::AreEqual(size_t(1), barriers.size());
            Assert::IsTrue(barriers[0].pResource == pResources[0]->GetD3DResource());
            Assert::IsTrue(barriers[0].LayoutBefore == InitLayouts[0]);
            Assert::IsTrue(barriers[0].LayoutAfter == D3D12_BARRIER_LAYOUT_SHADER_RESOURCE);
            Assert::IsTrue(barriers[0].Subresources.IndexOrFirstMipLevel == 0xFFFFFFFF); // ALL_SUBRESOURCES
            Assert::IsTrue(StateManager.m_TransitionList.IsEmpty());
            barriers.clear();

            // Two texture transitions
            pResources[0]->SetDesiredResourceLayout(StateManager, D3D12_BARRIER_LAYOUT_COPY_SOURCE);
            pResources[1]->SetDesiredResourceLayout(StateManager, D3D12_BARRIER_LAYOUT_SHADER_RESOURCE);
            StateManager.ResolveResourceBarriers(barriers);
            Assert::AreEqual(size_t(2), barriers.size());
            Assert::IsTrue(barriers[0].pResource == pResources[0]->GetD3DResource());
            Assert::IsTrue(barriers[0].LayoutBefore == D3D12_BARRIER_LAYOUT_SHADER_RESOURCE);
            Assert::IsTrue(barriers[0].LayoutAfter == D3D12_BARRIER_LAYOUT_COPY_SOURCE);
            Assert::IsTrue(barriers[1].pResource == pResources[1]->GetD3DResource());
            Assert::IsTrue(barriers[1].LayoutBefore == InitLayouts[1]);
            Assert::IsTrue(barriers[1].LayoutAfter == D3D12_BARRIER_LAYOUT_SHADER_RESOURCE);
            Assert::IsTrue(StateManager.m_TransitionList.IsEmpty());
            barriers.clear();

            // Individual subresource transitions on multi-subresource texture
            pResources[1]->SetDesiredSubresourceLayout(StateManager, 1, D3D12_BARRIER_LAYOUT_COPY_SOURCE);
            pResources[1]->SetDesiredSubresourceLayout(StateManager, 3, D3D12_BARRIER_LAYOUT_COPY_SOURCE);
            StateManager.ResolveResourceBarriers(barriers);
            Assert::AreEqual(size_t(2), barriers.size());
            Assert::IsTrue(barriers[0].pResource == pResources[1]->GetD3DResource());
            Assert::IsTrue(barriers[0].LayoutBefore == D3D12_BARRIER_LAYOUT_SHADER_RESOURCE);
            Assert::IsTrue(barriers[0].LayoutAfter == D3D12_BARRIER_LAYOUT_COPY_SOURCE);
            Assert::IsTrue(barriers[0].Subresources.IndexOrFirstMipLevel == 1);
            Assert::IsTrue(barriers[1].pResource == pResources[1]->GetD3DResource());
            Assert::IsTrue(barriers[1].LayoutBefore == D3D12_BARRIER_LAYOUT_SHADER_RESOURCE);
            Assert::IsTrue(barriers[1].LayoutAfter == D3D12_BARRIER_LAYOUT_COPY_SOURCE);
            Assert::IsTrue(barriers[1].Subresources.IndexOrFirstMipLevel == 3);
            Assert::IsTrue(StateManager.m_TransitionList.IsEmpty());
            Assert::IsFalse(pResources[1]->m_CurrentLayout.m_AllSame);
            Assert::IsTrue(pResources[1]->m_CurrentLayout.m_PerSubresource[1] == D3D12_BARRIER_LAYOUT_COPY_SOURCE);
            Assert::IsTrue(pResources[1]->m_CurrentLayout.m_PerSubresource[3] == D3D12_BARRIER_LAYOUT_COPY_SOURCE);
            barriers.clear();

            // Finish transitioning all subresources to COPY_SOURCE → should collapse back to uniform
            pResources[1]->SetDesiredSubresourceLayout(StateManager, 0, D3D12_BARRIER_LAYOUT_COPY_SOURCE);
            pResources[1]->SetDesiredSubresourceLayout(StateManager, 2, D3D12_BARRIER_LAYOUT_COPY_SOURCE);
            StateManager.ResolveResourceBarriers(barriers);
            Assert::AreEqual(size_t(2), barriers.size());
            barriers.clear();
            // All subresources now share the same layout → should be uniform again
            Assert::IsTrue(pResources[1]->m_CurrentLayout.m_AllSame);

            // Validate ALL_SUBRESOURCES transition after uniform collapse
            pResources[1]->SetDesiredResourceLayout(StateManager, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
            StateManager.ResolveResourceBarriers(barriers);
            Assert::AreEqual(size_t(1), barriers.size());
            Assert::IsTrue(barriers[0].pResource == pResources[1]->GetD3DResource());
            Assert::IsTrue(barriers[0].LayoutBefore == D3D12_BARRIER_LAYOUT_COPY_SOURCE);
            Assert::IsTrue(barriers[0].LayoutAfter == D3D12_BARRIER_LAYOUT_RENDER_TARGET);
            Assert::IsTrue(barriers[0].Subresources.IndexOrFirstMipLevel == 0xFFFFFFFF); // ALL_SUBRESOURCES
            Assert::IsTrue(pResources[1]->m_CurrentLayout.m_AllSame);
            barriers.clear();

            // Validate no barrier emitted when desired == current
            pResources[2]->SetDesiredResourceLayout(StateManager, InitLayouts[2]);
            StateManager.ResolveResourceBarriers(barriers);
            Assert::AreEqual(size_t(0), barriers.size());
            barriers.clear();
        }
    };
}
