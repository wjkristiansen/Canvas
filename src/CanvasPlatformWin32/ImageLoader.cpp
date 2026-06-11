#include "pch.h"
#include "CanvasCore.h"
#include <vector>

namespace Canvas::Platform::Win32 {

namespace {

//------------------------------------------------------------------------------------------------
// CImage - XImage implementation. Holds the decoded pixels in a std::vector that never
// escapes this DLL; callers only ever see the XImage interface.
//------------------------------------------------------------------------------------------------
class CImage : public XImage
{
public:
    CImage() = default;

    Gem::Result Initialize() { return Gem::Result::Success; }
    void Uninitialize() {}

    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XImage)
    END_GEM_INTERFACE_MAP()

    GEMMETHOD_(uint32_t, GetWidth)() final { return m_Width; }
    GEMMETHOD_(uint32_t, GetHeight)() final { return m_Height; }
    GEMMETHOD_(Canvas::GfxFormat, GetFormat)() final { return m_Format; }
    GEMMETHOD_(uint32_t, GetBytesPerPixel)() final { return m_BytesPerPixel; }

    GEMMETHOD_(const uint8_t*, GetPixels)() final
    {
        return m_Pixels.empty() ? nullptr : m_Pixels.data();
    }

    GEMMETHOD_(size_t, GetPixelByteCount)() final { return m_Pixels.size(); }

    GEMMETHOD_(bool, IsEmpty)() final
    {
        return m_Pixels.empty() || m_Width == 0 || m_Height == 0;
    }

    // Internal - takes ownership of a freshly decoded frame. Called once by the loader.
    void SetPixels(uint32_t width, uint32_t height, Canvas::GfxFormat format,
        uint32_t bytesPerPixel, std::vector<uint8_t>&& pixels)
    {
        m_Width         = width;
        m_Height        = height;
        m_Format        = format;
        m_BytesPerPixel = bytesPerPixel;
        m_Pixels        = std::move(pixels);
    }

private:
    uint32_t              m_Width         = 0;
    uint32_t              m_Height        = 0;
    Canvas::GfxFormat     m_Format        = Canvas::GfxFormat::Unknown;
    uint32_t              m_BytesPerPixel = 0;
    std::vector<uint8_t>  m_Pixels;
};

struct FormatDesc
{
    WICPixelFormatGUID wicGuid;
    uint32_t           bytesPerPixel;
};

bool GetFormatDesc(Canvas::GfxFormat format, FormatDesc& out, Canvas::XLogger* pLogger)
{
    switch (format)
    {
    case Canvas::GfxFormat::R8G8B8A8_UNorm:
        out = { GUID_WICPixelFormat32bppRGBA,    4 }; return true;
    case Canvas::GfxFormat::R16G16B16A16_Float:
        out = { GUID_WICPixelFormat64bppRGBAHalf, 8 }; return true;
    case Canvas::GfxFormat::R8_UNorm:
        out = { GUID_WICPixelFormat8bppGray,     1 }; return true;
    case Canvas::GfxFormat::R16_UNorm:
        out = { GUID_WICPixelFormat16bppGray,    2 }; return true;
    default:
        LogError(pLogger, "LoadImageData: unsupported GfxFormat %d",
            static_cast<int>(format));
        return false;
    }
}

bool DecodeToFormat(
    IWICImagingFactory* pFactory,
    IWICBitmapDecoder*  pDecoder,
    Canvas::GfxFormat   format,
    CImage*             outImage,
    Canvas::XLogger*    pLogger)
{
    FormatDesc fd;
    if (!GetFormatDesc(format, fd, pLogger))
        return false;

    CComPtr<IWICBitmapFrameDecode> pFrame;
    HRESULT hr = pDecoder->GetFrame(0, &pFrame);
    if (FAILED(hr))
    {
        LogError(pLogger, "LoadImageData: GetFrame failed hr=0x%08X",
            static_cast<unsigned>(hr));
        return false;
    }

    UINT width = 0, height = 0;
    hr = pFrame->GetSize(&width, &height);
    if (FAILED(hr) || width == 0 || height == 0)
    {
        LogError(pLogger, "LoadImageData: invalid size %ux%u", width, height);
        return false;
    }

    CComPtr<IWICFormatConverter> pConverter;
    hr = pFactory->CreateFormatConverter(&pConverter);
    if (FAILED(hr))
    {
        LogError(pLogger, "LoadImageData: CreateFormatConverter failed hr=0x%08X",
            static_cast<unsigned>(hr));
        return false;
    }

    hr = pConverter->Initialize(pFrame, fd.wicGuid,
        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr))
    {
        LogError(pLogger, "LoadImageData: format converter init failed hr=0x%08X",
            static_cast<unsigned>(hr));
        return false;
    }

    const UINT rowPitch = width * fd.bytesPerPixel;
    std::vector<uint8_t> pixels(static_cast<size_t>(rowPitch) * height);
    hr = pConverter->CopyPixels(nullptr, rowPitch,
        static_cast<UINT>(pixels.size()), pixels.data());
    if (FAILED(hr))
    {
        LogError(pLogger, "LoadImageData: CopyPixels failed hr=0x%08X",
            static_cast<unsigned>(hr));
        return false;
    }

