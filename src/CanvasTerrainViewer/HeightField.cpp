//================================================================================================
// HeightField - implementation
//
// WIC-based loader for Windows + a CPU mesh builder for the v1 terrain path.
//================================================================================================

#include "pch.h"
#include "HeightField.h"

namespace Canvas
{
namespace HeightField
{

namespace
{

#if defined(_WIN32)

//------------------------------------------------------------------------------------------------
// Pick a WIC destination format for the source pixel format. We always
// terminate at one of:
//   - GUID_WICPixelFormat16bppGray   -> straight R16 copy
//   - GUID_WICPixelFormat8bppGray    -> R8 promoted to R16
//
// Anything else (RGB, RGBA, paletted, ...) is converted to 8bpp gray as a
// best-effort luminance promotion. That is acceptable because hand-authored
// 16-bit heightmaps are typically already grayscale.
const GUID& ChooseTargetFormat(const GUID& sourceFormat)
{
    if (IsEqualGUID(sourceFormat, GUID_WICPixelFormat16bppGray) ||
        IsEqualGUID(sourceFormat, GUID_WICPixelFormat16bppGrayHalf) ||
        IsEqualGUID(sourceFormat, GUID_WICPixelFormat32bppGrayFloat))
    {
        return GUID_WICPixelFormat16bppGray;
    }
    return GUID_WICPixelFormat8bppGray;
}

//------------------------------------------------------------------------------------------------
HRESULT EnsureFactory(CComPtr<IWICImagingFactory>& outFactory)
{
    if (outFactory)
        return S_OK;
    return CoCreateInstance(
        CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&outFactory));
}

#endif // _WIN32

} // anonymous namespace

//------------------------------------------------------------------------------------------------
bool LoadHeightFieldWIC(
    const wchar_t*     path,
    const LoadOptions& opts,
    HeightField*       outField,
    XLogger*           pLogger)
{
    if (!outField)
        return false;
    *outField = HeightField{};

#if !defined(_WIN32)
    (void)path; (void)opts; (void)pLogger;
    LogError(pLogger, "LoadHeightFieldWIC: WIC backend not available on this platform");
    return false;
#else
    if (!path || !*path)
    {
        LogError(pLogger, "LoadHeightFieldWIC: null/empty path");
        return false;
    }

    // Validate scale/bias before doing any work; non-finite values would
    // propagate NaN/Inf into every downstream consumer.
    if (!std::isfinite(opts.HeightScale) || opts.HeightScale <= 0.0f)
    {
        LogError(pLogger, "LoadHeightFieldWIC: HeightScale must be a finite positive value (got %g)",
            static_cast<double>(opts.HeightScale));
        return false;
    }
    if (!std::isfinite(opts.HeightBias))
    {
        LogError(pLogger, "LoadHeightFieldWIC: HeightBias must be finite (got %g)",
            static_cast<double>(opts.HeightBias));
        return false;
    }
    if (!std::isfinite(opts.DxyMeters) || opts.DxyMeters <= 0.0f)
    {
        LogError(pLogger, "LoadHeightFieldWIC: DxyMeters must be a finite positive value (got %g)",
            static_cast<double>(opts.DxyMeters));
        return false;
    }

    CComPtr<IWICImagingFactory> pFactory;
    HRESULT hr = EnsureFactory(pFactory);
    if (FAILED(hr))
    {
        LogError(pLogger, "LoadHeightFieldWIC: WIC factory creation failed hr=0x%08X",
            static_cast<unsigned>(hr));
        return false;
    }

    CComPtr<IWICBitmapDecoder> pDecoder;
    hr = pFactory->CreateDecoderFromFilename(
        path, nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnDemand, &pDecoder);
    if (FAILED(hr))
    {
        LogError(pLogger, "LoadHeightFieldWIC: failed to open '%ls' hr=0x%08X",
            path, static_cast<unsigned>(hr));
        return false;
    }

    CComPtr<IWICBitmapFrameDecode> pFrame;
    hr = pDecoder->GetFrame(0, &pFrame);
    if (FAILED(hr))
    {
        LogError(pLogger, "LoadHeightFieldWIC: GetFrame failed hr=0x%08X", static_cast<unsigned>(hr));
        return false;
    }

    UINT width = 0, height = 0;
    hr = pFrame->GetSize(&width, &height);
    if (FAILED(hr) || width == 0 || height == 0)
    {
        LogError(pLogger, "LoadHeightFieldWIC: invalid image size %ux%u", width, height);
        return false;
    }

    WICPixelFormatGUID srcFormat{};
    hr = pFrame->GetPixelFormat(&srcFormat);
    if (FAILED(hr))
    {
        LogError(pLogger, "LoadHeightFieldWIC: GetPixelFormat failed hr=0x%08X",
            static_cast<unsigned>(hr));
        return false;
    }

    const GUID& dstFormat = ChooseTargetFormat(srcFormat);
    const bool dst16 = IsEqualGUID(dstFormat, GUID_WICPixelFormat16bppGray);

    CComPtr<IWICFormatConverter> pConverter;
    hr = pFactory->CreateFormatConverter(&pConverter);
    if (FAILED(hr))
        return false;
    hr = pConverter->Initialize(pFrame, dstFormat,
        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr))
    {
        LogError(pLogger, "LoadHeightFieldWIC: format converter init failed hr=0x%08X",
            static_cast<unsigned>(hr));
        return false;
    }

