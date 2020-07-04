#include "stdafx.h"
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

        TEST_METHOD(SimpleStateTransitions)
        {
            CResourceStateManager StateManager;
            std::vector<D3D12_RESOURCE_BARRIER> transitions;

            std::unique_ptr<CResource> pResources[4];
            CD3DX12_RESOURCE_DESC ResourceDescs[4] = {};
            D3D12_RESOURCE_STATES InitStates[4] =
            {
                D3D12_RESOURCE_STATE_COMMON,
                D3D12_RESOURCE_STATE_RENDER_TARGET,
                D3D12_RESOURCE_STATE_COPY_DEST,
                D3D12_RESOURCE_STATE_DEPTH_WRITE
            };

            // Buffer
            ResourceDescs[0] = CD3DX12_RESOURCE_DESC::Buffer(4096);

            // Render Target Texture
            ResourceDescs[1] = CD3DX12_RESOURCE_DESC::Tex2D(
                DXGI_FORMAT_R8G8B8A8_UNORM,
                256,
                256,
                1, 0, 1, 0,
                D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

            ResourceDescs[2] = CD3DX12_RESOURCE_DESC::Tex2D(
                DXGI_FORMAT_R8G8B8A8_UNORM,
                256,
                256,
                2,
                2,
                1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

            CComPtr<ID3D12Device> pDevice;
            Assert::IsTrue(SUCCEEDED(CreateTestDevice(&pDevice)));

            CD3DX12_HEAP_PROPERTIES heapProp(D3D12_HEAP_TYPE_DEFAULT);

            // Make some resources
            for (size_t i = 0; i < 3; ++i)
            {
                CComPtr<ID3D12Resource> pD3DResource;
                HRESULT hr = pDevice->CreateCommittedResource(
                    &heapProp,
                    D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES,
                    &ResourceDescs[i],
                    InitStates[i],
                    nullptr, 
                    IID_PPV_ARGS(&pD3DResource));
                Assert::IsTrue(SUCCEEDED(hr));
                pResources[i] = std::make_unique<CResource>(pD3DResource, InitStates[i]);
            }

            // Basic single-buffer transition
            pResources[0]->SetDesiredResourceState(StateManager, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            StateManager.ResolveResourceBarriers(transitions);
            Assert::AreEqual(size_t(1), transitions.size());
            Assert::IsTrue(transitions[0].Transition.pResource == pResources[0]->GetD3DResource());
            Assert::IsTrue(transitions[0].Transition.StateBefore == InitStates[0]);
            Assert::IsTrue(transitions[0].Transition.StateAfter == D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            Assert::IsTrue(transitions[0].Transition.Subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
            Assert::IsTrue(StateManager.m_TransitionList.IsEmpty());
            transitions.clear();

            // Two texture transition
            pResources[1]->SetDesiredResourceState(StateManager, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            pResources[2]->SetDesiredResourceState(StateManager, D3D12_RESOURCE_STATE_COPY_SOURCE);
            StateManager.ResolveResourceBarriers(transitions);
            Assert::AreEqual(size_t(2), transitions.size());
            Assert::IsTrue(transitions[0].Transition.pResource == pResources[1]->GetD3DResource());
            Assert::IsTrue(transitions[0].Transition.StateBefore == InitStates[1]);
            Assert::IsTrue(transitions[0].Transition.StateAfter == D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            Assert::IsTrue(transitions[0].Transition.Subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
            Assert::IsTrue(transitions[1].Transition.pResource == pResources[2]->GetD3DResource());
            Assert::IsTrue(transitions[1].Transition.StateBefore == InitStates[2]);
            Assert::IsTrue(transitions[1].Transition.StateAfter == D3D12_RESOURCE_STATE_COPY_SOURCE);
            Assert::IsTrue(transitions[1].Transition.Subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
            Assert::IsTrue(StateManager.m_TransitionList.IsEmpty());
            transitions.clear();

            // Individual subresource transitions
            pResources[2]->SetDesiredSubresourceState(StateManager, 1, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            pResources[2]->SetDesiredSubresourceState(StateManager, 3, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            StateManager.ResolveResourceBarriers(transitions);
            Assert::AreEqual(size_t(2), transitions.size());
            Assert::IsTrue(transitions[0].Transition.pResource == pResources[2]->GetD3DResource());
            Assert::IsTrue(transitions[0].Transition.StateBefore == D3D12_RESOURCE_STATE_COPY_SOURCE);
            Assert::IsTrue(transitions[0].Transition.StateAfter == D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            Assert::IsTrue(transitions[0].Transition.Subresource == 1);
            Assert::IsTrue(transitions[1].Transition.pResource == pResources[2]->GetD3DResource());
            Assert::IsTrue(transitions[1].Transition.StateBefore == D3D12_RESOURCE_STATE_COPY_SOURCE);
            Assert::IsTrue(transitions[1].Transition.StateAfter == D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            Assert::IsTrue(transitions[1].Transition.Subresource == 3);
            Assert::IsTrue(StateManager.m_TransitionList.IsEmpty());
            Assert::IsFalse(pResources[2]->m_ResourceState.AllSubresourcesSame);
            Assert::IsTrue(pResources[2]->m_ResourceState.SubresourceStates[1] == D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            Assert::IsTrue(pResources[2]->m_ResourceState.SubresourceStates[3] == D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            transitions.clear();

            // Finish transitioning all of pResources[2] subresources to D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
            pResources[2]->SetDesiredSubresourceState(StateManager, 0, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            pResources[2]->SetDesiredSubresourceState(StateManager, 2, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            StateManager.ResolveResourceBarriers(transitions);
            Assert::AreEqual(size_t(2), transitions.size());
            transitions.clear();
            Assert::IsTrue(pResources[2]->m_ResourceState.SubresourceStates[0] == D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            Assert::IsTrue(pResources[2]->m_ResourceState.SubresourceStates[2] == D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            // Verify the resulting state is uniform
            Assert::IsTrue(pResources[2]->m_ResourceState.AllSubresourcesSame);

            // Validate ALL_SUBRESOURCES transtion after last resolve
            pResources[2]->SetDesiredResourceState(StateManager, D3D12_RESOURCE_STATE_RENDER_TARGET);
            StateManager.ResolveResourceBarriers(transitions);
            Assert::AreEqual(size_t(1), transitions.size());
            Assert::IsTrue(transitions[0].Transition.pResource == pResources[2]->GetD3DResource());
            Assert::IsTrue(transitions[0].Transition.StateBefore == D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            Assert::IsTrue(transitions[0].Transition.StateAfter == D3D12_RESOURCE_STATE_RENDER_TARGET);
            Assert::IsTrue(transitions[0].Transition.Subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
            Assert::IsTrue(pResources[2]->m_ResourceState.SubresourceStates[0] == D3D12_RESOURCE_STATE_RENDER_TARGET);
            Assert::IsTrue(pResources[2]->m_ResourceState.AllSubresourcesSame);
            transitions.clear();

            // Validate that compatible state bits can be combined
            pResources[2]->SetDesiredResourceState(StateManager, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            pResources[2]->SetDesiredResourceState(StateManager, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            pResources[2]->SetDesiredResourceState(StateManager, D3D12_RESOURCE_STATE_COPY_SOURCE);
            StateManager.ResolveResourceBarriers(transitions);
            Assert::AreEqual(size_t(1), transitions.size());
            Assert::IsTrue(transitions[0].Transition.pResource == pResources[2]->GetD3DResource());
            Assert::IsTrue(transitions[0].Transition.StateBefore == D3D12_RESOURCE_STATE_RENDER_TARGET);
            Assert::IsTrue(transitions[0].Transition.StateAfter == 
                (D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | 
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | 
                D3D12_RESOURCE_STATE_COPY_SOURCE));
            Assert::IsTrue(transitions[0].Transition.Subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
            Assert::IsTrue(pResources[2]->m_ResourceState.SubresourceStates[0] ==
                (D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                D3D12_RESOURCE_STATE_COPY_SOURCE));
        }
    };
}