    outImage->SetPixels(width, height, format, fd.bytesPerPixel, std::move(pixels));
    return true;
}

bool MakeFactory(IWICImagingFactory** ppFactory, Canvas::XLogger* pLogger)
{
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(ppFactory));
    if (FAILED(hr))
        LogError(pLogger, "LoadImageData: WIC factory creation failed hr=0x%08X",
            static_cast<unsigned>(hr));
    return SUCCEEDED(hr);
}

bool DecodeIntoImage(
    IWICImagingFactory* pFactory, IWICBitmapDecoder* pDecoder, Canvas::GfxFormat format,
    XImage** ppImage, Canvas::XLogger* pLogger)
{
    Gem::TGemPtr<CImage> pImage;
    Gem::Result gr = Gem::TGenericImpl<CImage>::Create(&pImage);
    if (Gem::Failed(gr))
    {
        LogError(pLogger, "LoadImageData: failed to allocate image object");
        return false;
    }

    if (!DecodeToFormat(pFactory, pDecoder, format, pImage.Get(), pLogger))
        return false;

    return Gem::Succeeded(pImage->QueryInterface(ppImage));
}

} // anonymous namespace

Gem::Result LoadImageData(
    const wchar_t* path, Canvas::GfxFormat format, XImage** ppImage, Canvas::XLogger* pLogger)
{
    if (!ppImage)
        return Gem::Result::BadPointer;
    *ppImage = nullptr;

    if (!path || !*path)
    {
        LogError(pLogger, "LoadImageData: null/empty path");
        return Gem::Result::InvalidArg;
    }

    CComPtr<IWICImagingFactory> pFactory;
    if (!MakeFactory(&pFactory, pLogger))
        return Gem::Result::Fail;

    CComPtr<IWICBitmapDecoder> pDecoder;
    HRESULT hr = pFactory->CreateDecoderFromFilename(
        path, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &pDecoder);
    if (FAILED(hr))
    {
        LogError(pLogger, "LoadImageData: failed to open '%ls' hr=0x%08X",
            path, static_cast<unsigned>(hr));
        return Gem::Result::Fail;
    }

    if (!DecodeIntoImage(pFactory, pDecoder, format, ppImage, pLogger))
        return Gem::Result::Fail;

    LogInfo(pLogger, "LoadImageData: loaded %ux%u from '%ls'",
        (*ppImage)->GetWidth(), (*ppImage)->GetHeight(), path);
    return Gem::Result::Success;
}

Gem::Result LoadImageData(
    const uint8_t* data, size_t byteCount, Canvas::GfxFormat format,
    XImage** ppImage, Canvas::XLogger* pLogger)
{
    if (!ppImage)
        return Gem::Result::BadPointer;
    *ppImage = nullptr;

    if (!data || byteCount == 0)
    {
        LogError(pLogger, "LoadImageData: null/empty data buffer");
        return Gem::Result::InvalidArg;
    }

    CComPtr<IWICImagingFactory> pFactory;
    if (!MakeFactory(&pFactory, pLogger))
        return Gem::Result::Fail;

    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, byteCount);
    if (!hMem)
    {
        LogError(pLogger, "LoadImageData: GlobalAlloc failed for %zu bytes", byteCount);
        return Gem::Result::OutOfMemory;
    }
    void* pDst = GlobalLock(hMem);
    if (!pDst)
    {
        GlobalFree(hMem);
        LogError(pLogger, "LoadImageData: GlobalLock failed");
        return Gem::Result::Fail;
    }
    memcpy(pDst, data, byteCount);
    GlobalUnlock(hMem);

    CComPtr<IStream> pStream;
    HRESULT hr = CreateStreamOnHGlobal(hMem, /*fDeleteOnRelease*/ TRUE, &pStream);
    if (FAILED(hr))
    {
        GlobalFree(hMem);
        LogError(pLogger, "LoadImageData: CreateStreamOnHGlobal failed hr=0x%08X",
            static_cast<unsigned>(hr));
        return Gem::Result::Fail;
    }

    CComPtr<IWICBitmapDecoder> pDecoder;
    hr = pFactory->CreateDecoderFromStream(
        pStream, nullptr, WICDecodeMetadataCacheOnDemand, &pDecoder);
    if (FAILED(hr))
    {
        LogError(pLogger, "LoadImageData: decoder creation from stream failed hr=0x%08X",
            static_cast<unsigned>(hr));
        return Gem::Result::Fail;
    }

    if (!DecodeIntoImage(pFactory, pDecoder, format, ppImage, pLogger))
        return Gem::Result::Fail;

    LogInfo(pLogger, "LoadImageData: loaded %ux%u from memory (%zu bytes)",
        (*ppImage)->GetWidth(), (*ppImage)->GetHeight(), byteCount);
    return Gem::Result::Success;
}

} // namespace Canvas::Platform::Win32
