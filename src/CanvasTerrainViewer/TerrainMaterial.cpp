#include "pch.h"
#include "TerrainMaterial.h"

#include <limits>

namespace Canvas
{
namespace TerrainViewer
{

namespace
{

// Smoothstep with explicit edges, returning 0..1.
inline float smoothstep01(float edge0, float edge1, float x)
{
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Raw view of a decoded RGBA8 atlas. Built once from an XImage so the per-texel
// sampling loop reads pixels directly instead of paying a virtual call per access.
struct AtlasView
{
    const uint8_t* Pixels;
    uint32_t       Width;
    uint32_t       Height;
};

// Sample an atlas slot at world-relative UV. The atlas is 2x2 of equal-sized
// slots; slotU/slotV in {0, 1} pick the slot. (atlasU, atlasV) are world UVs
// in [0, 1) coordinates *within a single slot* (modulo the slot's repeat
// period); wrapped here, scaled into the half-atlas range, and offset.
//
// Returns the RGBA8 atlas pixel via the per-channel outputs.
inline void SampleAtlas(
    const AtlasView&  atlas,
    uint32_t          slotU,    // 0 or 1
    uint32_t          slotV,    // 0 or 1
    float             atlasU,
    float             atlasV,
    uint8_t&          outR,
    uint8_t&          outG,
    uint8_t&          outB)
{
    // Half-atlas extent in pixels.
    const uint32_t halfW = atlas.Width  / 2;
    const uint32_t halfH = atlas.Height / 2;

    // Wrap UVs into [0, 1).
    atlasU -= std::floor(atlasU);
    atlasV -= std::floor(atlasV);

    // Pixel index within the slot.
    uint32_t px = static_cast<uint32_t>(atlasU * static_cast<float>(halfW));
    uint32_t py = static_cast<uint32_t>(atlasV * static_cast<float>(halfH));
    if (px >= halfW) px = halfW - 1;
    if (py >= halfH) py = halfH - 1;

    // Global atlas pixel coords.
    const uint32_t gx = slotU * halfW + px;
    const uint32_t gy = slotV * halfH + py;

    const size_t off = (static_cast<size_t>(gy) * atlas.Width + gx) * 4u;
    outR = atlas.Pixels[off + 0];
    outG = atlas.Pixels[off + 1];
    outB = atlas.Pixels[off + 2];
}

} // anonymous namespace

Gem::Result BuildTerrainMaterial(
    XGfxDevice*                     pDevice,
    const HeightField::HeightField& field,
    Canvas::Platform::Win32::XImage* albedoAtlas,
    Canvas::Platform::Win32::XImage* ormAtlas,
    const TerrainMaterialOptions&   opts,
    TerrainMaterialOutputs*         outputs,
    XLogger*                        pLogger)
{
    if (!pDevice || !outputs || !albedoAtlas || !ormAtlas)
        return Gem::Result::InvalidArg;
    *outputs = TerrainMaterialOutputs{};
    if (field.IsEmpty() || albedoAtlas->IsEmpty() || ormAtlas->IsEmpty())
        return Gem::Result::InvalidArg;
    if ((albedoAtlas->GetWidth() & 1u) || (albedoAtlas->GetHeight() & 1u) ||
        (ormAtlas->GetWidth()    & 1u) || (ormAtlas->GetHeight()    & 1u))
    {
        LogError(pLogger, "BuildTerrainMaterial: atlas dimensions must be even (2x2 packing)");
        return Gem::Result::InvalidArg;
    }

    const AtlasView albedoView{ albedoAtlas->GetPixels(), albedoAtlas->GetWidth(), albedoAtlas->GetHeight() };
    const AtlasView ormView   { ormAtlas->GetPixels(),    ormAtlas->GetWidth(),    ormAtlas->GetHeight() };

    const uint32_t W = field.Desc.Width;
    const uint32_t H = field.Desc.Height;
    const float dxy = field.Desc.DxyMeters;
    const float invRepeat = 1.0f / std::max(opts.AtlasRepeatMeters, 0.001f);

    // Slot indices into the 2x2 atlas: [grass=TL rock=TR sand=BL snow=BR].
    constexpr uint32_t kGrassU = 0, kGrassV = 0;
    constexpr uint32_t kRockU  = 1, kRockV  = 0;
    constexpr uint32_t kSandU  = 0, kSandV  = 1;
    constexpr uint32_t kSnowU  = 1, kSnowV  = 1;

    std::vector<uint8_t> albedoPixels(static_cast<size_t>(W) * H * 4u);
    std::vector<uint8_t> aoPixels    (static_cast<size_t>(W) * H);
    std::vector<uint8_t> roughPixels (static_cast<size_t>(W) * H);

    // Diagnostic: track the altitude range we actually saw to surface
    // mismatches between heightmap content and biome thresholds.
    float seenMinH = std::numeric_limits<float>::infinity();
    float seenMaxH = -std::numeric_limits<float>::infinity();

    for (uint32_t y = 0; y < H; ++y)
    {
        const uint32_t ym = (y > 0)     ? y - 1 : y;
        const uint32_t yp = (y + 1 < H) ? y + 1 : y;

        for (uint32_t x = 0; x < W; ++x)
        {
            const uint32_t xm = (x > 0)     ? x - 1 : x;
            const uint32_t xp = (x + 1 < W) ? x + 1 : x;

            const float h   = field.HeightMeters(x,  y);
            seenMinH = std::min(seenMinH, h);
            seenMaxH = std::max(seenMaxH, h);
            const float hL  = field.HeightMeters(xm, y);
            const float hR  = field.HeightMeters(xp, y);
            const float hD  = field.HeightMeters(x,  ym);
            const float hU  = field.HeightMeters(x,  yp);
            const float dhdx = (hR - hL) / (2.0f * dxy);
            const float dhdy = (hU - hD) / (2.0f * dxy);

            // Slope: 1 - normal.z. 0 = flat, 1 = vertical cliff. By construction
            // the heightfield loader rejects non-finite HeightScale/HeightBias,
            // so dhdx/dhdy/h are guaranteed finite here.
            const float invLen = 1.0f / std::sqrt(dhdx * dhdx + dhdy * dhdy + 1.0f);
            const float slope  = 1.0f - invLen;

            // Absolute-meter altitude blend. Sand below SandMaxMeters fading
            // to grass by SandFadeMeters; snow above SnowMinMeters fading in
            // by SnowFadeMeters. Rock dominates whenever slope crosses the
            // rock band.
            float wSand  = 1.0f - smoothstep01(opts.SandMaxMeters, opts.SandFadeMeters, h);
            float wSnow  = smoothstep01(opts.SnowMinMeters, opts.SnowFadeMeters, h)
                         * (1.0f - smoothstep01(0.55f, 0.85f, slope));
            float wRock  = smoothstep01(opts.SlopeRockMin, opts.SlopeRockMax, slope);
            float wGrass = (1.0f - wRock) * (1.0f - wSand) * (1.0f - wSnow);
            wGrass = std::max(wGrass, 0.0f);

            const float wSum = wSand + wSnow + wRock + wGrass + 1e-6f;
            wSand  /= wSum;
            wSnow  /= wSum;
            wRock  /= wSum;
            wGrass /= wSum;

            // World UV into each atlas slot. The slot's UV space wraps every
            // AtlasRepeatMeters of world distance.
            const float wx = opts.OriginX + static_cast<float>(x) * dxy;
            const float wy = opts.OriginY + static_cast<float>(y) * dxy;
            const float aU = wx * invRepeat;
            const float aV = wy * invRepeat;

            // Sample each slot once from each atlas (4 samples per atlas).
            uint8_t gR, gG, gB; SampleAtlas(albedoView, kGrassU, kGrassV, aU, aV, gR, gG, gB);
            uint8_t rR, rG, rB; SampleAtlas(albedoView, kRockU,  kRockV,  aU, aV, rR, rG, rB);
            uint8_t sR, sG, sB; SampleAtlas(albedoView, kSandU,  kSandV,  aU, aV, sR, sG, sB);
            uint8_t nR, nG, nB; SampleAtlas(albedoView, kSnowU,  kSnowV,  aU, aV, nR, nG, nB);

            uint8_t go_ao, go_rough, go_metal; (void)go_metal;
            SampleAtlas(ormView, kGrassU, kGrassV, aU, aV, go_ao, go_rough, go_metal);
            uint8_t ro_ao, ro_rough, ro_metal; (void)ro_metal;
            SampleAtlas(ormView, kRockU,  kRockV,  aU, aV, ro_ao, ro_rough, ro_metal);
            uint8_t so_ao, so_rough, so_metal; (void)so_metal;
            SampleAtlas(ormView, kSandU,  kSandV,  aU, aV, so_ao, so_rough, so_metal);
            uint8_t no_ao, no_rough, no_metal; (void)no_metal;
            SampleAtlas(ormView, kSnowU,  kSnowV,  aU, aV, no_ao, no_rough, no_metal);

            // Blend.
            const float rF = wGrass * gR + wRock * rR + wSand * sR + wSnow * nR;
            const float gF = wGrass * gG + wRock * rG + wSand * sG + wSnow * nG;
            const float bF = wGrass * gB + wRock * rB + wSand * sB + wSnow * nB;
            const float aoF    = wGrass * go_ao    + wRock * ro_ao    + wSand * so_ao    + wSnow * no_ao;
            const float roughF = wGrass * go_rough + wRock * ro_rough + wSand * so_rough + wSnow * no_rough;

            const size_t aOff = (static_cast<size_t>(y) * W + x) * 4u;
            albedoPixels[aOff + 0] = static_cast<uint8_t>(std::clamp(rF, 0.0f, 255.0f));
            albedoPixels[aOff + 1] = static_cast<uint8_t>(std::clamp(gF, 0.0f, 255.0f));
            albedoPixels[aOff + 2] = static_cast<uint8_t>(std::clamp(bF, 0.0f, 255.0f));
            albedoPixels[aOff + 3] = 255;

            const size_t sOff = static_cast<size_t>(y) * W + x;
            aoPixels   [sOff] = static_cast<uint8_t>(std::clamp(aoF,    0.0f, 255.0f));
            roughPixels[sOff] = static_cast<uint8_t>(std::clamp(roughF, 0.0f, 255.0f));
        }
    }

    // Create + upload the three surfaces.
    auto MakeAndUpload = [&](GfxFormat fmt, uint32_t bpp,
                             const std::vector<uint8_t>& data,
                             Gem::TGemPtr<XGfxSurface>& out) -> Gem::Result
    {
        GfxSurfaceDesc desc = GfxSurfaceDesc::SurfaceDesc2D(fmt, W, H, SurfaceFlag_ShaderResource);
        Gem::Result r = pDevice->CreateSurface(desc, &out);
        if (Gem::Failed(r))
            return r;
        return pDevice->UploadTextureRegion(out.Get(), 0, 0, 0, W, H, data.data(), W * bpp);
    };

    Gem::Result r;
    r = MakeAndUpload(GfxFormat::R8G8B8A8_UNorm, 4, albedoPixels, outputs->pAlbedo);
    if (Gem::Failed(r)) { LogError(pLogger, "BuildTerrainMaterial: albedo surface failed"); return r; }
    r = MakeAndUpload(GfxFormat::R8_UNorm,       1, aoPixels,    outputs->pAO);
    if (Gem::Failed(r)) { LogError(pLogger, "BuildTerrainMaterial: AO surface failed");    return r; }
    r = MakeAndUpload(GfxFormat::R8_UNorm,       1, roughPixels, outputs->pRoughness);
    if (Gem::Failed(r)) { LogError(pLogger, "BuildTerrainMaterial: roughness surface failed"); return r; }

    LogInfo(pLogger,
        "BuildTerrainMaterial: baked %ux%u composites (albedo + AO + roughness). "
        "Altitude range seen: [%.2f, %.2f] m (heightScale %.1f m). "
        "Biome thresholds: sand <%.1fm, snow >%.1fm, rock slope >%.2f.",
        W, H, seenMinH, seenMaxH, field.Desc.HeightScale,
        opts.SandFadeMeters, opts.SnowMinMeters, opts.SlopeRockMin);
    return Gem::Result::Success;
}

} // namespace TerrainViewer
} // namespace Canvas
