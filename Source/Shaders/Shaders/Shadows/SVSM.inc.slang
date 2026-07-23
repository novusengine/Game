#ifndef SVSM_INCLUDED
#define SVSM_INCLUDED

#include "Shadows/LightBasis.inc.slang"

// Shared types and helpers for the Sparse Virtual Shadow Map chain:
// SVSMPrepare.cs -> SVSMInvalidateAABBs.cs -> SVSMPageUpdateA.cs -> SVSMPageMark.cs -> SVSMPageUpdateB.cs
//
// Clipmap k is a camera-centered light-space window of clipmap0Extent * 2^k meters, backed by a
// pageTableSize^2 page table over a virtual pageTableSize * pageSize texture. Page content is
// world-stable: a physical page holds a fixed absolute light-space page until its table slot is
// reused (toroidal wrap) or its content is invalidated.
//
// Precision rule: no wrap math at world magnitude. World coordinates reach ~17k and the finest
// texels are millimeters, so all addressing is anchor-relative: Prepare stores per clipmap the
// page-snapped window origin as an integer absolute page index (anchorPageMin) plus the camera
// offset inside the window (camMinusWindowMin), and every pass works with
// windowPos = lightSpace(P - camera) + camMinusWindowMin, which is at most one window extent.

// Push constants shared by all SVSM dispatches. One layout everywhere, but allocClipmap,
// dynamicPhase and numDirtyAABBs vary per dispatch
struct SVSMConstants
{
    float4 lightDirection;  // xyz = direction the light travels, quantized shadow sun
    uint numClipmaps;
    uint pageTableSize;     // pages per virtual texture row
    uint pageSize;          // texels per page row
    uint poolPagesPerRow;   // physical pool pages per row, total pages = poolPagesPerRow^2
    float clipmap0Extent;   // world extent of the finest clipmap window
    float markBorderTexels; // filter footprint margin, marks neighbor pages near page borders
    uint evictAge;          // frames without a mark before a resident page returns to the pool
    uint invalidateAll;     // CPU-requested invalidation cause bits (SVSM_CAUSE_*)
    uint numDirtyAABBs;     // Static dirty AABB count; for DynamicMark this carries the DYNAMIC AABB count instead
    uint allocClipmap;      // UpdateB runs once per clipmap, finest first, so pool starvation always lands on the coarsest ring
    float casterMargin;     // Sun-side sweep on the near cull plane, depth clamp pancakes those casters
    float zHalfRange;       // Half depth range of every clipmap window around the quantized Z anchor
    uint fillDrawCallCount; // Model draw calls this frame, Finalize sizes the per-view fill overhead args from it
    float padding1;
    float resolutionScale;  // Eye-distance clipmap floor scale, <= 0 disables
    uint dynamicPoolPagesPerRow; // Caster-split dynamic pool pages per row, 0 while the split is off
    uint renderBudget;      // Static pages rendered per frame, 0 = unlimited; the coarsest two rings are exempt
    uint dynamicPhase;      // DynamicUpdate: 0 = release pages, 1 = acquire + bookkeeping. Split so free-list pushes and pops never share a dispatch
    uint fillInstanceCount; // Model instances this frame, Finalize sizes the per-view fill dispatch args from it
    uint fillCellCount;     // Terrain cells this frame, same purpose
};

// Page table entry bits. A zeroed entry is a free slot
#define SVSM_PAGE_PHYS_MASK   0xFFFu       // Physical page index, flat, pool is capped to 4096 pages
#define SVSM_PAGE_AGE_SHIFT   12
#define SVSM_PAGE_AGE_MASK    (0xFFu << SVSM_PAGE_AGE_SHIFT)
#define SVSM_PAGE_INVALID     (1u << 24)   // Content is stale, re-render before the next sample
#define SVSM_PAGE_RESIDENT    (1u << 25)   // Backed by a physical page
#define SVSM_PAGE_MARKED      (1u << 26)   // Touched by a visible sample this frame
#define SVSM_PAGE_DIRTY       (1u << 27)   // Renders this frame, always consumed the frame it is set

// Invalidation cause bits, CPU bits come in through SVSMConstants::invalidateAll
#define SVSM_CAUSE_SUN_STEP      1u
#define SVSM_CAUSE_Z_RANGE       2u
#define SVSM_CAUSE_MANUAL        4u
#define SVSM_CAUSE_AABB_OVERFLOW 8u

