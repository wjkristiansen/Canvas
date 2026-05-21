// Composition pixel shader - reads G-buffers and performs deferred lighting.
// Pixels with no geometry are filled by the scene background: either the
// SolidColor or a sample from the bound skybox cubemap(s).  The composite
// owns every output pixel, so the render-target clear color is irrelevant.
// Outputs final lit color to the back buffer (SV_Target0).

// G-buffer textures (t0-t2), optional skybox cubes (t3 = A, t4 = B), an
// optional stars cube (t5), and an optional moon sprite (t6).  Unbound
// slots hold null SRVs; the shader's HasCubemap / HasStars / HasMoon
// flags select active branches.  The sun has no texture -- it's a
// procedural disc in the composite, driven by SunDirAndCosRadius +
// SunColorAndIntensity.
Texture2D   GBufferNormals      : register(t0);
Texture2D   GBufferDiffuseColor : register(t1);
Texture2D   GBufferWorldPos     : register(t2);
TextureCube SkyCubeA            : register(t3);
TextureCube SkyCubeB            : register(t4);
TextureCube StarsCube           : register(t5);
Texture2D   MoonTexture         : register(t6);

// s0: point/clamp for exact G-buffer texel fetch.
// s1: linear/wrap for cubemap sky / stars sampling.
// s2: linear/clamp for the moon billboard quad.
SamplerState PointSampler         : register(s0);
SamplerState LinearWrapSampler    : register(s1);
SamplerState LinearClampSampler   : register(s2);

#include "HlslTypes.h"

ConstantBuffer<HlslPerFrameConstants> PerFrame : register(b0);

static const float PI = 3.14159265358979323846;
static const float INV_PI = 1.0 / PI;
static const float INV_FOUR_PI = 1.0 / (4.0 * PI);

struct FSInput
{
    float2 TexCoord : TEXCOORD0;
    float4 Position : SV_Position;
};

float ComputeAttenuation(float4 attenuationAndRange, float dist, float distSq)
{
    float denom = attenuationAndRange.x +
                  attenuationAndRange.y * dist +
                  attenuationAndRange.z * distSq;
    return (denom > 1e-6) ? rcp(denom) : 1.0;
}

//----------------------------------------------------------------------------
// Reconstruct a world-space view ray for the current screen UV using the
// per-frame camera basis + projection params (no matrix inverse required).
//----------------------------------------------------------------------------
float3 ScreenUVToWorldDir(float2 uv)
{
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;
    float tanHalfFovY = PerFrame.CamForwardAndTanHalfFov.w;
    float aspect      = PerFrame.CamRightAndAspect.w;
    float3 fwd        = PerFrame.CamForwardAndTanHalfFov.xyz;
    float3 right      = PerFrame.CamRightAndAspect.xyz;
    float3 up         = PerFrame.CamUp.xyz;
    // Row-vector convention: row 1 = "left", so the visible right of screen
    // is -row[1]. Mirror the x ray accordingly.
    float3 dir = fwd
               + (-right) * (ndc.x * tanHalfFovY * aspect)
               + up       * (ndc.y * tanHalfFovY);
    return normalize(dir);
}

//----------------------------------------------------------------------------
// Rotate a vector by the conjugate of a unit quaternion.  Used to map a
// world-space view direction into the cubemap's authoring basis before
// sampling, so apps can rotate the sky (e.g. for diurnal alignment) without
// re-uploading textures.
//----------------------------------------------------------------------------
float3 RotateByQuatConjugate(float4 q, float3 v)
{
    // Conjugate is (-q.xyz, q.w); rotation formula:
    //   v' = v + 2 * cross(-q.xyz, cross(-q.xyz, v) + q.w * v)
    float3 qv = -q.xyz;
    float3 t  = cross(qv, v) + q.w * v;
    return v + 2.0 * cross(qv, t);
}

//----------------------------------------------------------------------------
// Scene background sample for pixels with no geometry.  Selected by the
// SkyHasCubemap flag in per-frame constants; intensity / tint / orientation
// / crossfade are all CB-driven.
//----------------------------------------------------------------------------
float3 SampleBackground(float3 viewDir)
{
    if (PerFrame.SkyHasCubemap == 0)
        return PerFrame.SkySolidColor.rgb;

    float3 sampleDir = RotateByQuatConjugate(PerFrame.SkyOrientationQuat, viewDir);
    float3 sky = SkyCubeA.SampleLevel(LinearWrapSampler, sampleDir, 0).rgb;
    if (PerFrame.SkyHasCubemapB != 0)
    {
        float3 skyB = SkyCubeB.SampleLevel(LinearWrapSampler, sampleDir, 0).rgb;
        sky = lerp(sky, skyB, saturate(PerFrame.SkyBlendFactor));
    }
    return sky * PerFrame.SkyIntensity;
}

