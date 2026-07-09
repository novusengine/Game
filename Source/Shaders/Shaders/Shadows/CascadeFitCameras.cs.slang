#include "Include/Camera.inc.slang"
#include "Shadows/SDSM.inc.slang"

// Final fit dispatch: builds the cascade cameras and writes them into the shared cameras
// buffer that culling, drawing and the material resolve read. Splits come from
// CascadeFitRange via the SDSM data buffer.
// Single thread: the work is a handful of tiny ortho fits with a sequential
// lastSplitDist dependency, cross-lane sync would cost more than it saves.

[[vk::push_constant]] SDSMConstants _constants;

[[vk::binding(0, PER_PASS)]] RWStructuredBuffer<Camera> _rwCameras; // Same buffer as the global _cameras, element 0 = main camera
[[vk::binding(1, PER_PASS)]] RWStructuredBuffer<SDSMData> _sdsmData;
[[vk::binding(2, PER_PASS)]] StructuredBuffer<uint> _cascadeBounds; // Written by CascadeXYReduce

// 1.25x multiplicative buckets with 5% down-hysteresis: within a bucket the texel scale is
// constant so bound changes are pure translation, which the texel snap absorbs
float BucketHalfExtent(float halfExtent, float previousBucket)
{
    const float bucketRatio = 1.25f;
    float target = pow(bucketRatio, ceil(log(halfExtent) / log(bucketRatio)));
    target = max(target, 2.0f); // Keep degenerate fits (single-texel receivers) sane

    if (previousBucket > 0.0f && target < previousBucket)
    {
        // Only shrink when the padded extent clearly fits the smaller bucket
        if (halfExtent >= 0.76f * previousBucket)
        {
            return previousBucket;
        }
    }
    return target;
}

// Every world position maps to clip (0, 0, 10, 1): TextureProj's z window rejects it and the
// material pass renders lit without sampling. Culling rejects everything via the frustum planes
Camera BuildEmptyCascadeCamera(float3 mainCameraPos, float splitDepth)
{
    Camera camera = (Camera)0;
    camera.worldToClip[3] = float4(0.0f, 0.0f, 10.0f, 1.0f);
    camera.viewToClip = BuildOrtho(-0.5f, 0.5f, -0.5f, 0.5f, 1.0f, 0.0f); // Keeps the normal-offset texel math finite
    camera.eyePosition = float4(mainCameraPos, splitDepth); // Split depth must stay valid for neighbor selection

    for (uint i = 0; i < 6; i++)
    {
        camera.frustum[i] = float4(0.0f, 1.0f, 0.0f, 3.0e38f); // dot(n, p) - w is always far below -radius
    }
    return camera;
}