// Persistent GPU state + per-frame stats. Scalar arrays only so the std430 layout matches the
// flat u32 readback on the CPU
struct SVSMData
{
    float4 prevLightDirection;   // w > 0.5 once valid

    float zRangeMin;             // Reserved for the stage 2 Z fit
    float zRangeMax;
    uint frameInvalidationFlags; // Transient: written by Prepare (or AABB overflow), consumed by UpdateA the same frame
    uint padding0;

    int anchorPageMinX[8];       // Absolute page index of the window min corner
    int anchorPageMinY[8];
    int prevAnchorPageMinX[8];
    int prevAnchorPageMinY[8];
    float camMinusWindowMinX[8]; // Camera light-space XY minus the window min corner, small
    float camMinusWindowMinY[8];
    float extent[8];
    float pageWorld[8];          // extent / pageTableSize
    uint clipmapInvalidate[8];   // Transient: clipmap config changed, UpdateA invalidates the whole clipmap

    int dirtyRectMinX[8];        // Window-local page coords of this frame's dirty pages
    int dirtyRectMinY[8];
    int dirtyRectMaxX[8];
    int dirtyRectMaxY[8];

    uint statsMarked[8];
    uint statsResident[8];
    uint statsDirty[8];
    uint statsEvicted[8];
    uint statsInvalidated[8];
    uint statsOverflow;
    uint statsInvalidationCause;
    uint statsFreeListCount;     // Snapshot at the start of the frame

    // Config mirror written by Prepare so draw-time shaders only need this buffer plus viewIndex
    uint configPageTableSize;
    uint configPageSize;
    uint configPoolPagesPerRow;
    uint configNumClipmaps;
    float configMarkBorderTexels;  // The sampler mirrors the marking margin through this
    float configPadding0;
    float configCamMinusZAnchor;   // Camera light-space Z minus the Z anchor: the sampler works
                                   // camera-relative for precision, the raster works absolute, this
                                   // small offset reconciles the two depth mappings
    float configResolutionScale;   // Eye-distance clipmap floor scale for the sampler, <= 0 disables
    uint padding3;

    // Static/dynamic caster split: dynamic pages live in their own small pool and re-render every
    // frame, so cached static content is never touched by animated/moving casters
    int dynamicRectMinX[8];        // Window-local page coords of this frame's live dynamic pages
    int dynamicRectMinY[8];
    int dynamicRectMaxX[8];
    int dynamicRectMaxY[8];

    uint statsDynamicLive[8];
    uint statsDynamicOverflow;
    uint statsDynamicTotal;        // Live dynamic pages across all clipmaps, gates the sampler's dynamic lookup
    uint configDynamicPoolPagesPerRow;
    uint padding4;

    // Render budget: pages past the per-frame cap stay MARKED+INVALID and retry next frame
    uint statsDeferred[8];
    uint statsBudgetUsed;
    uint padding5;
    uint padding6;
    uint padding7;

    // Fragment clip rects: this frame's dirty pages classified by UpdateB into three window-local
    // page rects per clipmap, indexed [rect * 8 + clipmap]. Rect 0 = newly exposed X columns,
    // 1 = newly exposed Y rows, 2 = everything else. The SVSM vertex shaders clip each of the
    // three static draws to one rect (SV_ClipDistance) so an L-shaped toroidal update rasterizes
    // two thin stripes instead of its huge bounding box
    int clipRectMinX[24];
    int clipRectMinY[24];
    int clipRectMaxX[24];
    int clipRectMaxY[24];
};

#define SVSM_CLIP_RECT_DISABLED 0xFFFFFFFFu

