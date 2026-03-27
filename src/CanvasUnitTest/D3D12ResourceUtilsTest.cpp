#include "pch.h"
#include "CppUnitTest.h"
#include "D3D12ResourceUtils.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

using namespace Canvas;

namespace CanvasUnitTest
{
    TEST_CLASS(D3D12ResourceUtilsTest)
    {
    public:

        TEST_METHOD(SubresourceLayout_UniformInit)
        {
            SubresourceLayout layout(D3D12_BARRIER_LAYOUT_COMMON, 4);
            Assert::IsTrue(layout.m_AllSame);
            Assert::IsTrue(layout.GetLayout(0) == D3D12_BARRIER_LAYOUT_COMMON);
            Assert::IsTrue(layout.GetLayout(3) == D3D12_BARRIER_LAYOUT_COMMON);
        }

        TEST_METHOD(SubresourceLayout_ExpandToPerSubresource)
        {
            SubresourceLayout layout(D3D12_BARRIER_LAYOUT_RENDER_TARGET, 4);
            layout.ExpandToPerSubresource(4);

            Assert::IsFalse(layout.m_AllSame);
            Assert::AreEqual(size_t(4), layout.m_PerSubresource.size());
            for (UINT i = 0; i < 4; ++i)
                Assert::IsTrue(layout.m_PerSubresource[i] == D3D12_BARRIER_LAYOUT_RENDER_TARGET);
        }

        TEST_METHOD(SubresourceLayout_PerSubresourceModify)
        {
            SubresourceLayout layout(D3D12_BARRIER_LAYOUT_SHADER_RESOURCE, 4);
            layout.ExpandToPerSubresource(4);

            layout.m_PerSubresource[1] = D3D12_BARRIER_LAYOUT_COPY_SOURCE;
            layout.m_PerSubresource[3] = D3D12_BARRIER_LAYOUT_COPY_SOURCE;

            Assert::IsTrue(layout.GetLayout(0) == D3D12_BARRIER_LAYOUT_SHADER_RESOURCE);
            Assert::IsTrue(layout.GetLayout(1) == D3D12_BARRIER_LAYOUT_COPY_SOURCE);
            Assert::IsTrue(layout.GetLayout(2) == D3D12_BARRIER_LAYOUT_SHADER_RESOURCE);
            Assert::IsTrue(layout.GetLayout(3) == D3D12_BARRIER_LAYOUT_COPY_SOURCE);
        }

        TEST_METHOD(SubresourceLayout_AssignUniform)
        {
            // Assigning a uniform layout after per-subresource expansion should work
            SubresourceLayout layout(D3D12_BARRIER_LAYOUT_COMMON, 4);
            layout.ExpandToPerSubresource(4);
            Assert::IsFalse(layout.m_AllSame);

            // Overwrite with a uniform layout
            layout = SubresourceLayout(D3D12_BARRIER_LAYOUT_RENDER_TARGET, 4);
            Assert::IsTrue(layout.m_AllSame);
            Assert::IsTrue(layout.GetLayout(0) == D3D12_BARRIER_LAYOUT_RENDER_TARGET);
        }

        TEST_METHOD(SubresourceLayout_SetLayout_AllSubresources)
        {
            SubresourceLayout layout(D3D12_BARRIER_LAYOUT_COMMON, 4);
            layout.SetLayout(0xFFFFFFFF, D3D12_BARRIER_LAYOUT_RENDER_TARGET);
            Assert::IsTrue(layout.m_AllSame);
            Assert::IsTrue(layout.GetLayout(0) == D3D12_BARRIER_LAYOUT_RENDER_TARGET);
        }

        TEST_METHOD(SubresourceLayout_SetLayout_Individual_SplitsUniform)
        {
            SubresourceLayout layout(D3D12_BARRIER_LAYOUT_COMMON, 4);
            Assert::IsTrue(layout.m_AllSame);

            // Setting subresource 1 should expand to per-subresource
            layout.SetLayout(1, D3D12_BARRIER_LAYOUT_COPY_SOURCE, 4);
            Assert::IsFalse(layout.m_AllSame);
            Assert::IsTrue(layout.GetLayout(0) == D3D12_BARRIER_LAYOUT_COMMON);
            Assert::IsTrue(layout.GetLayout(1) == D3D12_BARRIER_LAYOUT_COPY_SOURCE);
            Assert::IsTrue(layout.GetLayout(2) == D3D12_BARRIER_LAYOUT_COMMON);
            Assert::IsTrue(layout.GetLayout(3) == D3D12_BARRIER_LAYOUT_COMMON);
        }

        TEST_METHOD(SubresourceLayout_SetLayout_AllAfterPerSubresource_Collapses)
        {
            SubresourceLayout layout(D3D12_BARRIER_LAYOUT_COMMON, 4);
            layout.SetLayout(1, D3D12_BARRIER_LAYOUT_COPY_SOURCE, 4);
            Assert::IsFalse(layout.m_AllSame);

            // Setting all subresources should collapse back to uniform
            layout.SetLayout(0xFFFFFFFF, D3D12_BARRIER_LAYOUT_SHADER_RESOURCE);
            Assert::IsTrue(layout.m_AllSame);
            Assert::IsTrue(layout.GetLayout(0) == D3D12_BARRIER_LAYOUT_SHADER_RESOURCE);
            Assert::IsTrue(layout.m_PerSubresource.empty());
        }

        TEST_METHOD(SubresourceLayout_TryCollapse_AllMatch)
        {
            SubresourceLayout layout(D3D12_BARRIER_LAYOUT_COMMON, 4);
            layout.ExpandToPerSubresource(4);
            Assert::IsFalse(layout.m_AllSame);

            // All subresources are COMMON — should collapse
            layout.TryCollapse();
            Assert::IsTrue(layout.m_AllSame);
            Assert::IsTrue(layout.m_UniformLayout == D3D12_BARRIER_LAYOUT_COMMON);
            Assert::IsTrue(layout.m_PerSubresource.empty());
        }

        TEST_METHOD(SubresourceLayout_TryCollapse_Mismatch)
        {
            SubresourceLayout layout(D3D12_BARRIER_LAYOUT_COMMON, 4);
            layout.SetLayout(2, D3D12_BARRIER_LAYOUT_COPY_SOURCE, 4);
            Assert::IsFalse(layout.m_AllSame);

            // Subresources differ — should NOT collapse
            layout.TryCollapse();
            Assert::IsFalse(layout.m_AllSame);
        }

        TEST_METHOD(SubresourceLayout_TryCollapse_AfterUnifying)
        {
            SubresourceLayout layout(D3D12_BARRIER_LAYOUT_COMMON, 4);
            layout.SetLayout(0, D3D12_BARRIER_LAYOUT_RENDER_TARGET, 4);
            layout.SetLayout(1, D3D12_BARRIER_LAYOUT_RENDER_TARGET, 4);
            layout.SetLayout(2, D3D12_BARRIER_LAYOUT_RENDER_TARGET, 4);
            layout.SetLayout(3, D3D12_BARRIER_LAYOUT_RENDER_TARGET, 4);

            layout.TryCollapse();
            Assert::IsTrue(layout.m_AllSame);
            Assert::IsTrue(layout.m_UniformLayout == D3D12_BARRIER_LAYOUT_RENDER_TARGET);
        }

        TEST_METHOD(CTextureResource_InitLayout)
        {
            // Create a real D3D12 device and texture to test CTextureResource
            CComPtr<ID3D12Device> pDevice;
            HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&pDevice));
            if (FAILED(hr))
            {
                Assert::Fail(L"No D3D12 device available — skipping hardware test");
                return;
            }

