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

    float svsmConstantBias; // SVSM only: compare bias toward the sun in world meters, no hardware bias in the software depth path

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

// ---- SVSM (shadowTechnique 1) ----

// One texel from the virtual shadow map through the page table, resident = false when the page
// has no physical backing (the caller falls back to a coarser clipmap)
float SVSMLoadDepth(uint clipmapIndex, int2 virtualTexel, uint pageTableSize, uint pageSize, uint poolPagesPerRow, out bool resident)
{
    int2 localPage = virtualTexel / int(pageSize);
    int2 anchorPageMin = int2(_svsmData[0].anchorPageMinX[clipmapIndex], _svsmData[0].anchorPageMinY[clipmapIndex]);
    int2 slot = WrapPageSlot(anchorPageMin + localPage, pageTableSize);

    uint entry = _svsmPageTable[PageEntryIndex(clipmapIndex, slot, pageTableSize)];

    // INVALID content may predate a Z-range or sun change, i.e. live in a different depth
    // mapping — never sample it, the coarser fallback is always in the current mapping
    resident = (entry & (SVSM_PAGE_RESIDENT | SVSM_PAGE_INVALID)) == SVSM_PAGE_RESIDENT;
    if (!resident)
    {
        return 0.0f;
    }

    uint physicalPage = GetPagePhysical(entry);
    uint2 physicalBase = uint2(physicalPage % poolPagesPerRow, physicalPage / poolPagesPerRow) * pageSize;
    uint2 pageTexel = uint2(virtualTexel - localPage * int(pageSize));

    return asfloat(_svsmPagePool[physicalBase + pageTexel]);
}

// Dynamic caster depth for the same texel, 0 when no dynamic page exists there. Non-residency is
// the normal state for almost every page and must never drive the coarser-clipmap fallback
float SVSMLoadDynamicDepth(uint clipmapIndex, int2 virtualTexel, uint pageTableSize, uint pageSize, out bool resident)
{
    int2 localPage = virtualTexel / int(pageSize);
    int2 anchorPageMin = int2(_svsmData[0].anchorPageMinX[clipmapIndex], _svsmData[0].anchorPageMinY[clipmapIndex]);
    int2 slot = WrapPageSlot(anchorPageMin + localPage, pageTableSize);

    uint entry = _svsmDynamicPageTable[PageEntryIndex(clipmapIndex, slot, pageTableSize)];
    resident = (entry & SVSM_PAGE_RESIDENT) != 0;
    if (!resident)
    {
        return 0.0f;
    }

    uint poolPagesPerRow = _svsmData[0].configDynamicPoolPagesPerRow;
    uint physicalPage = GetPagePhysical(entry);
    uint2 physicalBase = uint2(physicalPage % poolPagesPerRow, physicalPage / poolPagesPerRow) * pageSize;
    uint2 pageTexel = uint2(virtualTexel - localPage * int(pageSize));

    return asfloat(_svsmDynamicPagePool[physicalBase + pageTexel]);
}

