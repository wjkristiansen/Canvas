#include "pch.h"
#include "SkyCubeLoader.h"
#include "ImageLoader.h"

namespace Canvas
{
namespace TerrainViewer
{

namespace
{

// D3D cube face order: +X, -X, +Y, -Y, +Z, -Z (matches the array slice index
// expected by D3D12 cube SRV sampling).
constexpr const char* kFaceSuffix[6] =
{
    "posx", "negx", "posy", "negy", "posz", "negz",
};

} // anonymous namespace

bool LoadSkyCube(
    XGfxDevice*                  pDevice,
    const std::filesystem::path& assetsDir,
    const char*                  presetName,
    XGfxSurface**                ppOutCube,
    XLogger*                     pLogger)
{
    if (!pDevice || !presetName || !ppOutCube)
    {
        LogError(pLogger, "LoadSkyCube: null argument");
        return false;
    }
    *ppOutCube = nullptr;

    // Load all six faces into RAM first so we can validate dimensions before
    // committing a GPU surface allocation.
    ImageRGBA8 faces[6];
    for (uint32_t i = 0; i < 6; ++i)
    {
        char filename[64];
        snprintf(filename, sizeof(filename), "sky_%s_%s.png", presetName, kFaceSuffix[i]);
        const std::filesystem::path facePath = assetsDir / filename;

        if (!LoadImageRGBA8(facePath.wstring().c_str(), &faces[i], pLogger))
        {
            LogError(pLogger, "LoadSkyCube: failed to load face '%s'",
                facePath.string().c_str());
            return false;
        }
        if (faces[i].Width != faces[0].Width || faces[i].Height != faces[0].Height)
        {
            LogError(pLogger,
                "LoadSkyCube: face '%s' dimensions %ux%u do not match face 0 (%ux%u)",
                facePath.string().c_str(),
                faces[i].Width, faces[i].Height,
                faces[0].Width, faces[0].Height);
            return false;
        }
    }

    if (faces[0].Width != faces[0].Height)
    {
        LogError(pLogger,
            "LoadSkyCube: preset '%s' faces are not square (%ux%u)",
            presetName, faces[0].Width, faces[0].Height);
        return false;
    }

    const uint32_t faceSize = faces[0].Width;

    GfxSurfaceDesc desc = GfxSurfaceDesc::SurfaceDescCube(
        GfxFormat::R8G8B8A8_UNorm,
        faceSize,
        /*arraySize*/ 1,
        SurfaceFlag_ShaderResource);

    Gem::TGemPtr<XGfxSurface> pCube;
    Gem::Result hr = pDevice->CreateSurface(desc, &pCube);
    if (Gem::Failed(hr))
    {
        LogError(pLogger,
            "LoadSkyCube: CreateSurface failed for preset '%s' (%s)",
            presetName, GemResultString(hr));
        return false;
    }

    const uint32_t rowPitchBytes = faceSize * 4;
    for (uint32_t i = 0; i < 6; ++i)
    {
        // Subresource index for a 1-mip cube is the face index (arraySlice).
        hr = pDevice->UploadTextureRegion(
            pCube,
            /*subresourceIndex*/ i,
            /*dstX*/ 0, /*dstY*/ 0,
            faceSize, faceSize,
            faces[i].Pixels.data(), rowPitchBytes);
        if (Gem::Failed(hr))
        {
            LogError(pLogger,
                "LoadSkyCube: UploadTextureRegion failed for preset '%s' face %u (%s)",
                presetName, i, GemResultString(hr));
            return false;
        }
    }

    LogInfo(pLogger,
        "LoadSkyCube: preset '%s' loaded (6 x %ux%u faces)",
        presetName, faceSize, faceSize);

    *ppOutCube = pCube.Detach();
    return true;
}

} // namespace TerrainViewer
} // namespace Canvas