            CComPtr<ID3D12Device10> pDevice10;
            hr = pDevice->QueryInterface(IID_PPV_ARGS(&pDevice10));
            Assert::IsTrue(SUCCEEDED(hr));

            CD3DX12_RESOURCE_DESC1 desc = CD3DX12_RESOURCE_DESC1::Tex2D(
                DXGI_FORMAT_R8G8B8A8_UNORM, 256, 256, 2, 2, 1, 0,
                D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
            CD3DX12_HEAP_PROPERTIES heapProp(D3D12_HEAP_TYPE_DEFAULT);

            CComPtr<ID3D12Resource> pD3DResource;
            hr = pDevice10->CreateCommittedResource3(
                &heapProp, D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES,
                &desc, D3D12_BARRIER_LAYOUT_COMMON, nullptr, nullptr, 0, nullptr,
                IID_PPV_ARGS(&pD3DResource));
            Assert::IsTrue(SUCCEEDED(hr));

            CTextureResource tex(pD3DResource, D3D12_BARRIER_LAYOUT_COMMON);
            Assert::IsTrue(tex.GetCurSubresourceLayout(0) == D3D12_BARRIER_LAYOUT_COMMON);
            Assert::AreEqual(UINT(4), tex.m_NumSubresources); // 2 array * 2 mip

            // Simulate ComputeFinalLayouts updating the committed state
            tex.m_CurrentLayout = SubresourceLayout(D3D12_BARRIER_LAYOUT_RENDER_TARGET, tex.m_NumSubresources);
            Assert::IsTrue(tex.GetCurSubresourceLayout(0) == D3D12_BARRIER_LAYOUT_RENDER_TARGET);
            Assert::IsTrue(tex.m_CurrentLayout.m_AllSame);
        }
    };
}
