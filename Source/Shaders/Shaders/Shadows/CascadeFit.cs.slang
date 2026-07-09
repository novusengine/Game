#include "Include/Camera.inc.slang"

// GPU port of the cascade camera fitting in CalculateShadowCameraMatrices.cpp.
// Writes cascade cameras [1..numCascades] into the shared cameras buffer that culling,
// drawing and the material resolve read. With useDepthBounds the cascades distribute over
// the visible depth range reduced by DepthMinMax.cs (SDSM), otherwise over the full
// [max(near, 1), shadowMaxDistance] range like the legacy CPU path (parity mode).
//
// Matrix layout rule (load-bearing): shaders consume glm column-major uploads with
// mul(rowVector, M), so a Slang float4x4 row i must hold what glm stores in column i.
// glm composition C = A * B therefore becomes C_s = mul(B_s, A_s) here.

struct Constants
{
    float4 lightDirection; // xyz = direction the light travels
    uint numCascades;
    float cascadeSplitLambda;
    float cascadeTextureSize;
    uint stableShadows;
    float shadowMaxDistance;
    float quantizeStep;
    float shrinkDelay;
    float deltaTime;
    uint useDepthBounds;
    float casterMargin;
};
[[vk::push_constant]] Constants _constants;

struct SDSMState
{
    float smoothedMin;
    float smoothedMax;
    float usedMin;
    float usedMax;
    float windowLowestMin;  // Widest bounds seen during the current shrink window
    float windowHighestMax;
    float windowTimer;
    float padding;
};

[[vk::binding(0, PER_PASS)]] RWStructuredBuffer<Camera> _rwCameras; // Same buffer as the global _cameras, element 0 = main camera
[[vk::binding(1, PER_PASS)]] StructuredBuffer<uint> _depthMinMax; // [0] = min depth bits (farthest), [1] = max depth bits (nearest)
[[vk::binding(2, PER_PASS)]] RWStructuredBuffer<SDSMState> _sdsmState;

// Matches glm::lookAtLH memory layout (GLM_FORCE_LEFT_HANDED)
float4x4 BuildLookAt(float3 eye, float3 center, float3 up)
{
    float3 f = normalize(center - eye);
    float3 s = normalize(cross(up, f));
    float3 u = cross(f, s);

    float4x4 m;
    m[0] = float4(s.x, u.x, f.x, 0.0f);
    m[1] = float4(s.y, u.y, f.y, 0.0f);
    m[2] = float4(s.z, u.z, f.z, 0.0f);
    m[3] = float4(-dot(s, eye), -dot(u, eye), -dot(f, eye), 1.0f);
    return m;
}

// Analytic inverse of a BuildLookAt matrix (rigid transform)
float4x4 InverseLookAt(float4x4 view, float3 eye)
{
    float3 s = float3(view[0].x, view[1].x, view[2].x);
    float3 u = float3(view[0].y, view[1].y, view[2].y);
    float3 f = float3(view[0].z, view[1].z, view[2].z);

    float4x4 m;
    m[0] = float4(s, 0.0f);
    m[1] = float4(u, 0.0f);
    m[2] = float4(f, 0.0f);
    m[3] = float4(eye, 1.0f);
    return m;
}

// Matches glm::orthoLH_ZO memory layout (GLM_FORCE_DEPTH_ZERO_TO_ONE)
float4x4 BuildOrtho(float left, float right, float bottom, float top, float zNear, float zFar)
{
    float4x4 m;
    m[0] = float4(2.0f / (right - left), 0.0f, 0.0f, 0.0f);
    m[1] = float4(0.0f, 2.0f / (top - bottom), 0.0f, 0.0f);
    m[2] = float4(0.0f, 0.0f, 1.0f / (zFar - zNear), 0.0f);
    m[3] = float4(-(right + left) / (right - left), -(top + bottom) / (top - bottom), -zNear / (zFar - zNear), 1.0f);
    return m;
}

