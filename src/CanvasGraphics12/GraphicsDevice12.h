//================================================================================================
// GraphicsDevice12
//================================================================================================

#pragma once

namespace Canvas
{
    namespace Graphics
    {
        //------------------------------------------------------------------------------------------------
        class CDevice12 :
            public XGraphicsDevice,
            public Gem::CGenericBase
        {
            QLog::CBasicLogger m_Logger;
            CComPtr<ID3D12Resource> m_pVertices;
            CComPtr<ID3D12Resource> m_pNormals;
            CComPtr<ID3D12Resource> m_pTextureUVs[4];
            CComPtr<ID3D12Resource> m_pBoneWeights;
        public:
            CComPtr<ID3D12Device5> m_pD3DDevice;
            CComPtr<ID3D12CommandQueue> m_pDirectCommandQueue;
            CComPtr<IDXGIFactory7> m_pDXGIFactory;
            CComPtr<IDXGISwapChain4> m_pSwapChain;
            CComPtr<ID3D12RootSignature> m_pDefaultRootSig;


            CDevice12(QLog::CLogClient *pLogClient);

            Result Initialize(HWND hWnd, bool Windowed);

            GEMMETHOD(Present)() final;
            // GEMMETHOD(AllocateUploadBuffer)(UINT64 SizeInBytes, XGraphicsUploadBuffer **ppUploadBuffer) final;
        };

        //------------------------------------------------------------------------------------------------
        class CUploadBuffer12 : public XGraphicsUploadBuffer
        {
            CComPtr<ID3D12Resource> m_pResource;
            UINT64 m_OffsetToStart = 0;
            void *m_pData = 0;

        public:
            CUploadBuffer12(ID3D12Resource *pResource, UINT64 OffsetToStart, UINT64 Size);
            GEMMETHOD_(void *, Data)() final;
        };
    }
}
