#include "pch.h"
#include "CanvasCore.h"

namespace Canvas::Platform::Win32 {

namespace {

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
    ImageData*          outImage,
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

    outImage->Width  = width;
    outImage->Height = height;
    outImage->Format = format;
    outImage->Pixels = std::move(pixels);
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

} // anonymous namespace

bool LoadImageData(
    const wchar_t* path, Canvas::GfxFormat format, ImageData* outImage, Canvas::XLogger* pLogger)
{
    if (!outImage)
        return false;
    *outImage = ImageData{};

    if (!path || !*path)
    {
        LogError(pLogger, "LoadImageData: null/empty path");
        return false;
    }

    CComPtr<IWICImagingFactory> pFactory;
    if (!MakeFactory(&pFactory, pLogger))
        return false;

    CComPtr<IWICBitmapDecoder> pDecoder;
    HRESULT hr = pFactory->CreateDecoderFromFilename(
        path, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &pDecoder);
    if (FAILED(hr))
    {
        LogError(pLogger, "LoadImageData: failed to open '%ls' hr=0x%08X",
            path, static_cast<unsigned>(hr));
        return false;
    }

    if (!DecodeToFormat(pFactory, pDecoder, format, outImage, pLogger))
        return false;

    LogInfo(pLogger, "LoadImageData: loaded %ux%u from '%ls'",
        outImage->Width, outImage->Height, path);
    return true;
}

bool LoadImageData(
    const uint8_t* data, size_t byteCount, Canvas::GfxFormat format,
    ImageData* outImage, Canvas::XLogger* pLogger)
{
    if (!outImage)
        return false;
    *outImage = ImageData{};

    if (!data || byteCount == 0)
    {
        LogError(pLogger, "LoadImageData: null/empty data buffer");
        return false;
    }

    CComPtr<IWICImagingFactory> pFactory;
    if (!MakeFactory(&pFactory, pLogger))
        return false;

    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, byteCount);
    if (!hMem)
    {
        LogError(pLogger, "LoadImageData: GlobalAlloc failed for %zu bytes", byteCount);
        return false;
    }
    void* pDst = GlobalLock(hMem);
    if (!pDst)
    {
        GlobalFree(hMem);
        LogError(pLogger, "LoadImageData: GlobalLock failed");
        return false;
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
        return false;
    }

    CComPtr<IWICBitmapDecoder> pDecoder;
    hr = pFactory->CreateDecoderFromStream(
        pStream, nullptr, WICDecodeMetadataCacheOnDemand, &pDecoder);
    if (FAILED(hr))
    {
        LogError(pLogger, "LoadImageData: decoder creation from stream failed hr=0x%08X",
            static_cast<unsigned>(hr));
        return false;
    }

    if (!DecodeToFormat(pFactory, pDecoder, format, outImage, pLogger))
        return false;

    LogInfo(pLogger, "LoadImageData: loaded %ux%u from memory (%zu bytes)",
        outImage->Width, outImage->Height, byteCount);
    return true;
}

} // namespace Canvas::Platform::Win32
