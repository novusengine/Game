#include "Include/Camera.inc.slang"
#include "Shadows/SVSM.inc.slang"

// Last SVSM dispatch, single thread, runs after UpdateB and DynamicUpdate so the dirty rects and
// per-ring page stats are final: writes the clipmap render cameras into the shared cameras buffer
// that culling and the page render passes read, and the per-view fill dispatch args that gate the
// geometry passes' instance fills. Same GPU-writes-the-camera-slots shape the retired SDSM fit
// pass used, minus the texel snap: the window center is aligned to the absolute page grid by
// construction.
//
// The camera MATRICES always map the full clipmap window (the render viewport spans the whole
// virtual texture). The cull PLANES are tightened to this frame's dirty page rect, XY-tight is
// safe for an ortho light (a caster's light-space XY equals its shadow's XY); the near plane is
// swept toward the sun by the caster margin, depth clamp pancakes those casters at render time.
//
// The fill args are the same-frame GPU truth the per-view skip must run on: a ring with no DIRTY
// static pages skips its static fill, a ring with no resident dynamic pages skips its dynamic
// fill. Gating this from a CPU readback (one frame old) drops freshly acquired dynamic pages'
// draws for a frame — resident-but-undrawn pages read lit and flicker dynamic shadows out

[[vk::push_constant]] SVSMConstants _constants;

[[vk::binding(0, PER_PASS)]] RWStructuredBuffer<Camera> _rwCameras; // Same buffer as the global _cameras, element 0 = main camera
[[vk::binding(1, PER_PASS)]] StructuredBuffer<SVSMData> _svsmData;
[[vk::binding(2, PER_PASS)]] RWStructuredBuffer<uint> _fillDispatchArgs; // Per clipmap: 5 x uvec3 (model static fill, model dynamic fill, terrain static fill, model static overhead, model dynamic overhead)