    HeightField field;
    field.Desc.Width       = width;
    field.Desc.Height      = height;
    field.Desc.DxyMeters   = opts.DxyMeters;
    field.Desc.HeightScale = opts.HeightScale;
    field.Desc.HeightBias  = opts.HeightBias;
    field.Samples.resize(static_cast<size_t>(width) * height);

    if (dst16)
    {
        const UINT rowPitch = width * sizeof(uint16_t);
        hr = pConverter->CopyPixels(
            nullptr, rowPitch,
            static_cast<UINT>(field.Samples.size() * sizeof(uint16_t)),
            reinterpret_cast<BYTE*>(field.Samples.data()));
        if (FAILED(hr))
        {
            LogError(pLogger, "LoadHeightFieldWIC: CopyPixels (16bpp) failed hr=0x%08X",
                static_cast<unsigned>(hr));
            return false;
        }
    }
    else
    {
        // 8bpp gray -> promote each byte to 16-bit by replicating into both
        // halves so [0..255] maps to [0..65535] uniformly.
        std::vector<uint8_t> bytes(static_cast<size_t>(width) * height);
        const UINT rowPitch = width;
        hr = pConverter->CopyPixels(
            nullptr, rowPitch,
            static_cast<UINT>(bytes.size()), bytes.data());
        if (FAILED(hr))
        {
            LogError(pLogger, "LoadHeightFieldWIC: CopyPixels (8bpp) failed hr=0x%08X",
                static_cast<unsigned>(hr));
            return false;
        }
        for (size_t i = 0; i < bytes.size(); ++i)
        {
            const uint16_t v = static_cast<uint16_t>(bytes[i]);
            field.Samples[i] = static_cast<uint16_t>((v << 8) | v);
        }
    }

    LogInfo(pLogger,
        "LoadHeightFieldWIC: loaded %ux%u (dxy=%.3fm, hScale=%.1fm, world=%.1fx%.1fm)",
        width, height, opts.DxyMeters, opts.HeightScale,
        field.WorldWidth(), field.WorldHeight());

    *outField = std::move(field);
    return true;
#endif // _WIN32
}

