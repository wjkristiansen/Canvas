// Composition pixel shader - reads G-buffers and performs deferred lighting.
// On pixels with no geometry (empty G-buffer) emits an analytic sky color
// reconstructed from the per-frame camera basis + lights, so the day/night
// cycle drives the visible sky tint with zero extra binding cost.
// Outputs final lit color to the back buffer (SV_Target0).

// G-buffer textures bound via descriptor table
Texture2D GBufferNormals      : register(t0);
Texture2D GBufferDiffuseColor : register(t1);
Texture2D GBufferWorldPos     : register(t2);

// Point sampler for exact texel fetch (G-buffer is 1:1 with screen)
SamplerState PointSampler : register(s0);

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
// Analytic sky for pixels with no geometry. Two contributions:
//   (1) A vertical gradient zenith -> horizon -> ground (cosine-weighted by
//       directional-light brightness so the sky goes dark at night).
//   (2) A small sun/moon disc where the view ray aligns with the inverse
//       direction of any directional light.
//----------------------------------------------------------------------------
float3 SampleSky(float3 viewDir)
{
    float verticalT = saturate(viewDir.z * 0.5 + 0.5);  // 0 nadir -> 0.5 horizon -> 1 zenith

    // Base palette is fixed; day/night brightness comes from the directional
    // lights summed below. The constants are chosen to read as "a sky" even
    // when there's no light at all.
    float3 zenithBase  = float3(0.05, 0.08, 0.18);
    float3 horizonBase = float3(0.18, 0.20, 0.25);
    float3 groundBase  = float3(0.02, 0.02, 0.03);

    float3 sky;
    if (viewDir.z >= 0.0)
        sky = lerp(horizonBase, zenithBase, smoothstep(0.0, 1.0, viewDir.z));
    else
        sky = lerp(horizonBase, groundBase, smoothstep(0.0, 1.0, -viewDir.z));

    // Per-light contribution: warm tint near each directional light's
    // anti-direction, sun-disc highlight at the brightest end of the cone.
    [loop]
    for (uint i = 0; i < PerFrame.LightCount && i < MAX_LIGHTS_PER_REGION; ++i)
    {
        HlslLight light = PerFrame.Lights[i];
        if (light.Type != LIGHT_DIRECTIONAL)
            continue;

        // DirectionOrPosition is the direction photons travel; the apparent
        // position of the source is the opposite.
        float3 sourceDir = normalize(-light.DirectionOrPosition.xyz);
        float c = saturate(dot(viewDir, sourceDir));

        // Atmospheric scattering proxy: smooth halo with a steep core.
        float halo = pow(c, 8.0)  * 0.4;
        float disc = pow(c, 512.0) * 4.0;
        float3 srcColor = light.Color.rgb;
        float srcMag = max(max(srcColor.r, srcColor.g), srcColor.b);

        // Modulate base sky brightness by directional-light strength so the
        // sky darkens at night naturally.
        sky *= (1.0 + srcMag * 0.4);

        // Add halo + disc, clipped to the upper hemisphere so the sun/moon
        // don't punch through ground pixels in the analytic sky.
        if (viewDir.z > -0.1)
            sky += srcColor * (halo + disc);
    }

    return sky;
}

float4 PSComposite(FSInput input) : SV_Target0
{
    // Sample G-buffers
    float4 normalSample  = GBufferNormals.Sample(PointSampler, input.TexCoord);
    float4 diffuseSample = GBufferDiffuseColor.Sample(PointSampler, input.TexCoord);
    float4 worldPosSample = GBufferWorldPos.Sample(PointSampler, input.TexCoord);

    // No geometry at this pixel: emit analytic sky color, then run the
    // composite's exposure curve on it so it tonemaps consistently with the
    // lit terrain.
    if (normalSample.a == 0.0)
    {
        float3 viewDir = ScreenUVToWorldDir(input.TexCoord);
        float3 skyColor = SampleSky(viewDir);
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
