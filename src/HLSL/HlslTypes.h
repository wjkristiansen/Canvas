// HlslTypes.h — Shared between C++ and HLSL
// Defines GPU-visible struct layouts and constants used by both shader and CPU code.
#pragma once

#ifdef __cplusplus
#include <cstdint>
#define ALIGN16 alignas(16)
#define ROW_MAJOR
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

#define MAX_LIGHTS_PER_REGION 16

#ifdef __cplusplus
namespace HlslTypes {
#endif

struct ALIGN16 HlslLight
{
    float4 DirectionOrPosition;
    float4 Color;
    float4 DirectionAndSpot;
    float4 AttenuationAndRange;
    uint   Type;
};

struct ALIGN16 HlslPerFrameConstants
{
    ROW_MAJOR float4x4 ViewProj;
    float4 CameraWorldPos;
    uint LightCount;
    float LightCullThreshold;
    float Exposure;            // Linear scene-radiance multiplier (exp2(stops))
    float _PerFramePad0;
    HlslLight Lights[MAX_LIGHTS_PER_REGION];
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