// Clip distances for one static SVSM draw: positive inside the rect, negative outside, so the
// fixed-function clipper culls triangles before rasterization. Same window math as the sampler.
// An empty rect clips everything, a disabled index (dynamic draws) clips nothing
float4 SVSMRectClipDistances(float3 worldPos, uint clipmapIndex, uint rectIndex, float3 cameraPos, StructuredBuffer<SVSMData> svsmData)
{
    if (rectIndex == SVSM_CLIP_RECT_DISABLED)
    {
        return float4(1.0f, 1.0f, 1.0f, 1.0f);
    }

    uint rectSlot = rectIndex * 8 + clipmapIndex;
    int2 rectMin = int2(svsmData[0].clipRectMinX[rectSlot], svsmData[0].clipRectMinY[rectSlot]);
    int2 rectMax = int2(svsmData[0].clipRectMaxX[rectSlot], svsmData[0].clipRectMaxY[rectSlot]);
    if (any(rectMax < rectMin))
    {
        return float4(-1.0f, -1.0f, -1.0f, -1.0f);
    }

    float3x3 lightRotation = BuildLightRotation(svsmData[0].prevLightDirection.xyz);
    float2 lightSpaceXY = mul(worldPos - cameraPos, lightRotation).xy;
    float2 windowPos = lightSpaceXY + float2(svsmData[0].camMinusWindowMinX[clipmapIndex], svsmData[0].camMinusWindowMinY[clipmapIndex]);

    uint pageSize = svsmData[0].configPageSize;
    float texelWorld = svsmData[0].extent[clipmapIndex] / float(svsmData[0].configPageTableSize * pageSize);
    float2 texel = windowPos / texelWorld;

    float2 minTexel = float2(rectMin * int(pageSize));
    float2 maxTexel = float2((rectMax + int2(1, 1)) * int(pageSize));
    return float4(texel.x - minTexel.x, maxTexel.x - texel.x, texel.y - minTexel.y, maxTexel.y - texel.y);
}

uint GetPagePhysical(uint entry)
{
    return entry & SVSM_PAGE_PHYS_MASK;
}

uint GetPageAge(uint entry)
{
    return (entry & SVSM_PAGE_AGE_MASK) >> SVSM_PAGE_AGE_SHIFT;
}

uint PackPageEntry(uint physicalPage, uint age, uint flags)
{
    return (physicalPage & SVSM_PAGE_PHYS_MASK) | (age << SVSM_PAGE_AGE_SHIFT) | flags;
}

// Table slot of an absolute page index, pageTableSize is a power of two
int2 WrapPageSlot(int2 absolutePage, uint pageTableSize)
{
    return absolutePage & int2(pageTableSize - 1, pageTableSize - 1);
}

uint PageEntryIndex(uint clipmapIndex, int2 slot, uint pageTableSize)
{
    return clipmapIndex * pageTableSize * pageTableSize + uint(slot.y) * pageTableSize + uint(slot.x);
}

// Window-local page rect of a world AABB in one clipmap: light-space XY rect via the |R|
// extents trick, one page of pad so filter taps just outside the caster footprint are covered
// too, clamped to the window. Returns false when the AABB misses this clipmap's window.
// Page granularity is coarse (quarter meters at the finest clipmap) so plain world-magnitude
// light-space math is precise enough here. windowMin uses the same reconstruction Prepare used
// for camMinusWindowMin, deterministic vs the stored anchors
bool SVSMComputeAABBPageRect(float3 aabbMin, float3 aabbMax, float3 lightDirection, float pageWorld, int2 anchorPageMin, uint pageTableSize, out int2 pageMin, out int2 pageMax)
{
    float3 center = (aabbMin + aabbMax) * 0.5f;
    float3 halfExtents = (aabbMax - aabbMin) * 0.5f;

    float3x3 lightRotation = BuildLightRotation(lightDirection);
    float2 centerLS = mul(center, lightRotation).xy;
    float2 halfLS = mul(halfExtents, float3x3(abs(lightRotation[0]), abs(lightRotation[1]), abs(lightRotation[2]))).xy;

    float2 windowMin = float2(anchorPageMin) * pageWorld;

    pageMin = int2(floor((centerLS - halfLS - windowMin) / pageWorld)) - 1;
    pageMax = int2(floor((centerLS + halfLS - windowMin) / pageWorld)) + 1;
    pageMin = max(pageMin, int2(0, 0));
    pageMax = min(pageMax, int2(pageTableSize - 1, pageTableSize - 1));

    return all(pageMax >= pageMin);
}

