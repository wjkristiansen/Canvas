// HlslTypes.h — Shared between C++ and HLSL
// Defines GPU-visible struct layouts and constants used by both shader and CPU code.
#pragma once

#ifdef __cplusplus
#include <cstdint>
#define ALIGN16 alignas(16)
#define ROW_MAJOR
struct float2 { float x, y; };
struct float4 { float x, y, z, w; };
struct float4x4 { float m[4][4]; };
typedef uint32_t uint;
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4324) // structure was padded due to alignment specifier
#endif
#else
#define ALIGN16
#define ROW_MAJOR row_major
#endif

// Light types (match Canvas::LightType enum values)
#define LIGHT_AMBIENT     0
#define LIGHT_POINT       1
#define LIGHT_DIRECTIONAL 2
#define LIGHT_SPOT        3
#define LIGHT_AREA        4

#define MAX_LIGHTS_PER_REGION 1024

#ifdef __cplusplus
namespace HlslTypes {
#endif

struct ALIGN16 HlslLight
{
    float4 DirectionOrPosition;
    float4 Color;
    float4 DirectionAndSpot;
    float4 AttenuationAndRange;

    // Layout note: the four scalars below pack into exactly one 16-byte
    // HLSL CB chunk so that ShadowAtlasRectUV lands on a 16-byte boundary
    // without HLSL needing to add hidden padding -- which would desync
    // from the C++ float4's natural 4-byte alignment and corrupt every
    // field after it.  Do not insert padding here.
    //   Type                     - LIGHT_* enum value (matches LightType).
    //   ShadowFlags bit 0        = HasShadow (atlas + view matrix valid).
    //   ShadowDepthBias          - constant added to receiver NDC z before
    //                              the hardware compare (reverse-Z).
    //   ShadowNormalOffsetTexels - push the sample point along the
    //                              surface normal at compare time, in
    //                              shadow-atlas texels of this light's tile.
    uint   Type;
    uint   ShadowFlags;
    float  ShadowDepthBias;
    float  ShadowNormalOffsetTexels;

    //   ShadowAtlasRectUV.xy     = atlas-UV origin of this light's tile,
    //                    .zw     = atlas-UV size of this light's tile.
    //                              (0,0,0,0) when the light has no allocated tile.
    //   ShadowViewProj           - world-space row vector -> shadow NDC.
    //                              Reverse-Z ortho for directional lights.
    float4 ShadowAtlasRectUV;
    ROW_MAJOR float4x4 ShadowViewProj;
};

#define SHADOW_FLAG_HAS_SHADOW (1u << 0)

struct ALIGN16 HlslPerFrameConstants
{
    ROW_MAJOR float4x4 ViewProj;
    float4 CameraWorldPos;

    // Per-frame camera basis + projection params for view-ray reconstruction
    // in fullscreen passes (e.g. the analytic sky in PSComposite). Packs
    // forward / right / up world-space basis vectors with the FOV-derived
    // tangent and aspect ratio in the w channels.
    float4 CamForwardAndTanHalfFov;   // xyz = forward, w = tan(fovY / 2)
    float4 CamRightAndAspect;         // xyz = right,   w = aspect (W/H)
    float4 CamUp;                     // xyz = up,      w = unused

    uint LightCount;
    float LightCullThreshold;
    float Exposure;            // Linear scene-radiance multiplier (exp2(stops))
    float _PerFramePad0;

    // Shadow-atlas global parameters. ShadowAtlasSize is in texels (atlas is
    // square). ShadowPcfTexelStep = 1 / ShadowAtlasSize, precomputed on the
    // CPU so the composite shader can compute neighbour-tap offsets without
    // a divide.
    uint   ShadowAtlasSize;
    float  ShadowPcfTexelStep;
    float  _ShadowAtlasPad0;
    float  _ShadowAtlasPad1;

    // Scene background.  Composite fills empty G-buffer pixels with either
    // the SolidColor (when SkyHasCubemap == 0) or a sample from the bound
    // skybox cubemap (t3, and optionally t4 with SkyBlendFactor crossfade
    // when SkyHasCubemapB != 0).  SkyOrientationQuat rotates the view ray
    // (its conjugate is applied) before sampling so cubemaps can be authored
    // in any basis.  SkyIntensity is a linear multiplier on the sampled
    // cubemap color (not applied to SolidColor).
    float4 SkySolidColor;            // rgb = solid background, a unused
    float4 SkyOrientationQuat;       // (x, y, z, w), identity = (0,0,0,1)
    uint   SkyHasCubemap;            // 0 = no skybox bound -> use SolidColor
    uint   SkyHasCubemapB;           // 0 = single-cube path (no crossfade)
    float  SkyBlendFactor;           // 0 = A only, 1 = B only
    float  SkyIntensity;             // linear multiplier on cubemap sample

    // Stars overlay: rotating RGBA cubemap (just stars; sun and moon
    // are rendered procedurally / billboard below).  Bound at t5; null
    // SRV when HasStars == 0.
    float4 StarsOrientationQuat;     // identity = (0, 0, 0, 1)
    uint   HasStars;                 // 0 = no stars cube bound
    float  StarsIntensity;
    float  _StarsPad0;
    float  _StarsPad1;

    // Sun: procedural disc in the composite shader.  SunColor.a > 0
    // enables; a controls overall intensity.  cos(SunAngularRadius)
    // is precomputed by the engine for the cone test.
    float4 SunDirAndCosRadius;       // xyz = unit dir, w = cos(SunAngularRadius)
    float4 SunColorAndIntensity;     // rgb = additive tint, a = intensity (0 disables)

