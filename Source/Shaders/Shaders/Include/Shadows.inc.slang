#ifndef SHADOWS_INCLUDED
#define SHADOWS_INCLUDED

#include "DescriptorSet/Global.inc.slang"
#include "DescriptorSet/Light.inc.slang"

struct ShadowSettings
{
    bool enableShadows;
    float filterSize;
    float penumbraFilterSize;
    float strength; // Driven by sun elevation, fades shadows out around dawn/dusk
    float normalOffset; // Receiver offset along the surface normal in cascade texels

    uint cascadeIndex; // Filled in by ApplyLighting
};

float TextureProj(float4 P, float2 offset, uint shadowCascadeIndex)
{
    float shadow = 1.0f;

    float4 shadowCoord = P / P.w;
    shadowCoord.xy = shadowCoord.xy * 0.5f + 0.5f;

    if (shadowCoord.z > -1.0f && shadowCoord.z < 1.0f)
    {
        float3 sc = float3(float2(shadowCoord.x, 1.0f - shadowCoord.y) + offset, shadowCoord.z);
        //shadow = _shadowRT.SampleLevel(_shadowSampler, sc.xy, sc.z);
        shadow = _shadowCascadeRTs[shadowCascadeIndex].SampleCmpLevelZero(_shadowCmpSampler, sc.xy, sc.z);
    }
    return shadow;
}

// 9 imad (+ 6 iops with final shuffle)
uint3 PCG3DHash(uint3 v)
{
    v = v * 1664525u + 1013904223u;

    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;

    v ^= v >> 16u;

    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;

    return v;
}

// Percentage Closer Filtering
float FilterPCF(float2 screenUV, float4 shadowCoord, ShadowSettings shadowSettings)
{
    float2 texDim;
    _shadowCascadeRTs[0].GetDimensions(texDim.x, texDim.y);

    const float scale = 0.5f;
    float dx = scale * (1.0f / texDim.x);
    float dy = scale * (1.0f / texDim.y);

    // Calculate a random per-pixel rotation for the filtering
    uint2 pixelPos = uint2(screenUV * texDim);
    uint3 hash = PCG3DHash(uint3(pixelPos, pixelPos.x ^ pixelPos.y));
    float2 rand = float2(hash.xy & 0xFFFFu) / 65535.0f;

    float2 dirA = normalize(rand * 2.0f - 1.0f);
    float2 dirB = float2(-dirA.y, dirA.x);

    dirA *= dx;
    dirB *= dy;

    // Add together all the filter taps
    float shadowFactor = 0.0f;
    const int range = 2;
    int count = 0;

    [unroll]
    for (int x = -range; x <= range; x++)
    {
        [unroll]
        for (int y = -range; y <= range; y++)
        {
            shadowFactor += TextureProj(shadowCoord, dirA * x + dirB * y, shadowSettings.cascadeIndex);
            count++;
        }
    }

    return shadowFactor / float(count);
}


// Percentage Closer Soft Shadows, credits: https://www.gamedev.net/tutorials/programming/graphics/contact-hardening-soft-shadows-made-fast-r4906/
float InterleavedGradientNoise(float2 screenPos)
{
    float3 magic = float3(0.06711056f, 0.00583715f, 52.9829189f);
    return frac(magic.z * frac(dot(screenPos, magic.xy)));
}

float2 VogelDiskSample(int sampleIndex, int samplesCount, float phi)
{
    float goldenAngle = 2.4f;

    float r = sqrt(sampleIndex + 0.5f) / sqrt(samplesCount);
    float theta = sampleIndex * goldenAngle + phi;

    float sine, cosine;
    sincos(theta, sine, cosine);

    return float2(r * cosine, r * sine);
}

// This should be tuned for the visuals we want
float AvgBlockersDepthToPenumbra(float shadowMapViewZ, float avgBlockersDepth)
{
    // Reversed Z, greater depth = closer to the light, so blocker - receiver is the occlusion gap
    float penumbra = (avgBlockersDepth - shadowMapViewZ) / max(1.0f - avgBlockersDepth, 1e-4f);
    penumbra *= penumbra;
    return saturate(80.0f * penumbra);
}