// Finest clipmap whose window contains the position with margin to spare, -1 if none.
// One shared function for marking and sampling: the margin guarantees the filter footprint and
// the border page marks never leave the selected window. lightSpaceRelCam = mul(P - cam, R).xy
//
// Eye-distance floor: clipmap containment is light-space-centric but resolution needs are
// eye-centric (at grazing sun, distant relief sits close in light-space XY and would burn fine
// texels on content whose screen pixels are meters wide). Rings whose texels are finer than the
// sample's approximate screen footprint are skipped. floorRingBias: the sampler passes -1 so its
// walk always passes through the ring the marker marked, regardless of float divergence between
// their eye distance reconstructions. resolutionScale <= 0 disables the floor
int SelectClipmap(float2 lightSpaceRelCam, float eyeDistance, StructuredBuffer<SVSMData> svsmData, uint numClipmaps, uint pageTableSize, uint pageSize, float marginTexels, float resolutionScale, int floorRingBias)
{
    uint startClipmap = 0;
    if (resolutionScale > 0.0f)
    {
        // ~1000 pixels across the FOV as the fixed reference, exactness is irrelevant
        float minTexelWorld = eyeDistance * resolutionScale * (1.0f / 1000.0f);
        for (uint k = 0; k + 1 < numClipmaps; k++)
        {
            float texelWorld = svsmData[0].extent[k] / float(pageTableSize * pageSize);
            if (texelWorld >= minTexelWorld)
            {
                break;
            }
            startClipmap++;
        }
        startClipmap = uint(max(int(startClipmap) + floorRingBias, 0));
    }

    for (uint k = startClipmap; k < numClipmaps; k++)
    {
        float extent = svsmData[0].extent[k];
        float texelWorld = extent / float(pageTableSize * pageSize);
        float margin = marginTexels * texelWorld;

        float2 windowPos = lightSpaceRelCam + float2(svsmData[0].camMinusWindowMinX[k], svsmData[0].camMinusWindowMinY[k]);
        if (all(windowPos > float2(margin, margin)) && all(windowPos < float2(extent - margin, extent - margin)))
        {
            return int(k);
        }
    }
    return -1;
}

// The page-table entry under a page-render fragment. svPosition.xy comes straight from
// SV_Position: window-local virtual texel coordinates (the viewport spans the full virtual
// texture)
uint SVSMLoadPageEntry(float2 svPositionXY, uint clipmapIndex, StructuredBuffer<SVSMData> svsmData, StructuredBuffer<uint> pageTable)
{
    uint pageTableSize = svsmData[0].configPageTableSize;
    uint pageSize = svsmData[0].configPageSize;

    int2 texel = int2(svPositionXY);
    int2 localPage = texel / int(pageSize);
    int2 anchorPageMin = int2(svsmData[0].anchorPageMinX[clipmapIndex], svsmData[0].anchorPageMinY[clipmapIndex]);
    int2 slot = WrapPageSlot(anchorPageMin + localPage, pageTableSize);

    return pageTable[PageEntryIndex(clipmapIndex, slot, pageTableSize)];
}

// Software depth write for the SVSM page render passes, taking a pre-loaded page entry (the pixel
// shader may have fetched it for its early alpha-key skip). z is the reversed ortho depth; depth
// clamp disables clipping so pancaked casters saturate here instead.
// requiredFlags: static pages render only while DIRTY, dynamic pages render whenever RESIDENT
void SVSMWritePageDepthEntry(float3 svPosition, uint entry, uint requiredFlags, uint poolPagesPerRow, StructuredBuffer<SVSMData> svsmData, RWTexture2D<uint> pagePool)
{
    if ((entry & requiredFlags) != requiredFlags)
    {
        return;
    }

    uint pageSize = svsmData[0].configPageSize;
    int2 texel = int2(svPosition.xy);
    int2 localPage = texel / int(pageSize);

    uint physicalPage = GetPagePhysical(entry);
    uint2 physicalBase = uint2(physicalPage % poolPagesPerRow, physicalPage / poolPagesPerRow) * pageSize;
    uint2 pageTexel = uint2(texel - localPage * int(pageSize));

    InterlockedMax(pagePool[physicalBase + pageTexel], asuint(saturate(svPosition.z)));
}

// Fragments outside pages that render this frame exit after one table load
void SVSMWritePageDepth(float3 svPosition, uint clipmapIndex, uint requiredFlags, uint poolPagesPerRow, StructuredBuffer<SVSMData> svsmData, StructuredBuffer<uint> pageTable, RWTexture2D<uint> pagePool)
{
    uint entry = SVSMLoadPageEntry(svPosition.xy, clipmapIndex, svsmData, pageTable);
    SVSMWritePageDepthEntry(svPosition, entry, requiredFlags, poolPagesPerRow, svsmData, pagePool);
}

#endif // SVSM_INCLUDED