// Software percentage-closer filtering over the sparse clipmaps: select the finest covering
// clipmap, walk to coarser ones when a touched page is not resident, 3x3 bilinear-weighted taps
// over a shared 4x4 texel block. Returns 1 (lit) when nothing is resident, the marking pass
// guarantees residency for everything the main view actually sees. Static and dynamic resolve
// their rings independently (see the dynamic block below) and combine via min
float GetSVSMShadowFactor(float3 worldPos, float3 worldNormal, ShadowSettings shadowSettings)
{
    uint numClipmaps = _svsmData[0].configNumClipmaps;
    float4 lightDirection = _svsmData[0].prevLightDirection;
    if (numClipmaps == 0 || lightDirection.w < 0.5f)
    {
        return 1.0f;
    }

    uint pageTableSize = _svsmData[0].configPageTableSize;
    uint pageSize = _svsmData[0].configPageSize;
    uint poolPagesPerRow = _svsmData[0].configPoolPagesPerRow;

    float3x3 lightRotation = BuildLightRotation(lightDirection.xyz);
    float3 relativePos = worldPos - _cameras[0].eyePosition.xyz; // Camera-relative, world-magnitude wrap math loses texels
    float2 lightSpaceRelCam = mul(relativePos, lightRotation).xy;

    // Same margin the marking pass used, so every page the filter can touch was marked. Floor ring
    // bias -1: the walk must pass through the ring the marker marked despite float divergence
    int selectedClipmap = SelectClipmap(lightSpaceRelCam, length(relativePos), _svsmData, numClipmaps, pageTableSize, pageSize, _svsmData[0].configMarkBorderTexels + 2.0f, _svsmData[0].configResolutionScale, -1);
    if (selectedClipmap < 0)
    {
        return 1.0f; // Outside the coarsest window, beyond all shadow data
    }

    float zRangeMin = _svsmData[0].zRangeMin;
    float zRangeMax = _svsmData[0].zRangeMax;
    float zRangeLength = max(zRangeMax - zRangeMin, 1.0f);

    // Ring-independent scalars hoisted out of the walks, structured-buffer loads don't reliably CSE
    float zHalf = zRangeLength * 0.5f;
    float camMinusZAnchor = _svsmData[0].configCamMinusZAnchor;
    float compareBias = shadowSettings.svsmConstantBias / zRangeLength;

    float staticFactor = 1.0f;
    for (uint k = uint(selectedClipmap); k < numClipmaps; k++)
    {
        float extent = _svsmData[0].extent[k];
        float texelWorld = extent / float(pageTableSize * pageSize);

        // Receiver pushed along its surface normal in clipmap texels, same acne control as the CSM path
        float3 lightSpacePos = mul(relativePos + worldNormal * (texelWorld * shadowSettings.normalOffset), lightRotation);
        float2 windowPos = lightSpacePos.xy + float2(_svsmData[0].camMinusWindowMinX[k], _svsmData[0].camMinusWindowMinY[k]);

        // Reversed Z like the cascades: 1 = sun side. lightSpacePos is camera-relative, the raster
        // maps absolute light-space Z, camMinusZAnchor reconciles the two with small quantities.
        // Positive bias moves the receiver toward the sun
        float compareDepth = (zHalf - (lightSpacePos.z + camMinusZAnchor)) / zRangeLength + compareBias;

        // 4x4 texel block shared by all 9 bilinear taps
        float2 texelPos = windowPos / texelWorld;
        float2 blockBaseF = floor(texelPos - 0.5f);
        int2 blockBase = int2(blockBaseF) - 1;
        float2 fracPart = texelPos - 0.5f - blockBaseF;

        // The block spans at most 2x2 pages: resolve their table entries once, the taps then only
        // pay pool loads. The containment margin keeps the whole block inside the window
        int2 anchorPageMin = int2(_svsmData[0].anchorPageMinX[k], _svsmData[0].anchorPageMinY[k]);
        int2 basePage = blockBase / int(pageSize);
        int2 baseRel = blockBase - basePage * int(pageSize);
        bool2 crossesPage = bool2(baseRel.x + 3 >= int(pageSize), baseRel.y + 3 >= int(pageSize));

        bool pageResident[2][2];
        uint2 pagePhysBase[2][2];

        [unroll]
        for (int py = 0; py < 2; py++)
        {
            [unroll]
            for (int px = 0; px < 2; px++)
            {
                int2 slot = WrapPageSlot(anchorPageMin + basePage + int2(px, py), pageTableSize);
                uint entry = _svsmPageTable[PageEntryIndex(k, slot, pageTableSize)];

                // INVALID content may live in a dead depth mapping, never sample it
                pageResident[py][px] = (entry & (SVSM_PAGE_RESIDENT | SVSM_PAGE_INVALID)) == SVSM_PAGE_RESIDENT;
                uint physicalPage = GetPagePhysical(entry);
                pagePhysBase[py][px] = uint2(physicalPage % poolPagesPerRow, physicalPage / poolPagesPerRow) * pageSize;
            }
        }

        bool allResident = pageResident[0][0]
            && (!crossesPage.x || pageResident[0][1])
            && (!crossesPage.y || pageResident[1][0])
            && (!(crossesPage.x && crossesPage.y) || pageResident[1][1]);
        if (!allResident)
        {
            continue; // Fall back to a coarser clipmap, worst case ends lit
        }

        float lit[4][4];

        [unroll]
        for (int y = 0; y < 4; y++)
        {
            [unroll]
            for (int x = 0; x < 4; x++)
            {
                int2 rel = baseRel + int2(x, y);
                int2 pageSel = int2(rel.x >= int(pageSize) ? 1 : 0, rel.y >= int(pageSize) ? 1 : 0);
                uint2 pageTexel = uint2(rel - pageSel * int(pageSize));
                float storedDepth = asfloat(_svsmPagePool[pagePhysBase[pageSel.y][pageSel.x] + pageTexel]);
                lit[y][x] = compareDepth > storedDepth ? 1.0f : 0.0f;
            }
        }

        float shadowFactor = 0.0f;

        [unroll]
        for (int ty = 0; ty < 3; ty++)
        {
            [unroll]
            for (int tx = 0; tx < 3; tx++)
            {
                float litX0 = lerp(lit[ty + 0][tx + 0], lit[ty + 0][tx + 1], fracPart.x);
                float litX1 = lerp(lit[ty + 1][tx + 0], lit[ty + 1][tx + 1], fracPart.x);
                shadowFactor += lerp(litX0, litX1, fracPart.y);
            }
        }
        shadowFactor /= 9.0f;

        // Fade to unshadowed over the outer 15% of the coarsest window, beyond it there is no shadow data
        if (k == numClipmaps - 1)
        {
            float halfExtent = extent * 0.5f;
            float centerDistance = max(abs(windowPos.x - halfExtent), abs(windowPos.y - halfExtent));
            float fadeStart = 0.85f * halfExtent;
            float fade = saturate((centerDistance - fadeStart) / (0.15f * halfExtent));
            shadowFactor = lerp(shadowFactor, 1.0f, fade);
        }

        staticFactor = shadowFactor;
        break;
    }

    // Dynamic casters resolve independently of the static ring: static residency is a long-lived
    // cache, dynamic pages exist only at rings marked THIS frame (and marking has per-frame noise
    // from animated-geometry pixels). Coupling the two flickers dynamic shadows whenever the
    // static resolve lands on a ring whose dynamic page came and went — so walk the dynamic
    // table on its own and combine. Depths share the global Z window, min() keeps the darker result
    float dynamicFactor = 1.0f;
    if (_svsmData[0].statsDynamicTotal != 0)
    {
        for (uint kd = uint(selectedClipmap); kd < numClipmaps; kd++)
        {
            if (_svsmData[0].statsDynamicLive[kd] == 0)
            {
                continue; // No dynamic pages anywhere in this ring this frame
            }

            float extent = _svsmData[0].extent[kd];
            float texelWorld = extent / float(pageTableSize * pageSize);

            float3 lightSpacePos = mul(relativePos + worldNormal * (texelWorld * shadowSettings.normalOffset), lightRotation);
            float2 windowPos = lightSpacePos.xy + float2(_svsmData[0].camMinusWindowMinX[kd], _svsmData[0].camMinusWindowMinY[kd]);
            float2 texelPos = windowPos / texelWorld;

            // Center-texel residency probe: DynamicMark pads the caster footprint by a full page,
            // far wider than the filter, so a non-resident center means no dynamic caster here
            bool centerResident;
            SVSMLoadDynamicDepth(kd, int2(floor(texelPos)), pageTableSize, pageSize, centerResident);
            if (!centerResident)
            {
                continue;
            }

            float compareDepth = (zHalf - (lightSpacePos.z + camMinusZAnchor)) / zRangeLength + compareBias;

            float2 blockBaseF = floor(texelPos - 0.5f);
            int2 blockBase = int2(blockBaseF) - 1;
            float2 fracPart = texelPos - 0.5f - blockBaseF;

            // Same 2x2 page resolve as the static filter. Non-resident pages read depth 0 = lit,
            // correct: no dynamic caster covers that texel
            uint dynamicPoolPagesPerRow = _svsmData[0].configDynamicPoolPagesPerRow;
            int2 anchorPageMin = int2(_svsmData[0].anchorPageMinX[kd], _svsmData[0].anchorPageMinY[kd]);
            int2 basePage = blockBase / int(pageSize);
            int2 baseRel = blockBase - basePage * int(pageSize);

            bool pageResident[2][2];
            uint2 pagePhysBase[2][2];

            [unroll]
            for (int py = 0; py < 2; py++)
            {
                [unroll]
                for (int px = 0; px < 2; px++)
                {
                    int2 slot = WrapPageSlot(anchorPageMin + basePage + int2(px, py), pageTableSize);
                    uint entry = _svsmDynamicPageTable[PageEntryIndex(kd, slot, pageTableSize)];
                    pageResident[py][px] = (entry & SVSM_PAGE_RESIDENT) != 0;
                    uint physicalPage = GetPagePhysical(entry);
                    pagePhysBase[py][px] = uint2(physicalPage % dynamicPoolPagesPerRow, physicalPage / dynamicPoolPagesPerRow) * pageSize;
                }
            }

            float lit[4][4];

            [unroll]
            for (int y = 0; y < 4; y++)
            {
                [unroll]
                for (int x = 0; x < 4; x++)
                {
                    int2 rel = baseRel + int2(x, y);
                    int2 pageSel = int2(rel.x >= int(pageSize) ? 1 : 0, rel.y >= int(pageSize) ? 1 : 0);
                    uint2 pageTexel = uint2(rel - pageSel * int(pageSize));
                    float storedDepth = pageResident[pageSel.y][pageSel.x] ? asfloat(_svsmDynamicPagePool[pagePhysBase[pageSel.y][pageSel.x] + pageTexel]) : 0.0f;
                    lit[y][x] = compareDepth > storedDepth ? 1.0f : 0.0f;
                }
            }

            float shadowFactor = 0.0f;

            [unroll]
            for (int ty = 0; ty < 3; ty++)
            {
                [unroll]
                for (int tx = 0; tx < 3; tx++)
                {
                    float litX0 = lerp(lit[ty + 0][tx + 0], lit[ty + 0][tx + 1], fracPart.x);
                    float litX1 = lerp(lit[ty + 1][tx + 0], lit[ty + 1][tx + 1], fracPart.x);
                    shadowFactor += lerp(litX0, litX1, fracPart.y);
                }
            }
            dynamicFactor = shadowFactor / 9.0f;
            break;
        }
    }

    return min(staticFactor, dynamicFactor);
}

