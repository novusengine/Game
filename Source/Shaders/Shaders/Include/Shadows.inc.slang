#ifndef SHADOWS_INCLUDED
#define SHADOWS_INCLUDED

#include "DescriptorSet/Global.inc.slang"
#include "DescriptorSet/Light.inc.slang"

struct ShadowSettings
{
    bool enableShadows;
    float strength; // Driven by sun elevation, fades shadows out around dawn/dusk
    float normalOffset; // Receiver offset along the surface normal in shadow texels

    float svsmConstantBias; // Compare bias toward the sun in world meters, no hardware bias in the software depth path
};

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

// Residency alone: one page-table read, no pool load — for probes that only need to know whether
// a dynamic page exists at the texel
bool SVSMIsDynamicPageResident(uint clipmapIndex, int2 virtualTexel, uint pageTableSize, uint pageSize)
{
    int2 localPage = virtualTexel / int(pageSize);
    int2 anchorPageMin = int2(_svsmData[0].anchorPageMinX[clipmapIndex], _svsmData[0].anchorPageMinY[clipmapIndex]);
    int2 slot = WrapPageSlot(anchorPageMin + localPage, pageTableSize);

    uint entry = _svsmDynamicPageTable[PageEntryIndex(clipmapIndex, slot, pageTableSize)];
    return (entry & SVSM_PAGE_RESIDENT) != 0;
}

// One ring of the SVSM PCF filter: the 4x4 texel block shared by all 9 bilinear taps spans at
// most 2x2 pages, so their table entries resolve once and the taps only pay pool loads. One
// implementation for both the static and dynamic halves of GetSVSMShadowFactor, parameterized on
// the (table, pool, pool row count) pair and the missing-page policy:
// - missingPagesReadLit = false (static cache): INVALID content may live in a dead depth mapping
//   and counts as missing; any missing touched page aborts the ring (returns false, the caller
//   falls back to a coarser clipmap, worst case ends lit).
// - missingPagesReadLit = true (dynamic pool): non-resident pages read depth 0 = lit — correct,
//   no dynamic caster covers that texel — and the ring always resolves.
// The callers' containment margin keeps the whole block inside the window
bool SVSMFilterRing(uint clipmapIndex, float2 texelPos, float compareDepth, uint pageTableSize, uint pageSize, uint poolPagesPerRow, bool missingPagesReadLit, StructuredBuffer<uint> pageTable, Texture2D<uint> pagePool, out float shadowFactor)
{
    shadowFactor = 1.0f;

    float2 blockBaseF = floor(texelPos - 0.5f);
    int2 blockBase = int2(blockBaseF) - 1;
    float2 fracPart = texelPos - 0.5f - blockBaseF;

    int2 anchorPageMin = int2(_svsmData[0].anchorPageMinX[clipmapIndex], _svsmData[0].anchorPageMinY[clipmapIndex]);
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
            uint entry = pageTable[PageEntryIndex(clipmapIndex, slot, pageTableSize)];

            pageResident[py][px] = missingPagesReadLit ? (entry & SVSM_PAGE_RESIDENT) != 0
                                                       : (entry & (SVSM_PAGE_RESIDENT | SVSM_PAGE_INVALID)) == SVSM_PAGE_RESIDENT;
            uint physicalPage = GetPagePhysical(entry);
            pagePhysBase[py][px] = uint2(physicalPage % poolPagesPerRow, physicalPage / poolPagesPerRow) * pageSize;
        }
    }

    if (!missingPagesReadLit)
    {
        bool2 crossesPage = bool2(baseRel.x + 3 >= int(pageSize), baseRel.y + 3 >= int(pageSize));
        bool allResident = pageResident[0][0]
            && (!crossesPage.x || pageResident[0][1])
            && (!crossesPage.y || pageResident[1][0])
            && (!(crossesPage.x && crossesPage.y) || pageResident[1][1]);
        if (!allResident)
        {
            return false;
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
            float storedDepth = pageResident[pageSel.y][pageSel.x] ? asfloat(pagePool[pagePhysBase[pageSel.y][pageSel.x] + pageTexel]) : 0.0f;
            lit[y][x] = compareDepth > storedDepth ? 1.0f : 0.0f;
        }
    }

    float factor = 0.0f;

    [unroll]
    for (int ty = 0; ty < 3; ty++)
    {
        [unroll]
        for (int tx = 0; tx < 3; tx++)
        {
            float litX0 = lerp(lit[ty + 0][tx + 0], lit[ty + 0][tx + 1], fracPart.x);
            float litX1 = lerp(lit[ty + 1][tx + 0], lit[ty + 1][tx + 1], fracPart.x);
            factor += lerp(litX0, litX1, fracPart.y);
        }
    }
    shadowFactor = factor / 9.0f;
    return true;
}