float Penumbra(float gradientNoise, float2 shadowMapUV, float shadowMapViewZ, float penumbraFilterSize, int samplesCount, uint shadowCascadeIndex)
{
    float avgBlockersDepth = 0.0f;
    float blockersCount = 0.0f;

    for (int i = 0; i < samplesCount; i++)
    {
        float2 sampleUV = VogelDiskSample(i, samplesCount, gradientNoise);
        sampleUV = shadowMapUV + penumbraFilterSize * sampleUV;

        float sampleDepth = _shadowCascadeRTs[shadowCascadeIndex].SampleLevel(_shadowPointClampSampler, sampleUV, 0).x;

        // Reversed Z, blockers are closer to the light than the receiver and thus have greater depth
        if (sampleDepth > shadowMapViewZ)
        {
            avgBlockersDepth += sampleDepth;
            blockersCount += 1.0f;
        }
    }

    if (blockersCount > 0.0f)
    {
        avgBlockersDepth /= blockersCount;
        return AvgBlockersDepthToPenumbra(shadowMapViewZ, avgBlockersDepth);
    }
    else
    {
        return 0.0f;
    }
}

float FilterPCSS(float2 screenUV, float4 P, ShadowSettings shadowSettings)
{
    float2 texDim;
    _shadowCascadeRTs[0].GetDimensions(texDim.x, texDim.y);

    float4 shadowCoord = P / P.w;
    shadowCoord.xy = shadowCoord.xy * 0.5f + 0.5f;
    shadowCoord.y = 1.0f - shadowCoord.y; // Same Y flip as TextureProj, keeps all UVs in [0, 1]

    const float tau = 6.28318;
    float gradientNoise = tau * InterleavedGradientNoise(screenUV * texDim);

    float shadow = 0.0f;
    {
        float penumbra = 1.0f - Penumbra(gradientNoise, shadowCoord.xy, shadowCoord.z, shadowSettings.penumbraFilterSize, 16, shadowSettings.cascadeIndex);

        for (int i = 0; i < 16; i++)
        {
            float2 sampleUV = VogelDiskSample(i, 16, gradientNoise);
            sampleUV = shadowCoord.xy + sampleUV * penumbra * shadowSettings.filterSize;

            shadow += _shadowCascadeRTs[shadowSettings.cascadeIndex].SampleCmpLevelZero(_shadowCmpSampler, sampleUV, shadowCoord.z);
        }
    }
    shadow /= 16.0f;

    return shadow;
}

#define SHADOW_FILTER_MODE_OFF     0
#define SHADOW_FILTER_MODE_PCF     1 // Percentage Closer Filtering
#define SHADOW_FILTER_MODE_PCSS    2 // Percentage Closer Soft Shadows

#ifndef SHADOW_FILTER_MODE
#define SHADOW_FILTER_MODE SHADOW_FILTER_MODE_OFF
#endif

float GetShadowFactor(float2 screenUV, float4 shadowCoord, ShadowSettings shadowSettings)
{
#if SHADOW_FILTER_MODE == SHADOW_FILTER_MODE_PCF
    return lerp(0.0f, 1.0f, FilterPCF(screenUV, shadowCoord, shadowSettings));
#elif SHADOW_FILTER_MODE == SHADOW_FILTER_MODE_PCSS
    return saturate(FilterPCSS(screenUV, shadowCoord, shadowSettings));
#else
    return lerp(0.0f, 1.0f, TextureProj(shadowCoord, float2(0, 0), shadowSettings.cascadeIndex));
#endif
}

// Returns a 0-based cascade index, clamped to the last cascade for depths beyond its split
uint GetShadowCascadeIndexFromDepth(float depth, uint numCascades)
{
    uint cascadeIndex = 0;
    for (uint i = 1; i < numCascades; i++)
    {
        // _cameras[i].eyePosition.w holds the split depth of cascade i - 1
        if (depth > _cameras[i].eyePosition.w)
        {
            cascadeIndex = i;
        }
    }
    return cascadeIndex;
}
#endif // SHADOWS_INCLUDED