// Diagnostic view of the SVSM compare: single center-texel sample, classification colors.
// Dark purple = no SVSM state, blue = outside coverage, magenta = page never resident,
// red = shadowed (brightness = margin up to 8m, green channel rises toward 512m -> yellow),
// green = lit (brightness = margin up to 8m, blue channel rises toward 512m -> cyan)
float3 GetSVSMDebugColor(float3 worldPos, float3 worldNormal, float normalOffset, float constantBias)
{
    uint numClipmaps = _svsmData[0].configNumClipmaps;
    float4 lightDirection = _svsmData[0].prevLightDirection;
    if (numClipmaps == 0 || lightDirection.w < 0.5f)
    {
        return float3(0.1f, 0.0f, 0.3f);
    }

    uint pageTableSize = _svsmData[0].configPageTableSize;
    uint pageSize = _svsmData[0].configPageSize;
    uint poolPagesPerRow = _svsmData[0].configPoolPagesPerRow;

    float3x3 lightRotation = BuildLightRotation(lightDirection.xyz);
    float3 relativePos = worldPos - _cameras[0].eyePosition.xyz;
    float2 lightSpaceRelCam = mul(relativePos, lightRotation).xy;

    int selectedClipmap = SelectClipmap(lightSpaceRelCam, length(relativePos), _svsmData, numClipmaps, pageTableSize, pageSize, _svsmData[0].configMarkBorderTexels + 2.0f, _svsmData[0].configResolutionScale, -1);
    if (selectedClipmap < 0)
    {
        return float3(0.0f, 0.3f, 1.0f);
    }

    float zRangeMin = _svsmData[0].zRangeMin;
    float zRangeMax = _svsmData[0].zRangeMax;
    float zRangeLength = max(zRangeMax - zRangeMin, 1.0f);

    // Same independent static/dynamic resolves as GetSVSMShadowFactor, single center tap each.
    // The classification compares whichever depth is more occluding against its own receiver
    float deltaMeters = 0.0f;
    bool anyResident = false;

    for (uint k = uint(selectedClipmap); k < numClipmaps; k++)
    {
        float extent = _svsmData[0].extent[k];
        float texelWorld = extent / float(pageTableSize * pageSize);

        float3 lightSpacePos = mul(relativePos + worldNormal * (texelWorld * normalOffset), lightRotation);
        float2 windowPos = lightSpacePos.xy + float2(_svsmData[0].camMinusWindowMinX[k], _svsmData[0].camMinusWindowMinY[k]);

        float zHalf = zRangeLength * 0.5f;
        float receiverDepth = (zHalf - (lightSpacePos.z + _svsmData[0].configCamMinusZAnchor)) / zRangeLength;
        float compareDepth = receiverDepth + constantBias / zRangeLength;

        bool resident;
        float storedDepth = SVSMLoadDepth(k, int2(windowPos / texelWorld), pageTableSize, pageSize, poolPagesPerRow, resident);
        if (!resident)
        {
            continue;
        }

        deltaMeters = (storedDepth - compareDepth) * zRangeLength;
        anyResident = true;
        break;
    }

    if (_svsmData[0].statsDynamicTotal != 0)
    {
        for (uint kd = uint(selectedClipmap); kd < numClipmaps; kd++)
        {
            if (_svsmData[0].statsDynamicLive[kd] == 0)
            {
                continue;
            }

            float extent = _svsmData[0].extent[kd];
            float texelWorld = extent / float(pageTableSize * pageSize);

            float3 lightSpacePos = mul(relativePos + worldNormal * (texelWorld * normalOffset), lightRotation);
            float2 windowPos = lightSpacePos.xy + float2(_svsmData[0].camMinusWindowMinX[kd], _svsmData[0].camMinusWindowMinY[kd]);

            bool resident;
            float dynamicDepth = SVSMLoadDynamicDepth(kd, int2(windowPos / texelWorld), pageTableSize, pageSize, resident);
            if (!resident)
            {
                continue;
            }

            float zHalf = zRangeLength * 0.5f;
            float receiverDepth = (zHalf - (lightSpacePos.z + _svsmData[0].configCamMinusZAnchor)) / zRangeLength;
            float compareDepth = receiverDepth + constantBias / zRangeLength;

            float dynamicDelta = (dynamicDepth - compareDepth) * zRangeLength;
            deltaMeters = anyResident ? max(deltaMeters, dynamicDelta) : dynamicDelta;
            anyResident = true;
            break;
        }
    }

    if (!anyResident)
    {
        return float3(1.0f, 0.0f, 1.0f);
    }

    if (deltaMeters > 0.0f)
    {
        return float3(saturate(deltaMeters / 8.0f), saturate(deltaMeters / 512.0f), 0.0f); // Shadowed
    }
    return float3(0.0f, saturate(-deltaMeters / 8.0f), saturate(-deltaMeters / 512.0f)); // Lit
}

#endif // SHADOWS_INCLUDED