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
    HlslLight Lights[MAX_LIGHTS_PER_REGION];
};

struct ALIGN16 HlslPerObjectConstants
{
    ROW_MAJOR float4x4 World;
    ROW_MAJOR float4x4 WorldInvTranspose;
};

#ifdef __cplusplus
} // namespace HlslTypes
#endif