// Software percentage-closer filtering over the sparse clipmaps: select the finest covering
// clipmap, walk to coarser ones when a touched page is not resident, 3x3 bilinear-weighted taps
// over a shared 4x4 texel block (SVSMFilterRing). Returns 1 (lit) when nothing is resident, the
// marking pass guarantees residency for everything the main view actually sees. Static and
// dynamic resolve their rings independently (see the dynamic block below) and combine via min
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

    // Receivers below the window's far plane have no representable occluder: the raster clamps
    // their casters away (pages stay 0 = lit), but an unclamped receiver depth goes negative and
    // then even EMPTY texels compare as shadowing — the camera-anchored window climbs with the
    // camera, so terrain more than zHalfRange below it (along the light axis) blacks out square.
    // No data means lit. The Z window is global, so no ring can do better — bail out entirely
    if (zHalf - (mul(relativePos, lightRotation).z + camMinusZAnchor) < 0.0f)
    {
        return 1.0f;
    }

    float staticFactor = 1.0f;
    for (uint k = uint(selectedClipmap); k < numClipmaps; k++)
    {
        float extent = _svsmData[0].extent[k];
        float texelWorld = extent / float(pageTableSize * pageSize);

        // Receiver pushed along its surface normal in clipmap texels, same acne control as the CSM path
        float3 lightSpacePos = mul(relativePos + worldNormal * (texelWorld * shadowSettings.normalOffset), lightRotation);
        float2 windowPos = lightSpacePos.xy + float2(_svsmData[0].camMinusWindowMinX[k], _svsmData[0].camMinusWindowMinY[k]);

        // Reversed Z: 1 = sun side. lightSpacePos is camera-relative, the raster
        // maps absolute light-space Z, camMinusZAnchor reconciles the two with small quantities.
        // Positive bias moves the receiver toward the sun
        float compareDepth = (zHalf - (lightSpacePos.z + camMinusZAnchor)) / zRangeLength + compareBias;

        float2 texelPos = windowPos / texelWorld;

        float shadowFactor;
        if (!SVSMFilterRing(k, texelPos, compareDepth, pageTableSize, pageSize, poolPagesPerRow, false, _svsmPageTable, _svsmPagePool, shadowFactor))
        {
            continue; // Fall back to a coarser clipmap, worst case ends lit
        }

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
            if (!SVSMIsDynamicPageResident(kd, int2(floor(texelPos)), pageTableSize, pageSize))
            {
                continue;
            }

            float compareDepth = (zHalf - (lightSpacePos.z + camMinusZAnchor)) / zRangeLength + compareBias;

            // Same filter as the static ring against the dynamic table/pool; non-resident pages
            // read lit, so this always resolves
            SVSMFilterRing(kd, texelPos, compareDepth, pageTableSize, pageSize, _svsmData[0].configDynamicPoolPagesPerRow, true, _svsmDynamicPageTable, _svsmDynamicPagePool, dynamicFactor);
            break;
        }
    }

    return min(staticFactor, dynamicFactor);
}

// Diagnostic view of the SVSM compare: single center-texel sample, classification colors.
// Dark purple = no SVSM state, blue = outside coverage (XY window or below the Z window),
// magenta = page never resident,
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

    // Below the window's far plane, same bail as the sampler: no representable occluder
    if (zRangeLength * 0.5f - (mul(relativePos, lightRotation).z + _svsmData[0].configCamMinusZAnchor) < 0.0f)
    {
        return float3(0.0f, 0.3f, 1.0f);
    }

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