//----------------------------------------------------------------------------
// Stars overlay: rotating RGBA cubemap sampled with the conjugate-rotated
// view ray.  Returns an additive contribution (rgb * alpha * intensity),
// clipped below the horizon.  Returns zero when no stars cube is bound.
//----------------------------------------------------------------------------
float3 SampleStars(float3 viewDir)
{
    if (PerFrame.HasStars == 0)
        return float3(0.0, 0.0, 0.0);
    if (viewDir.z < 0.0)
        return float3(0.0, 0.0, 0.0);

    float3 sampleDir = RotateByQuatConjugate(PerFrame.StarsOrientationQuat, viewDir);
    float4 stars = StarsCube.SampleLevel(LinearWrapSampler, sampleDir, 0);
    return stars.rgb * stars.a * PerFrame.StarsIntensity;
}

//----------------------------------------------------------------------------
// Sun: procedural soft-edged disc rendered directly from SunDirection and
// SunAngularRadius.  Inside the cone we fade with a smooth disc + halo
// falloff so the body has a bright core and a soft atmospheric edge.
// Disabled when SunColorAndIntensity.a == 0; clipped below the horizon.
//----------------------------------------------------------------------------
float3 SampleProceduralSun(float3 viewDir)
{
    float intensity = PerFrame.SunColorAndIntensity.a;
    if (intensity <= 0.0)
        return float3(0.0, 0.0, 0.0);
    float3 sunDir = PerFrame.SunDirAndCosRadius.xyz;
    if (sunDir.z < 0.0)
        return float3(0.0, 0.0, 0.0);

    float cosRadius = PerFrame.SunDirAndCosRadius.w;
    float cosAngle  = dot(viewDir, sunDir);
    if (cosAngle < cosRadius)
        return float3(0.0, 0.0, 0.0);

    // Normalized distance from disc center in [0, 1] (0 = centre, 1 = edge).
    float sinRadius = sqrt(saturate(1.0 - cosRadius * cosRadius));
    float radial    = sqrt(saturate(1.0 - cosAngle * cosAngle)) / max(sinRadius, 1e-6);

    // Bright core out to ~0.6, then smooth falloff to the edge.
    float core = 1.0 - smoothstep(0.6, 1.0, radial);
    return PerFrame.SunColorAndIntensity.rgb * (intensity * core);
}

//----------------------------------------------------------------------------
// Moon: textured billboard sampled inside an angular disc around
// MoonDirection.  Texture's alpha modulates blending; MoonColor.rgb
// tints; MoonColor.a is the overall multiplier (0 disables).  Clipped
// below the horizon.  Returns straight-alpha rgba so the caller can
// alpha-over.
//----------------------------------------------------------------------------
float4 SampleMoonBillboard(float3 viewDir)
{
    if (PerFrame.HasMoon == 0)
        return float4(0.0, 0.0, 0.0, 0.0);
    float intensity = PerFrame.MoonColorAndIntensity.a;
    if (intensity <= 0.0)
        return float4(0.0, 0.0, 0.0, 0.0);

    float3 moonDir = PerFrame.MoonDirAndCosRadius.xyz;
    if (moonDir.z < 0.0)
        return float4(0.0, 0.0, 0.0, 0.0);

    float cosRadius = PerFrame.MoonDirAndCosRadius.w;
    float cosAngle  = dot(viewDir, moonDir);
    if (cosAngle < cosRadius)
        return float4(0.0, 0.0, 0.0, 0.0);

    // Local 2D basis perpendicular to moonDir; project the in-cone offset
    // and map to texture UV.  Y flipped so the texture's top row maps to
    // the visual top of the disc on screen.
    float3 ref = (abs(moonDir.z) > 0.9) ? float3(0.0, 1.0, 0.0)
                                        : float3(0.0, 0.0, 1.0);
    float3 right = normalize(cross(ref, moonDir));
    float3 up    = cross(moonDir, right);

    float3 offset = viewDir - cosAngle * moonDir;
    float  sinRadius = sqrt(saturate(1.0 - cosRadius * cosRadius));
    float2 discUV    = float2(dot(offset, right), dot(offset, up)) / max(sinRadius, 1e-6);
    float2 uv        = float2(discUV.x * 0.5 + 0.5, 0.5 - discUV.y * 0.5);

    float4 tex = MoonTexture.SampleLevel(LinearClampSampler, uv, 0);
    return float4(tex.rgb * PerFrame.MoonColorAndIntensity.rgb,
                  tex.a   * intensity);
}