//------------------------------------------------------------------------------------------------
bool BuildMipChain(const HeightField& field, std::vector<HeightMipLevel>* outMips)
{
    if (!outMips) return false;
    outMips->clear();
    if (field.IsEmpty()) return false;

    HeightMipLevel mip0;
    mip0.Width   = field.Desc.Width;
    mip0.Height  = field.Desc.Height;
    mip0.Samples = field.Samples;
    outMips->push_back(std::move(mip0));

    uint32_t w = field.Desc.Width;
    uint32_t h = field.Desc.Height;
    while (w > 1 || h > 1)
    {
        const uint32_t prevW = w;
        const uint32_t prevH = h;
        const uint32_t nextW = std::max(1u, w / 2u);
        const uint32_t nextH = std::max(1u, h / 2u);

        HeightMipLevel m;
        m.Width  = nextW;
        m.Height = nextH;
        m.Samples.resize(static_cast<size_t>(nextW) * nextH);

        const auto& prev = outMips->back().Samples;
        for (uint32_t y = 0; y < nextH; ++y)
        {
            const uint32_t py0 = std::min(y * 2u,      prevH - 1u);
            const uint32_t py1 = std::min(y * 2u + 1u, prevH - 1u);
            for (uint32_t x = 0; x < nextW; ++x)
            {
                const uint32_t px0 = std::min(x * 2u,      prevW - 1u);
                const uint32_t px1 = std::min(x * 2u + 1u, prevW - 1u);
                const uint32_t sum =
                      static_cast<uint32_t>(prev[static_cast<size_t>(py0) * prevW + px0])
                    + static_cast<uint32_t>(prev[static_cast<size_t>(py0) * prevW + px1])
                    + static_cast<uint32_t>(prev[static_cast<size_t>(py1) * prevW + px0])
                    + static_cast<uint32_t>(prev[static_cast<size_t>(py1) * prevW + px1]);
                m.Samples[static_cast<size_t>(y) * nextW + x] =
                    static_cast<uint16_t>((sum + 2u) / 4u);
            }
        }

        outMips->push_back(std::move(m));
        w = nextW;
        h = nextH;
    }

    return true;
}

