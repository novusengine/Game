#include "Include/Camera.inc.slang"
#include "Shadows/SVSM.inc.slang"

// First SVSM dispatch, single thread: detects invalidation events, snaps the per-clipmap window
// anchors to the absolute page grid and resets the per-frame stats.
//
// The anchors are the precision foundation for every later pass: anchorPageMin is exact integer
// math, and camMinusWindowMin is the only place the world-magnitude cancellation happens, so all
// consumers share the same (sub-texel) absolute placement error instead of diverging.

[[vk::push_constant]] SVSMConstants _constants;

[[vk::binding(0, PER_PASS)]] RWStructuredBuffer<SVSMData> _svsmData;
[[vk::binding(1, PER_PASS)]] StructuredBuffer<Camera> _srcCameras; // Element 0 = main camera
[[vk::binding(2, PER_PASS)]] StructuredBuffer<uint> _freeList;     // [0] = count, entries from [4]
[[vk::binding(3, PER_PASS)]] RWStructuredBuffer<uint> _clearList;  // [0..2] = indirect dispatch args, physical pages from [4]
[[vk::binding(4, PER_PASS)]] RWStructuredBuffer<uint> _dynamicClearList; // Same layout, for the caster-split dynamic pool

[shader("compute")]
[numthreads(1, 1, 1)]
void main()
{
    // Start-of-frame snapshot, the free list only changes in UpdateA/UpdateB later in the frame
    _svsmData[0].statsFreeListCount = _freeList[0];

    // Reset the dirty page clear lists, UpdateB / DynamicUpdate append and bump the indirect group counts
    _clearList[0] = 0;
    _clearList[1] = 1;
    _clearList[2] = 1;
    _dynamicClearList[0] = 0;
    _dynamicClearList[1] = 1;
    _dynamicClearList[2] = 1;

    uint cause = _constants.invalidateAll;

    // The quantized shadow sun stepped, every page's light basis changed
    float3 lightDirection = _constants.lightDirection.xyz;
    float4 prevLightDirection = _svsmData[0].prevLightDirection;
    if (prevLightDirection.w > 0.5f && dot(prevLightDirection.xyz, lightDirection) < 0.999999f)
    {
        cause |= SVSM_CAUSE_SUN_STEP;
    }

    float3x3 lightRotation = BuildLightRotation(lightDirection);
    float3 cameraLightSpace = mul(_srcCameras[0].eyePosition.xyz, lightRotation);

    // One global Z window for all clipmaps, anchored on the camera in coarse steps: stored page
    // depths change meaning when it moves, so it must move rarely and invalidate when it does
    const float zAnchorStep = 512.0f;
    float zAnchor = floor(cameraLightSpace.z / zAnchorStep + 0.5f) * zAnchorStep;
    float zRangeMin = zAnchor - _constants.zHalfRange;
    float zRangeMax = zAnchor + _constants.zHalfRange;
    if (prevLightDirection.w > 0.5f && (_svsmData[0].zRangeMin != zRangeMin || _svsmData[0].zRangeMax != zRangeMax))
    {
        cause |= SVSM_CAUSE_Z_RANGE;
    }
    _svsmData[0].zRangeMin = zRangeMin;
    _svsmData[0].zRangeMax = zRangeMax;
    _svsmData[0].prevLightDirection = float4(lightDirection, 1.0f);

    _svsmData[0].frameInvalidationFlags = cause;
    _svsmData[0].statsInvalidationCause = cause;
    _svsmData[0].statsOverflow = 0;

    uint pageTableSize = _constants.pageTableSize;

    // Config mirror for the draw-time shaders, they only get viewIndex as a push constant
    _svsmData[0].configPageTableSize = pageTableSize;
    _svsmData[0].configPageSize = _constants.pageSize;
    _svsmData[0].configPoolPagesPerRow = _constants.poolPagesPerRow;
    _svsmData[0].configNumClipmaps = _constants.numClipmaps;
    _svsmData[0].configMarkBorderTexels = _constants.markBorderTexels;
    _svsmData[0].configCamMinusZAnchor = cameraLightSpace.z - zAnchor;
    _svsmData[0].configResolutionScale = _constants.resolutionScale;

    _svsmData[0].statsDynamicOverflow = 0;
    _svsmData[0].statsDynamicTotal = 0;
    _svsmData[0].configDynamicPoolPagesPerRow = _constants.dynamicPoolPagesPerRow;
    _svsmData[0].statsBudgetUsed = 0;

    for (uint k = 0; k < 8; k++)
    {
        // Per-frame stats and dirty rect sentinels
        _svsmData[0].statsMarked[k] = 0;
        _svsmData[0].statsResident[k] = 0;
        _svsmData[0].statsDirty[k] = 0;
        _svsmData[0].statsEvicted[k] = 0;
        _svsmData[0].statsInvalidated[k] = 0;
        _svsmData[0].statsDynamicLive[k] = 0;
        _svsmData[0].statsDeferred[k] = 0;
        _svsmData[0].dirtyRectMinX[k] = int(pageTableSize);
        _svsmData[0].dirtyRectMinY[k] = int(pageTableSize);
        _svsmData[0].dirtyRectMaxX[k] = -1;
        _svsmData[0].dirtyRectMaxY[k] = -1;
        _svsmData[0].dynamicRectMinX[k] = int(pageTableSize);
        _svsmData[0].dynamicRectMinY[k] = int(pageTableSize);
        _svsmData[0].dynamicRectMaxX[k] = -1;
        _svsmData[0].dynamicRectMaxY[k] = -1;

        for (uint r = 0; r < 3; r++)
        {
            _svsmData[0].clipRectMinX[r * 8 + k] = int(pageTableSize);
            _svsmData[0].clipRectMinY[r * 8 + k] = int(pageTableSize);
            _svsmData[0].clipRectMaxX[r * 8 + k] = -1;
            _svsmData[0].clipRectMaxY[r * 8 + k] = -1;
        }

        if (k >= _constants.numClipmaps)
        {
            _svsmData[0].clipmapInvalidate[k] = 0;
            continue;
        }

        float extent = _constants.clipmap0Extent * exp2(float(k));
        float pageWorld = extent / float(pageTableSize);

        // A window geometry change makes the whole clipmap's content unaddressable
        _svsmData[0].clipmapInvalidate[k] = (_svsmData[0].extent[k] != extent || _svsmData[0].pageWorld[k] != pageWorld) ? 1u : 0u;

        // Window min corner snapped to the absolute page grid, camera sits pageTableSize/2 pages in.
        // floor can flip by one page when the camera rests exactly on a boundary, that only costs a
        // toroidal row invalidation, never correctness
        int2 anchorPageMin = int2(floor(cameraLightSpace.xy / pageWorld)) - int(pageTableSize / 2);

        _svsmData[0].prevAnchorPageMinX[k] = _svsmData[0].anchorPageMinX[k];
        _svsmData[0].prevAnchorPageMinY[k] = _svsmData[0].anchorPageMinY[k];
        _svsmData[0].anchorPageMinX[k] = anchorPageMin.x;
        _svsmData[0].anchorPageMinY[k] = anchorPageMin.y;

        // The one world-magnitude subtraction, its rounding shifts the whole clipmap by a fraction
        // of a texel and is shared by every consumer through this stored value
        float2 windowMin = float2(anchorPageMin) * pageWorld;
        _svsmData[0].camMinusWindowMinX[k] = cameraLightSpace.x - windowMin.x;
        _svsmData[0].camMinusWindowMinY[k] = cameraLightSpace.y - windowMin.y;

        _svsmData[0].extent[k] = extent;
        _svsmData[0].pageWorld[k] = pageWorld;
    }
}
