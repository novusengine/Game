#include "Include/Camera.inc.slang"
#include "Shadows/SVSM.inc.slang"

// Marks the pages touched by visible depth samples: one thread per depth pixel reconstructs the
// world position and marks the page it samples. Positions are reconstructed camera-relative (clipToView + rotate-only
// viewToWorld) and addressed through the anchor-relative window math from SVSM.inc.
//
// Samples within markBorderTexels of a page border also mark the neighbor page so the filter
// footprint is resident at sampling time; the selection margin guarantees those neighbors exist
// inside the selected clipmap's window.
//
// A 16x16 group usually lands in a handful of distinct pages, so marks are deduplicated through a
// small LDS cache and flushed as a few global ORs instead of 256.

[[vk::push_constant]] SVSMConstants _constants;

[[vk::binding(0, PER_PASS)]] Texture2D<float> _depth;
[[vk::binding(1, PER_PASS)]] StructuredBuffer<Camera> _srcCameras; // Element 0 = main camera
[[vk::binding(2, PER_PASS)]] StructuredBuffer<SVSMData> _svsmData;
[[vk::binding(3, PER_PASS)]] RWStructuredBuffer<uint> _pageTable;

#define MARK_CACHE_SIZE 16
#define MARK_CACHE_EMPTY 0xFFFFFFFFu

groupshared uint gMarkCache[MARK_CACHE_SIZE];

void MarkPage(uint entryIndex)
{
    // Dedup through the cache, linear probing. Falls back to a direct OR when full
    for (uint i = 0; i < MARK_CACHE_SIZE; i++)
    {
        uint existing;
        InterlockedCompareExchange(gMarkCache[i], MARK_CACHE_EMPTY, entryIndex, existing);
        if (existing == MARK_CACHE_EMPTY || existing == entryIndex)
        {
            return;
        }
    }
    InterlockedOr(_pageTable[entryIndex], SVSM_PAGE_MARKED);
}

struct CSInput
{
    uint3 dispatchThreadID : SV_DispatchThreadID;
    uint groupIndex : SV_GroupIndex;
};

[shader("compute")]
[numthreads(16, 16, 1)]
void main(CSInput input)
{
    if (input.groupIndex < MARK_CACHE_SIZE)
    {
        gMarkCache[input.groupIndex] = MARK_CACHE_EMPTY;
    }
    GroupMemoryBarrierWithGroupSync();

    uint2 dim;
    _depth.GetDimensions(dim.x, dim.y);

    // Clamp instead of early return: the LDS mark-cache flush below needs every thread at the
    // group barrier, and the duplicate edge marks dedup through the cache anyway
    uint2 pixel = min(input.dispatchThreadID.xy, dim - 1);
    float depth = _depth[pixel];

    // Cleared depth is exactly 0.0 (reversed Z far plane), sky never needs shadow pages
    if (depth != 0.0f)
    {
        Camera mainCamera = _srcCameras[0];

        float2 uv = (float2(pixel) + 0.5f) / float2(dim);
        float2 ndc = float2(uv.x, 1.0f - uv.y) * 2.0f - 1.0f;

        float4 viewPos = mul(float4(ndc, depth, 1.0f), mainCamera.clipToView);
        viewPos.xyz /= viewPos.w;

        // Camera-relative world position, rotation only
        float3 relativePos = mul(float4(viewPos.xyz, 0.0f), mainCamera.viewToWorld).xyz;
        float2 lightSpaceRelCam = mul(relativePos, BuildLightRotation(_constants.lightDirection.xyz)).xy;

        uint pageTableSize = _constants.pageTableSize;
        uint pageSize = _constants.pageSize;

        // +2 texels of slack over the border marks so snap jitter can never select a clipmap whose
        // border pages fall outside the window
        float eyeDistance = length(viewPos.xyz);
        int clipmapIndex = SelectClipmap(lightSpaceRelCam, eyeDistance, _svsmData, _constants.numClipmaps, pageTableSize, pageSize, _constants.markBorderTexels + 2.0f, _constants.resolutionScale, 0);
        if (clipmapIndex >= 0)
        {
            float extent = _svsmData[0].extent[clipmapIndex];
            float texelWorld = extent / float(pageTableSize * pageSize);
            float2 windowPos = lightSpaceRelCam + float2(_svsmData[0].camMinusWindowMinX[clipmapIndex], _svsmData[0].camMinusWindowMinY[clipmapIndex]);

            int2 texel = int2(windowPos / texelWorld);
            int2 localPage = texel / int(pageSize);
            int2 pageTexel = texel - localPage * int(pageSize);
            int2 anchorPageMin = int2(_svsmData[0].anchorPageMinX[clipmapIndex], _svsmData[0].anchorPageMinY[clipmapIndex]);

            int border = int(_constants.markBorderTexels);
            int2 neighborStep = int2(0, 0);
            if (pageTexel.x < border) neighborStep.x = -1;
            else if (pageTexel.x >= int(pageSize) - border) neighborStep.x = 1;
            if (pageTexel.y < border) neighborStep.y = -1;
            else if (pageTexel.y >= int(pageSize) - border) neighborStep.y = 1;

            // The containing page plus the bordering neighbors (corner marks all three)
            for (int sy = 0; sy < 2; sy++)
            {
                for (int sx = 0; sx < 2; sx++)
                {
                    int2 pageOffset = int2(sx * neighborStep.x, sy * neighborStep.y);
                    if ((sx == 1 && pageOffset.x == 0) || (sy == 1 && pageOffset.y == 0))
                    {
                        continue; // No border on this axis, skip the duplicate
                    }

                    int2 page = clamp(localPage + pageOffset, int2(0, 0), int2(pageTableSize - 1, pageTableSize - 1));
                    int2 slot = WrapPageSlot(anchorPageMin + page, pageTableSize);
                    MarkPage(PageEntryIndex(clipmapIndex, slot, pageTableSize));
                }
            }
        }
    }

    GroupMemoryBarrierWithGroupSync();

    if (input.groupIndex < MARK_CACHE_SIZE && gMarkCache[input.groupIndex] != MARK_CACHE_EMPTY)
    {
        InterlockedOr(_pageTable[gMarkCache[input.groupIndex]], SVSM_PAGE_MARKED);
    }
}