[shader("compute")]
[numthreads(1, 1, 1)]
void main()
{
    float3 lightDirection = _constants.lightDirection.xyz;
    float3x3 lightRotation = BuildLightRotation(lightDirection);
    float3 upDir = StableUp(lightDirection);

    // Per-view fill dispatch args, zero groups for rings with no page work this frame. The
    // overhead args gate the fill's drawcall-granularity companions (instance-count clear +
    // CreateIndirect) on the same bits — safe because a skipped CreateIndirect means nothing
    // reads the stale instance counts, and the tiny per-fill counter reset stays unconditional
    uint modelFillGroups = (_constants.fillInstanceCount + 31) / 32;
    uint terrainFillGroups = (_constants.fillCellCount + 31) / 32;
    uint modelOverheadGroups = (_constants.fillDrawCallCount + 31) / 32;
    for (uint k = 0; k < 8; k++)
    {
        bool inUse = k < _constants.numClipmaps;
        bool hasDirty = inUse && _svsmData[0].statsDirty[k] != 0;
        bool hasDynamic = inUse && _svsmData[0].statsDynamicLive[k] != 0;

        uint base = k * 15;
        _fillDispatchArgs[base + 0] = hasDirty ? modelFillGroups : 0;
        _fillDispatchArgs[base + 1] = 1;
        _fillDispatchArgs[base + 2] = 1;
        _fillDispatchArgs[base + 3] = hasDynamic ? modelFillGroups : 0;
        _fillDispatchArgs[base + 4] = 1;
        _fillDispatchArgs[base + 5] = 1;
        _fillDispatchArgs[base + 6] = hasDirty ? terrainFillGroups : 0;
        _fillDispatchArgs[base + 7] = 1;
        _fillDispatchArgs[base + 8] = 1;
        _fillDispatchArgs[base + 9] = hasDirty ? modelOverheadGroups : 0;
        _fillDispatchArgs[base + 10] = 1;
        _fillDispatchArgs[base + 11] = 1;
        _fillDispatchArgs[base + 12] = hasDynamic ? modelOverheadGroups : 0;
        _fillDispatchArgs[base + 13] = 1;
        _fillDispatchArgs[base + 14] = 1;
    }

    float zRangeMin = _svsmData[0].zRangeMin;
    float zRangeMax = _svsmData[0].zRangeMax;
    float zCenter = (zRangeMin + zRangeMax) * 0.5f;
    float zHalf = max((zRangeMax - zRangeMin) * 0.5f, 1.0f);

    for (uint k = 0; k < _constants.numClipmaps; k++)
    {
        float extent = _svsmData[0].extent[k];
        float pageWorld = _svsmData[0].pageWorld[k];
        float halfExtent = extent * 0.5f;

        int2 anchorPageMin = int2(_svsmData[0].anchorPageMinX[k], _svsmData[0].anchorPageMinY[k]);
        float2 windowMinLS = float2(anchorPageMin) * pageWorld;
        float2 centerLS = windowMinLS + float2(halfExtent, halfExtent);

        // Rotate the light-space center back to world, mul(v, transpose(R)) inverts the rotation
        float3 worldCenter = mul(float3(centerLS, zCenter), transpose(lightRotation));
        float3 shadowCameraPos = worldCenter - lightDirection * zHalf; // Sun side

        // Bottom/top swapped: the engine's viewport Y-flip would otherwise mirror the page content
        // against the marking/sampling texel math, this way SV_Position.xy IS the virtual texel
        float4x4 proj = BuildOrtho(-halfExtent, halfExtent, halfExtent, -halfExtent, 2.0f * zHalf, 0.0f); // Reversed Z, sun side = 1
        float4x4 view = BuildLookAt(shadowCameraPos, worldCenter, upDir);

        Camera clipmapCamera = (Camera)0;
        clipmapCamera.worldToView = view;
        clipmapCamera.viewToClip = proj;
        clipmapCamera.viewToWorld = InverseLookAt(view, shadowCameraPos);
        clipmapCamera.clipToView = InverseOrtho(proj);
        clipmapCamera.worldToClip = mul(view, proj); // glm viewToClip * worldToView
        clipmapCamera.clipToWorld = mul(clipmapCamera.clipToView, clipmapCamera.viewToWorld); // glm viewToWorld * clipToView

        clipmapCamera.eyePosition = float4(shadowCameraPos, 0.0f);
        clipmapCamera.eyeRotation = float4(0.0f, 0.0f, 0.0f, 0.0f);
        clipmapCamera.nearFar = float4(0.0f, 0.0f, 0.0f, 0.0f);

        // Union of the static dirty rect and the dynamic live rect: one cull volume per view serves
        // both fill classes, the class filter and the PS page checks absorb the over-inclusion
        int2 dirtyMin = min(int2(_svsmData[0].dirtyRectMinX[k], _svsmData[0].dirtyRectMinY[k]),
                            int2(_svsmData[0].dynamicRectMinX[k], _svsmData[0].dynamicRectMinY[k]));
        int2 dirtyMax = max(int2(_svsmData[0].dirtyRectMaxX[k], _svsmData[0].dirtyRectMaxY[k]),
                            int2(_svsmData[0].dynamicRectMaxX[k], _svsmData[0].dynamicRectMaxY[k]));
        if (any(dirtyMax < dirtyMin))
        {
            // Nothing renders into this clipmap this frame, cull everything
            for (uint p = 0; p < 6; p++)
            {
                clipmapCamera.frustum[p] = float4(0.0f, 1.0f, 0.0f, 3.0e38f); // dot(n, p) - w is always far below -radius
            }
        }
        else
        {
            // Cull volume over the dirty pages only, window-local rect back to center-relative extents
            float cullMinX = float(dirtyMin.x) * pageWorld - halfExtent;
            float cullMaxX = float(dirtyMax.x + 1) * pageWorld - halfExtent;
            float cullMinY = float(dirtyMin.y) * pageWorld - halfExtent;
            float cullMaxY = float(dirtyMax.y + 1) * pageWorld - halfExtent;

            float4x4 cullProj = BuildOrtho(cullMinX, cullMaxX, cullMaxY, cullMinY, 2.0f * zHalf, 0.0f); // Same Y mirror as proj, plane extraction is sign-agnostic
            float4x4 cullMatrix = mul(view, cullProj);

            float4 row0 = MatrixRow(cullMatrix, 0);
            float4 row1 = MatrixRow(cullMatrix, 1);
            float4 row2 = MatrixRow(cullMatrix, 2);
            float4 row3 = MatrixRow(cullMatrix, 3);

            clipmapCamera.frustum[0] = ExtractPlane(row3 + row0); // Left
            clipmapCamera.frustum[1] = ExtractPlane(row3 - row0); // Right
            clipmapCamera.frustum[2] = ExtractPlane(row3 + row1); // Bottom
            clipmapCamera.frustum[3] = ExtractPlane(row3 - row1); // Top
            clipmapCamera.frustum[4] = ExtractPlane(row3 - row2); // Near, reversed Z so depth 1 is near
            clipmapCamera.frustum[5] = ExtractPlane(row2);        // Far

            // Casters beyond the near plane still render correctly (depth clamp pancakes them)
            clipmapCamera.frustum[4].w -= _constants.casterMargin;
        }

        _rwCameras[1 + k] = clipmapCamera;
    }
}
