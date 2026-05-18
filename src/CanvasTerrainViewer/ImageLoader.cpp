#include "pch.h"
#include "ImageLoader.h"

namespace Canvas
{
namespace TerrainViewer
{

bool LoadImageRGBA8(const wchar_t* path, ImageRGBA8* outImage, XLogger* pLogger)
{
    if (!outImage)
        return false;
    *outImage = ImageRGBA8{};

    if (!path || !*path)
    {
        LogError(pLogger, "LoadImageRGBA8: null/empty path");
        return false;
    }

    CComPtr<IWICImagingFactory> pFactory;
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&pFactory));
    if (FAILED(hr))
    {
        LogError(pLogger, "LoadImageRGBA8: WIC factory creation failed hr=0x%08X",
            static_cast<unsigned>(hr));
        return false;
    }

    CComPtr<IWICBitmapDecoder> pDecoder;
    hr = pFactory->CreateDecoderFromFilename(
        path, nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnDemand, &pDecoder);
    if (FAILED(hr))
    {
        LogError(pLogger, "LoadImageRGBA8: failed to open '%ls' hr=0x%08X",
            path, static_cast<unsigned>(hr));
        return false;
    }

    CComPtr<IWICBitmapFrameDecode> pFrame;
    hr = pDecoder->GetFrame(0, &pFrame);
    if (FAILED(hr))
    {
        LogError(pLogger, "LoadImageRGBA8: GetFrame failed hr=0x%08X for '%ls'",
            static_cast<unsigned>(hr), path);
        return false;
    }

    UINT width = 0, height = 0;
    hr = pFrame->GetSize(&width, &height);
    if (FAILED(hr) || width == 0 || height == 0)
    {
        LogError(pLogger, "LoadImageRGBA8: invalid size %ux%u for '%ls'", width, height, path);
        return false;
    }

    CComPtr<IWICFormatConverter> pConverter;
    hr = pFactory->CreateFormatConverter(&pConverter);
    if (FAILED(hr))
    {
        LogError(pLogger, "LoadImageRGBA8: CreateFormatConverter failed hr=0x%08X",
            static_cast<unsigned>(hr));
        return false;
    }
    hr = pConverter->Initialize(pFrame, GUID_WICPixelFormat32bppRGBA,
        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr))
    {
        LogError(pLogger, "LoadImageRGBA8: format converter init failed hr=0x%08X",
            static_cast<unsigned>(hr));
        return false;
    }

    const UINT rowPitch = width * 4;
    std::vector<uint8_t> pixels(static_cast<size_t>(rowPitch) * height);
    hr = pConverter->CopyPixels(nullptr, rowPitch,
        static_cast<UINT>(pixels.size()), pixels.data());
    if (FAILED(hr))
    {
        LogError(pLogger, "LoadImageRGBA8: CopyPixels failed hr=0x%08X", static_cast<unsigned>(hr));
        return false;
    }

    outImage->Width  = width;
    outImage->Height = height;
    outImage->Pixels = std::move(pixels);

    LogInfo(pLogger, "LoadImageRGBA8: loaded %ux%u from '%ls'", width, height, path);
    return true;
}

} // namespace TerrainViewer
} // namespace Canvas