float4 PSComposite(FSInput input) : SV_Target0
{
    // Sample G-buffers
    float4 normalSample  = GBufferNormals.Sample(PointSampler, input.TexCoord);
    float4 diffuseSample = GBufferDiffuseColor.Sample(PointSampler, input.TexCoord);
    float4 worldPosSample = GBufferWorldPos.Sample(PointSampler, input.TexCoord);

    // No geometry at this pixel: emit the scene background (solid color
    // or skybox) plus the stars + procedural sun + moon billboard, then
    // run the composite's exposure curve so the result tonemaps
    // consistently with the lit scene.  Stars and sun are additive over
    // the sky; the moon is alpha-blended on top so its rgb is independent
    // of the sky brightness behind it.
    if (normalSample.a == 0.0)
    {
        float3 viewDir   = ScreenUVToWorldDir(input.TexCoord);
        float3 skyColor  = SampleBackground(viewDir);
        skyColor += SampleStars(viewDir);
        skyColor += SampleProceduralSun(viewDir);
        float4 moonRgba  = SampleMoonBillboard(viewDir);
        skyColor         = lerp(skyColor, moonRgba.rgb, saturate(moonRgba.a));
        skyColor = 1.0 - exp(-skyColor * PerFrame.Exposure);
        return float4(skyColor, 1.0);
    }

    // Decode world-space normal from [0,1] -> [-1,1]
    float3 N = normalize(normalSample.rgb * 2.0 - 1.0);
    float3 albedo = diffuseSample.rgb;
    float3 P = worldPosSample.xyz;

    // Accumulate lighting from all active lights
    float3 totalLight = float3(0.0, 0.0, 0.0);

    for (uint i = 0; i < PerFrame.LightCount && i < MAX_LIGHTS_PER_REGION; ++i)
    {
        HlslLight light = PerFrame.Lights[i];

        if (light.Type == LIGHT_DIRECTIONAL)
        {
            // Directional lights store their forward/emission direction.
            // Lambert needs the vector from surface toward the light source.
            float3 L = normalize(-light.DirectionOrPosition.xyz);
            float NdotL = saturate(dot(N, L));
            totalLight += light.Color.rgb * NdotL;
        }
        else if (light.Type == LIGHT_POINT)
        {
            float3 toLight = light.DirectionOrPosition.xyz - P;
            float distSq = dot(toLight, toLight);
            if (distSq <= 1e-8)
                continue;

            float dist = sqrt(distSq);

            // Cutoff distance is precomputed on the CPU and stored in .w
            float cutoffDist = light.AttenuationAndRange.w;
            if (cutoffDist <= 0.0 || dist > cutoffDist)
                continue;

            float3 L = toLight / dist;
            float NdotL = saturate(dot(N, L));
            float attenuation = ComputeAttenuation(light.AttenuationAndRange, dist, distSq);

            // Blender/FBX point light power is flux-like. Convert isotropic flux to
            // directional intensity by dividing by 4*pi before distance attenuation.
            attenuation *= INV_FOUR_PI;

            totalLight += light.Color.rgb * (NdotL * attenuation);
        }
        else if (light.Type == LIGHT_SPOT)
        {
            float3 toLight = light.DirectionOrPosition.xyz - P;
            float distSq = dot(toLight, toLight);
            if (distSq <= 1e-8)
                continue;

            float dist = sqrt(distSq);

            // Cutoff distance is precomputed on the CPU and stored in .w
            float cutoffDist = light.AttenuationAndRange.w;
            if (cutoffDist <= 0.0 || dist > cutoffDist)
                continue;

            float3 L = toLight / dist;
            float NdotL = saturate(dot(N, L));
            if (NdotL <= 0.0)
                continue;

            float3 lightForward = normalize(light.DirectionAndSpot.xyz);
            float cosTheta = dot(-L, lightForward);
            float outerCos = light.DirectionAndSpot.w;
            float innerCos = light.Color.w;
            float cone = smoothstep(outerCos, max(innerCos, outerCos + 1e-4), cosTheta);
            if (cone <= 0.0)
                continue;

            float attenuation = ComputeAttenuation(light.AttenuationAndRange, dist, distSq);

            // Treat spot light intensity as total cone flux. Convert to directional
            // intensity using cone solid angle: Omega = 2*pi*(1-cos(theta_outer)).
            float coneSolidAngle = max(2.0 * PI * (1.0 - outerCos), 1e-4);
            attenuation *= rcp(coneSolidAngle);

            totalLight += light.Color.rgb * (NdotL * cone * attenuation);
        }
        else if (light.Type == LIGHT_AMBIENT)
        {
            totalLight += light.Color.rgb;
        }
    }

    // Lambertian diffuse BRDF term.
    float3 color = albedo * totalLight * INV_PI;

    // Apply camera exposure multiplier, then a simple Reinhard-style
    // exponential roll-off so highlights don't clip immediately. A future
    // tonemapping pass (ACES/AgX/Filmic) should replace the roll-off.
    color = 1.0 - exp(-color * PerFrame.Exposure);
    return float4(color, 1.0);
}