[shader("compute")]
[numthreads(1, 1, 1)]
void main()
{
    Camera mainCamera = _rwCameras[0];

    float nearClip = mainCamera.nearFar.x;
    float farClip = mainCamera.nearFar.y;
    float clipRange = farClip - nearClip;

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
        float splitDist = _sdsmData[0].splitDist[cascadeIndex];

        // Decode the reduced light-space bounds for this cascade, sentinel decodes as min > max
        bool xyValid = false;
        float3 boundsMin = float3(0.0f, 0.0f, 0.0f);
        float3 boundsMax = float3(0.0f, 0.0f, 0.0f);
        if (_constants.useXYBounds >= 1)
        {
            uint3 encodedMin = uint3(_cascadeBounds[cascadeIndex * 3 + 0], _cascadeBounds[cascadeIndex * 3 + 1], _cascadeBounds[cascadeIndex * 3 + 2]);
            uint3 encodedMax = uint3(_cascadeBounds[24 + cascadeIndex * 3 + 0], _cascadeBounds[24 + cascadeIndex * 3 + 1], _cascadeBounds[24 + cascadeIndex * 3 + 2]);

            xyValid = all(encodedMin <= encodedMax);
            if (xyValid)
            {
                boundsMin = float3(DecodeLightSpaceCoord(encodedMin.x), DecodeLightSpaceCoord(encodedMin.y), DecodeLightSpaceCoord(encodedMin.z));
                boundsMax = float3(DecodeLightSpaceCoord(encodedMax.x), DecodeLightSpaceCoord(encodedMax.y), DecodeLightSpaceCoord(encodedMax.z));
            }

            _sdsmData[0].cascadeDiag[cascadeIndex] = float4(xyValid ? boundsMax - boundsMin : float3(0.0f, 0.0f, 0.0f), xyValid ? 1.0f : 0.0f);
        }

        if (_constants.useXYBounds == 2 && !xyValid)
        {
            // No visible receivers in this cascade's depth range: nothing can sample it
            _rwCameras[1 + cascadeIndex] = BuildEmptyCascadeCamera(mainCamera.eyePosition.xyz, _sdsmData[0].splitDepth[cascadeIndex]);
            _sdsmData[0].cascadeStable[cascadeIndex] = float4(0.0f, 0.0f, 0.0f, 0.0f); // Buckets reseed when the cascade refills
            lastSplitDist = splitDist;
            continue;
        }

        float3 frustumCenter;
        float3 upDir;
        float3 minExtents;
        float3 maxExtents;

        if (_constants.useXYBounds == 2)
        {
            upDir = StableUp(lightDirection);

            float3 centerLS = (boundsMin + boundsMax) * 0.5f;
            float3 halfLS = (boundsMax - boundsMin) * 0.5f;

            // Filter + normal-offset margin, applied before bucketing so the bucket absorbs it
            float marginScale = (_constants.cascadeTextureSize + 2.0f * _constants.xyMarginTexels) / _constants.cascadeTextureSize;
            halfLS.xy *= marginScale;

            float previousBucketX = _sdsmData[0].cascadeStable[cascadeIndex].x;
            float previousBucketY = _sdsmData[0].cascadeStable[cascadeIndex].y;
            float bucketHalfX = BucketHalfExtent(halfLS.x, previousBucketX);
            float bucketHalfY = BucketHalfExtent(halfLS.y, previousBucketY);
            _sdsmData[0].cascadeStable[cascadeIndex] = float4(bucketHalfX, bucketHalfY, 0.0f, 0.0f);

            // Z: pad by the margin in world units and quantize outward, Z only affects the bias
            // distribution so piecewise-constant is enough
            float texelWorldSize = (2.0f * bucketHalfX) / _constants.cascadeTextureSize;
            float zPadding = _constants.xyMarginTexels * texelWorldSize;
            float zStep = max(_constants.quantizeStep, 1.0f);
            float quantizedMinZ = floor((boundsMin.z - zPadding) / zStep) * zStep;
            float quantizedMaxZ = ceil((boundsMax.z + zPadding) / zStep) * zStep;
            quantizedMaxZ = max(quantizedMaxZ, quantizedMinZ + 1.0f);

            // The reduced bounds are camera-relative, rotate the center back and re-anchor on the camera
            float3x3 lightRotation = BuildLightRotation(lightDirection);
            frustumCenter = mainCamera.eyePosition.xyz + mul(centerLS, transpose(lightRotation));

            minExtents = float3(-bucketHalfX, -bucketHalfY, quantizedMinZ - centerLS.z);
            maxExtents = float3(bucketHalfX, bucketHalfY, quantizedMaxZ - centerLS.z);
        }
        else
        {

        // Slice the frustum, corners 0-3 are on the far plane, the ray points from far to near
        float3 frustumCorners[8];
        for (uint j = 0; j < 4; j++)
        {
            float3 cornerRay = baseCorners[j + 4] - baseCorners[j];
            frustumCorners[j + 4] = baseCorners[j] + cornerRay * splitDist;
            frustumCorners[j] = baseCorners[j] + cornerRay * lastSplitDist;
        }

        frustumCenter = float3(0.0f, 0.0f, 0.0f);
        for (uint j = 0; j < 8; j++)
        {
            frustumCenter += frustumCorners[j];
        }
        frustumCenter /= 8.0f;

        // Non-stable mode uses the camera right axis, world X basis of viewToWorld
        upDir = -float3(mainCamera.viewToWorld[0].xyz);

        if (_constants.stableShadows)
        {
            upDir = StableUp(lightDirection);

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

        cascadeCamera.eyePosition = float4(shadowCameraPos, _sdsmData[0].splitDepth[cascadeIndex]);
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