//------------------------------------------------------------------------------------------------
// v1 CPU mesh builder.
//
// Produces a regular triangulated grid covering the heightfield. No index
// buffer (the existing CreateMeshData path is non-indexed): each quad emits
// 6 vertices. Per-vertex normals come from central differences against the
// height samples, clamped at the tile boundary (in a tiled scene the
// boundary samples are shared with the neighbor, so both sides see the
// same normal).
Gem::Result BuildTerrainMesh(
    XGfxDevice*               pDevice,
    const HeightField&        field,
    const TerrainMeshOptions& opts,
    XGfxMeshData**            ppMesh,
    PCSTR                     name)
{
    if (!pDevice || !ppMesh)
        return Gem::Result::InvalidArg;
    *ppMesh = nullptr;
    if (field.IsEmpty())
        return Gem::Result::InvalidArg;

    if (field.Desc.Width < 2 || field.Desc.Height < 2)
        return Gem::Result::InvalidArg;

    const uint32_t stride = std::max<uint32_t>(opts.Stride, 1u);

    // Decimated vertex grid: every `stride` texels is one vertex.
    const uint32_t cellsX = (field.Desc.Width  - 1) / stride;
    const uint32_t cellsY = (field.Desc.Height - 1) / stride;
    if (cellsX == 0 || cellsY == 0)
        return Gem::Result::InvalidArg;

    const uint32_t vertsX = cellsX + 1;
    const uint32_t vertsY = cellsY + 1;

    const float dxy = field.Desc.DxyMeters * static_cast<float>(stride);

    // Per-vertex world position + normal in temporary buffers, then expand to
    // the per-triangle vertex stream below.
    std::vector<Math::FloatVector4> gridPos(static_cast<size_t>(vertsX) * vertsY);
    std::vector<Math::FloatVector4> gridNrm(static_cast<size_t>(vertsX) * vertsY);
    std::vector<Math::FloatVector2> gridUv (static_cast<size_t>(vertsX) * vertsY);

    auto SampleAt = [&](int gx, int gy) -> float
    {
        // Decimated-grid coords -> texel coords. Clamp at boundaries; for
        // tiled scenes the shared edge texels match the neighbor tile.
        const int tx = gx * static_cast<int>(stride);
        const int ty = gy * static_cast<int>(stride);
        const int cx = std::clamp(tx, 0, static_cast<int>(field.Desc.Width)  - 1);
        const int cy = std::clamp(ty, 0, static_cast<int>(field.Desc.Height) - 1);
        return field.HeightMeters(static_cast<uint32_t>(cx), static_cast<uint32_t>(cy));
    };

    // Build per-vertex grid.
    for (uint32_t y = 0; y < vertsY; ++y)
    {
        for (uint32_t x = 0; x < vertsX; ++x)
        {
            const float wx = opts.OriginX + static_cast<float>(x) * dxy;
            const float wy = opts.OriginY + static_cast<float>(y) * dxy;
            const float wz = SampleAt(static_cast<int>(x), static_cast<int>(y));

            // Central differences on the decimated grid (sample 1 cell in each
            // direction; falls back to clamped samples at the boundary).
            const float hL = SampleAt(static_cast<int>(x) - 1, static_cast<int>(y));
            const float hR = SampleAt(static_cast<int>(x) + 1, static_cast<int>(y));
            const float hD = SampleAt(static_cast<int>(x), static_cast<int>(y) - 1);
            const float hU = SampleAt(static_cast<int>(x), static_cast<int>(y) + 1);

            // Tangents along world X and Y axes.
            const float dhdx = (hR - hL) / (2.0f * dxy);
            const float dhdy = (hU - hD) / (2.0f * dxy);

            // Surface normal for a heightfield z = h(x, y) is (-dh/dx, -dh/dy, 1)
            // normalized.
            Math::FloatVector4 n(-dhdx, -dhdy, 1.0f, 0.0f);
            n = n.Normalize();

            const size_t i = static_cast<size_t>(y) * vertsX + x;
            gridPos[i] = Math::FloatVector4(wx, wy, wz, 1.0f);
            gridNrm[i] = n;
            // Tile-normalized UVs in [0, 1] across the heightfield so a
            // per-tile composite texture samples 1:1 with terrain texels under
            // the standard LinearWrap/LinearClamp sampler. (For multi-tile
            // scenes each tile binds its own composite; the shared edge still
            // matches because the bake reads from world coords.)
            const float u = (vertsX > 1) ? static_cast<float>(x) / static_cast<float>(vertsX - 1) : 0.0f;
            const float v = (vertsY > 1) ? static_cast<float>(y) / static_cast<float>(vertsY - 1) : 0.0f;
            gridUv [i] = Math::FloatVector2(u, v);
        }
    }

    // Emit a non-indexed triangle list: 2 triangles per cell, 6 vertices each.
    const size_t triVertCount = static_cast<size_t>(cellsX) * cellsY * 6u;

    std::vector<Math::FloatVector4> outPos(triVertCount);
    std::vector<Math::FloatVector4> outNrm(triVertCount);
    std::vector<Math::FloatVector2> outUv (triVertCount);

    size_t w = 0;
    for (uint32_t y = 0; y < cellsY; ++y)
    {
        for (uint32_t x = 0; x < cellsX; ++x)
        {
            const size_t i00 = static_cast<size_t>(y    ) * vertsX + (x    );
            const size_t i10 = static_cast<size_t>(y    ) * vertsX + (x + 1);
            const size_t i01 = static_cast<size_t>(y + 1) * vertsX + (x    );
            const size_t i11 = static_cast<size_t>(y + 1) * vertsX + (x + 1);

            // Tri 1: (0,0) (1,0) (1,1) - CCW when viewed from +Z.
            outPos[w] = gridPos[i00]; outNrm[w] = gridNrm[i00]; outUv[w] = gridUv[i00]; ++w;
            outPos[w] = gridPos[i10]; outNrm[w] = gridNrm[i10]; outUv[w] = gridUv[i10]; ++w;
            outPos[w] = gridPos[i11]; outNrm[w] = gridNrm[i11]; outUv[w] = gridUv[i11]; ++w;

            // Tri 2: (0,0) (1,1) (0,1)
            outPos[w] = gridPos[i00]; outNrm[w] = gridNrm[i00]; outUv[w] = gridUv[i00]; ++w;
            outPos[w] = gridPos[i11]; outNrm[w] = gridNrm[i11]; outUv[w] = gridUv[i11]; ++w;
            outPos[w] = gridPos[i01]; outNrm[w] = gridNrm[i01]; outUv[w] = gridUv[i01]; ++w;
        }
    }

    MeshDataGroupDesc group{};
    group.VertexCount = static_cast<uint32_t>(triVertCount);
    group.pPositions  = outPos.data();
    group.pNormals    = outNrm.data();
    group.pUV0        = outUv.data();
    group.pTangents   = nullptr;
    group.pMaterial   = opts.pMaterial;

    MeshDataDesc desc{};
    desc.pGroups    = &group;
    desc.GroupCount = 1;
    desc.pName      = name;

    return pDevice->CreateMeshData(desc, ppMesh);
}

} // namespace HeightField
} // namespace Canvas