// Analytic inverse of a BuildOrtho matrix, uses the post-snap projection values
float4x4 InverseOrtho(float4x4 proj)
{
    float3 scale = float3(proj[0].x, proj[1].y, proj[2].z);
    float3 translation = proj[3].xyz;

    float4x4 m;
    m[0] = float4(1.0f / scale.x, 0.0f, 0.0f, 0.0f);
    m[1] = float4(0.0f, 1.0f / scale.y, 0.0f, 0.0f);
    m[2] = float4(0.0f, 0.0f, 1.0f / scale.z, 0.0f);
    m[3] = float4(-translation / scale, 1.0f);
    return m;
}

// glm row i of M, planes are extracted from mathematical rows
float4 MatrixRow(float4x4 m, uint i)
{
    return float4(m[0][i], m[1][i], m[2][i], m[3][i]);
}

// Gribb-Hartmann row (inside if dot(n, p) + d >= 0) to the culling encoding vec4(n, dot(n, pointOnPlane))
float4 ExtractPlane(float4 row)
{
    float normalLength = length(row.xyz);
    return float4(row.xyz / normalLength, -row.w / normalLength);
}

[shader("compute")]
[numthreads(1, 1, 1)]
void main()
{
    Camera mainCamera = _rwCameras[0];

    float nearClip = mainCamera.nearFar.x;
    float farClip = mainCamera.nearFar.y;
    float clipRange = farClip - nearClip;

    // Working range for the cascade distribution
    float rangeMin = max(nearClip, 1.0f);
    float rangeMax = min(farClip, _constants.shadowMaxDistance);

    SDSMState state = _sdsmState[0];

    if (_constants.useDepthBounds && _depthMinMax[1] != 0)
    {
        // Reversed Z linearization, depth 1 = near plane, depth 0 = far plane
        float minDepth = asfloat(_depthMinMax[1]); // Max depth bits = nearest sample
        float maxDepth = asfloat(_depthMinMax[0]); // Min depth bits = farthest sample

        float rawMin = (nearClip * farClip) / (nearClip + minDepth * (farClip - nearClip));
        float rawMax = (nearClip * farClip) / (nearClip + maxDepth * (farClip - nearClip));

        rawMin = clamp(rawMin, rangeMin, _constants.shadowMaxDistance - 1.0f);
        rawMax = clamp(rawMax, rawMin + 1.0f, _constants.shadowMaxDistance);

        // Hysteresis: expand instantly, shrink in a single jump after the range has been smaller
        // for a whole window. A continuous shrink rescales the cascades every step and the texel
        // grid realignment reads as shimmer, one jump per window is one realignment
        if (state.smoothedMax <= state.smoothedMin) // Uninitialized state
        {
            state.smoothedMin = rawMin;
            state.smoothedMax = rawMax;
            state.windowLowestMin = rawMin;
            state.windowHighestMax = rawMax;
            state.windowTimer = 0.0f;
        }
        else
        {
            state.smoothedMin = min(state.smoothedMin, rawMin);
            state.smoothedMax = max(state.smoothedMax, rawMax);

            state.windowLowestMin = min(state.windowLowestMin, rawMin);
            state.windowHighestMax = max(state.windowHighestMax, rawMax);
            state.windowTimer += _constants.deltaTime;

            if (state.windowTimer >= _constants.shrinkDelay)
            {
                // Shrink to the widest bounds the window actually needed
                state.smoothedMin = max(state.smoothedMin, state.windowLowestMin);
                state.smoothedMax = min(state.smoothedMax, state.windowHighestMax);

                state.windowLowestMin = rawMin;
                state.windowHighestMax = rawMax;
                state.windowTimer = 0.0f;
            }
        }

        // Quantize outward AFTER smoothing so the working bounds are piecewise-constant
        // and the stable texel snapping stays effective
        float step = max(_constants.quantizeStep, 0.01f);
        rangeMin = max(floor(state.smoothedMin / step) * step, max(nearClip, 1.0f));
        rangeMax = min(ceil(state.smoothedMax / step) * step, _constants.shadowMaxDistance);
        rangeMax = max(rangeMax, rangeMin + 1.0f);
    }
    else
    {
        // Parity mode / no valid depth: keep the state seeded so enabling SDSM starts sane
        state.smoothedMin = rangeMin;
        state.smoothedMax = rangeMax;
        state.windowLowestMin = rangeMin;
        state.windowHighestMax = rangeMax;
        state.windowTimer = 0.0f;
    }

    state.usedMin = rangeMin;
    state.usedMax = rangeMax;
    _sdsmState[0] = state;

    // Split distances, same log/uniform lambda mix as the CPU path.
    // The split fractions stay relative to the FULL camera clip range so the frustum
    // corner slicing and the eyePosition.w = distance - near semantics hold.
    float cascadeSplits[8];
    {
        float ratio = rangeMax / rangeMin;
        float range = rangeMax - rangeMin;
        for (uint i = 0; i < _constants.numCascades; i++)
        {
            float p = (i + 1) / float(_constants.numCascades);
            float logSplit = rangeMin * pow(ratio, p);
            float uniformSplit = rangeMin + range * p;
            float d = _constants.cascadeSplitLambda * (logSplit - uniformSplit) + uniformSplit;

            cascadeSplits[i] = 1.0f - ((d - nearClip) / clipRange);
        }
    }

    // Unproject the NDC cube corners, z = 0 is the far plane with reversed Z
    const float3 ndcCorners[8] =
    {
        float3(-1.0f,  1.0f, 0.0f),
        float3( 1.0f,  1.0f, 0.0f),
        float3( 1.0f, -1.0f, 0.0f),
        float3(-1.0f, -1.0f, 0.0f),
        float3(-1.0f,  1.0f, 1.0f),
        float3( 1.0f,  1.0f, 1.0f),
        float3( 1.0f, -1.0f, 1.0f),
        float3(-1.0f, -1.0f, 1.0f)
    };

    float3 baseCorners[8];
    for (uint j = 0; j < 8; j++)
    {
        float4 corner = mul(float4(ndcCorners[j], 1.0f), mainCamera.clipToWorld);
        baseCorners[j] = corner.xyz / corner.w;
    }

    float3 lightDirection = _constants.lightDirection.xyz;

    float lastSplitDist = 1.0f;
    for (uint cascadeIndex = 0; cascadeIndex < _constants.numCascades; cascadeIndex++)
    {
        float splitDist = cascadeSplits[cascadeIndex];

        // Slice the frustum, corners 0-3 are on the far plane, the ray points from far to near
        float3 frustumCorners[8];
        for (uint j = 0; j < 4; j++)
        {
            float3 cornerRay = baseCorners[j + 4] - baseCorners[j];
            frustumCorners[j + 4] = baseCorners[j] + cornerRay * splitDist;
            frustumCorners[j] = baseCorners[j] + cornerRay * lastSplitDist;
        }

        float3 frustumCenter = float3(0.0f, 0.0f, 0.0f);
        for (uint j = 0; j < 8; j++)
        {
            frustumCenter += frustumCorners[j];
        }
        frustumCenter /= 8.0f;

        // Non-stable mode uses the camera right axis, world X basis of viewToWorld
        float3 upDir = -float3(mainCamera.viewToWorld[0].xyz);

        float3 minExtents;
        float3 maxExtents;
        if (_constants.stableShadows)
        {
            // Constant up for stability, fall back to Z when the light is near vertical
            upDir = float3(0.0f, 1.0f, 0.0f);
            if (abs(lightDirection.y) > 0.99f)
            {
                upDir = float3(0.0f, 0.0f, 1.0f);
            }

            float sphereRadius = 0.0f;
            for (uint j = 0; j < 8; j++)
            {
                float dist = length(frustumCorners[j] - frustumCenter);
                sphereRadius = max(sphereRadius, dist);
            }

            sphereRadius = ceil(sphereRadius * 16.0f) / 16.0f;

            if (_constants.useDepthBounds)
            {
                // Quantize the radius into multiplicative buckets: within a bucket the texel scale is
                // constant so bound changes are pure translation, which the texel snap absorbs. Only a
                // bucket crossing rescales the grid, and only for that cascade
                const float bucketRatio = 1.25f;
                sphereRadius = pow(bucketRatio, ceil(log(sphereRadius) / log(bucketRatio)));
            }

            maxExtents = float3(sphereRadius, sphereRadius, sphereRadius);
            minExtents = -maxExtents;
        }
        else
        {
            // AABB of the slice in light view space
            float4x4 lightView = BuildLookAt(frustumCenter, frustumCenter + lightDirection, upDir);

            float3 mins = float3(3.402823466e+38f, 3.402823466e+38f, 3.402823466e+38f);
            float3 maxes = -mins;
            for (uint j = 0; j < 8; j++)
            {
                float4 corner = mul(float4(frustumCorners[j], 1.0f), lightView);
                mins = min(mins, corner.xyz / corner.w);
                maxes = max(maxes, corner.xyz / corner.w);
            }

            minExtents = mins;
            maxExtents = maxes;

            // Adjust the min/max to accommodate the filtering size
            const float fixedFilterKernelSize = 3.0f;
            float scale = (_constants.cascadeTextureSize + fixedFilterKernelSize) / _constants.cascadeTextureSize;
            minExtents.xy *= scale;
            maxExtents.xy *= scale;
        }

        // minExtents.z is negative, this places the shadow camera on the sun side
        float3 shadowCameraPos = frustumCenter + lightDirection * minExtents.z;

        float farPlane = maxExtents.z;
        float nearPlane = minExtents.z;

        float4x4 proj = BuildOrtho(minExtents.x, maxExtents.x, minExtents.y, maxExtents.y, farPlane - nearPlane, 0.0f);
        float4x4 view = BuildLookAt(shadowCameraPos, frustumCenter, upDir);

        if (_constants.stableShadows)
        {
            // Texel snap: round the projected world origin to texel increments
            float4x4 shadowMatrix = mul(view, proj);
            float4 shadowOrigin = shadowMatrix[3]; // mul(float4(0,0,0,1), M) = row 3 = glm translation column
            shadowOrigin *= _constants.cascadeTextureSize / 2.0f;

            float4 roundedOrigin = round(shadowOrigin);
            float4 roundOffset = (roundedOrigin - shadowOrigin) * (2.0f / _constants.cascadeTextureSize);

            proj[3].xy += roundOffset.xy;
        }

        Camera cascadeCamera = (Camera)0;

        cascadeCamera.worldToView = view;
        cascadeCamera.viewToClip = proj;
        cascadeCamera.viewToWorld = InverseLookAt(view, shadowCameraPos);
        cascadeCamera.clipToView = InverseOrtho(proj);
        cascadeCamera.worldToClip = mul(view, proj); // glm viewToClip * worldToView
        cascadeCamera.clipToWorld = mul(cascadeCamera.clipToView, cascadeCamera.viewToWorld); // glm viewToWorld * clipToView

        float splitDepth = farClip - (nearClip + splitDist * clipRange); // = split view distance - nearClip
        cascadeCamera.eyePosition = float4(shadowCameraPos, splitDepth);
        cascadeCamera.eyeRotation = float4(0.0f, 0.0f, 0.0f, 0.0f);
        cascadeCamera.nearFar = float4(0.0f, 0.0f, 0.0f, 0.0f);

        // Frustum planes, slot order matches the FrustumPlane enum: Left, Right, Bottom, Top, Near, Far
        float4 row0 = MatrixRow(cascadeCamera.worldToClip, 0);
        float4 row1 = MatrixRow(cascadeCamera.worldToClip, 1);
        float4 row2 = MatrixRow(cascadeCamera.worldToClip, 2);
        float4 row3 = MatrixRow(cascadeCamera.worldToClip, 3);

        cascadeCamera.frustum[0] = ExtractPlane(row3 + row0); // Left
        cascadeCamera.frustum[1] = ExtractPlane(row3 - row0); // Right
        cascadeCamera.frustum[2] = ExtractPlane(row3 + row1); // Bottom
        cascadeCamera.frustum[3] = ExtractPlane(row3 - row1); // Top
        cascadeCamera.frustum[4] = ExtractPlane(row3 - row2); // Near, reversed Z so depth 1 is near
        cascadeCamera.frustum[5] = ExtractPlane(row2);        // Far

        // The near plane sits at the shadow camera on the sun side. Casters beyond it still render
        // correctly (depth clamp pancakes them), so culling must not reject them
        cascadeCamera.frustum[4].w -= _constants.casterMargin;

        _rwCameras[1 + cascadeIndex] = cascadeCamera;

        lastSplitDist = splitDist;
    }
}