    // Moon: textured billboard.  Texture bound at t6; null SRV when
    // HasMoon == 0.  cos(MoonAngularRadius) precomputed for cone test.
    float4 MoonDirAndCosRadius;      // xyz = unit dir, w = cos(MoonAngularRadius)
    float4 MoonColorAndIntensity;    // rgb = tint, a = overall multiplier
    uint   HasMoon;                  // 0 = no moon texture bound
    float  _MoonPad0;
    float  _MoonPad1;
    float  _MoonPad2;

    // Per-frame lights live in a separate StructuredBuffer<HlslLight>
    // bound at t8, sized to LightCount.  Kept out of this CB so the
    // per-frame constants stay small and LightCount can scale beyond
    // a fixed-size CB array.  See PSComposite.hlsl for the buffer
    // declaration.
};

struct ALIGN16 HlslPerObjectConstants
{
    ROW_MAJOR float4x4 World;
    ROW_MAJOR float4x4 WorldInvTranspose;
    float4 BaseColorFactor;       // RGBA, multiplied with sampled albedo
    float4 EmissiveFactor;        // RGB, multiplied with sampled emissive (A unused)
    float4 RoughMetalAOFactor;    // R=roughness, G=metallic, B=AO, A unused
    uint   MaterialFlags;         // see MATERIAL_FLAG_* bits below
    uint   _Pad0;
    uint   _Pad1;
    uint   _Pad2;
};

//------------------------------------------------------------------------------------------------
// Per-instance constants for the engine's displaced-mesh pipeline
// (VS/HS/DS/PS in Displaced.hlsli). Populated by the engine from the
// mesh instance's world transform and the paired material's
// GfxDisplacementDesc.
struct ALIGN16 HlslDisplacedConstants
{
    ROW_MAJOR float4x4 World;        // Per-tile world transform applied to mesh-local CP positions
    float  MapScale;                 // World units per [0, 1] displacement-map sample
    float  MapBias;                  // World units added after scale
    uint   _Pad0;
    uint   _Pad1;
};

// Per-draw material flags. Bits are uniform per-draw and used to enable
// optional sample/multiply paths in the uber-shader. When a flag is clear the
// corresponding sampled value is ignored and only the *Factor is used.
#define MATERIAL_FLAG_HAS_UV          (1u << 0)
#define MATERIAL_FLAG_HAS_TANGENT     (1u << 1)
#define MATERIAL_FLAG_HAS_ALBEDO_TEX  (1u << 2)
#define MATERIAL_FLAG_HAS_NORMAL_TEX  (1u << 3)
#define MATERIAL_FLAG_HAS_EMISSIVE_TEX (1u << 4)
#define MATERIAL_FLAG_HAS_ROUGH_TEX   (1u << 5)
#define MATERIAL_FLAG_HAS_METAL_TEX   (1u << 6)
#define MATERIAL_FLAG_HAS_AO_TEX      (1u << 7)

#ifdef __cplusplus
} // namespace HlslTypes
#endif

//------------------------------------------------------------------------------------------------
// UI rendering types — shared between UI shaders and the graphics backend.
// These live outside the HlslTypes namespace in HLSL but inside it in C++.
//------------------------------------------------------------------------------------------------

#ifdef __cplusplus
namespace HlslTypes {
#endif

// Per-glyph instance for GPU-driven text rendering (StructuredBuffer element).
// The vertex shader expands each instance to a 6-vertex quad via SV_VertexID.
struct HlslGlyphInstance
{
    float2 Offset;           // Element-local pixel position of quad top-left
    float2 Size;             // Quad width, height in pixels
    float4 AtlasUV;          // (u0, v0, u1, v1)
};

// Per-draw constants for text rendering (root CBV).
struct ALIGN16 HlslTextConstants
{
    float2 ScreenSize;       // Viewport width, height in pixels
    float2 ElementOffset;    // Element screen-space position (pixels)
    float4 TextColor;        // RGBA text color (uniform per draw)
};

// Per-draw constants for rectangle rendering (root CBV).
struct ALIGN16 HlslRectConstants
{
    float2 ScreenSize;       // Viewport width, height in pixels
    float2 ElementOffset;    // Element screen-space position (pixels)
    float2 RectSize;         // Rectangle width, height in pixels
    float2 _Pad0;            // Padding to align FillColor to 16-byte boundary
    float4 FillColor;        // RGBA fill color
};

#ifdef __cplusplus
} // namespace HlslTypes
#endif

#ifdef __cplusplus
static_assert(sizeof(HlslTypes::HlslGlyphInstance) == 32, "HlslGlyphInstance must be 32 bytes");
static_assert(sizeof(HlslTypes::HlslTextConstants) == 32, "HlslTextConstants must be 32 bytes");
static_assert(sizeof(HlslTypes::HlslRectConstants) == 48, "HlslRectConstants must be 48 bytes");
// HlslLight layout must match the HLSL CB packing exactly: the four
// scalars after AttenuationAndRange (Type, ShadowFlags, ShadowDepthBias,
// ShadowNormalOffsetTexels) fully consume one 16-byte chunk, then
// ShadowAtlasRectUV at offset 80 and ShadowViewProj at offset 96.
// 4*16 (vec4s) + 16 (scalar chunk) + 16 (rect) + 64 (matrix) = 160 bytes.
static_assert(offsetof(HlslTypes::HlslLight, ShadowAtlasRectUV) == 80,
    "ShadowAtlasRectUV must sit at offset 80 to match HLSL CB packing");
static_assert(offsetof(HlslTypes::HlslLight, ShadowViewProj) == 96,
    "ShadowViewProj must sit at offset 96 to match HLSL CB packing");
static_assert(sizeof(HlslTypes::HlslLight) == 160, "HlslLight must be 160 bytes");
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#endif
