#include "Include/Camera.inc.slang"
#include "Shadows/SDSM.inc.slang"

// Second fit dispatch: reduces the light-space AABB of the visible depth samples per cascade.
// Positions are reconstructed camera-relative (clipToView + rotate-only viewToWorld), going
// through clipToWorld at world-scale coordinates loses precision. Coordinates are encoded
// with the order-preserving signed-float transform so uint atomics work on them.

[[vk::push_constant]] SDSMConstants _constants;

[[vk::binding(0, PER_PASS)]] Texture2D<float> _depth;
[[vk::binding(1, PER_PASS)]] StructuredBuffer<Camera> _srcCameras; // Element 0 = main camera
[[vk::binding(2, PER_PASS)]] StructuredBuffer<SDSMData> _sdsmData;
[[vk::binding(3, PER_PASS)]] RWStructuredBuffer<uint> _cascadeBounds; // [cascade * 3 + axis] encoded mins, [24 + cascade * 3 + axis] encoded maxs

groupshared uint gMins[24];
groupshared uint gMaxs[24];

struct CSInput
{
    uint3 dispatchThreadID : SV_DispatchThreadID;
    uint groupIndex : SV_GroupIndex;
};

void Contribute(uint cascadeIndex, uint3 encoded)
{
    uint base = cascadeIndex * 3;
    InterlockedMin(gMins[base + 0], encoded.x);
    InterlockedMin(gMins[base + 1], encoded.y);
    InterlockedMin(gMins[base + 2], encoded.z);
    InterlockedMax(gMaxs[base + 0], encoded.x);
    InterlockedMax(gMaxs[base + 1], encoded.y);
    InterlockedMax(gMaxs[base + 2], encoded.z);
}

[shader("compute")]
[numthreads(16, 16, 1)]
void main(CSInput input)
{
    if (input.groupIndex < 24)
    {
        gMins[input.groupIndex] = 0xFFFFFFFFu;
        gMaxs[input.groupIndex] = 0u;
    }
    GroupMemoryBarrierWithGroupSync();

    uint2 dim;
    _depth.GetDimensions(dim.x, dim.y);

    uint2 pixel = min(input.dispatchThreadID.xy, dim - 1);
    float depth = _depth[pixel];

    // Cleared depth is exactly 0.0 (reversed Z far plane), sky must not drive the bounds
    if (depth != 0.0f)
    {
        Camera mainCamera = _srcCameras[0];

        float2 uv = (float2(pixel) + 0.5f) / float2(dim);
        float2 ndc = float2(uv.x, 1.0f - uv.y) * 2.0f - 1.0f;

        float4 viewPos = mul(float4(ndc, depth, 1.0f), mainCamera.clipToView);
        viewPos.xyz /= viewPos.w;

        float viewZ = viewPos.z;
        uint numCascades = _constants.numCascades;
        float lastSplitDepth = _sdsmData[0].splitDepth[numCascades - 1];

        // Beyond the last split the material pass is fully faded, those pixels never use cascade data
        if (viewZ <= lastSplitDepth * 1.005f)
        {
            // Mirrors GetShadowCascadeIndexFromDepth, _cameras[i].eyePosition.w == splitDepth[i - 1]
            uint cascadeIndex = 0;
            for (uint i = 1; i < numCascades; i++)
            {
                if (viewZ > _sdsmData[0].splitDepth[i - 1])
                {
                    cascadeIndex = i;
                }
            }

            // Camera-relative world position, rotation only
            float3 relativePos = mul(float4(viewPos.xyz, 0.0f), mainCamera.viewToWorld).xyz;
            float3 lightSpacePos = mul(relativePos, BuildLightRotation(_constants.lightDirection.xyz));

            uint3 encoded = uint3(
                EncodeLightSpaceCoord(lightSpacePos.x),
                EncodeLightSpaceCoord(lightSpacePos.y),
                EncodeLightSpaceCoord(lightSpacePos.z));

            Contribute(cascadeIndex, encoded);

            // Pixels near a split also contribute to the neighbor: the material pass reconstructs
            // viewZ through a different float path and must never select a cascade this reduce
            // considered empty
            float epsilon = 0.005f * max(_sdsmData[0].splitDepth[cascadeIndex], 1.0f);
            if (cascadeIndex > 0 && (viewZ - _sdsmData[0].splitDepth[cascadeIndex - 1]) < epsilon)
            {
                Contribute(cascadeIndex - 1, encoded);
            }
            if (cascadeIndex + 1 < numCascades && (_sdsmData[0].splitDepth[cascadeIndex] - viewZ) < epsilon)
            {
                Contribute(cascadeIndex + 1, encoded);
            }
        }
    }

    GroupMemoryBarrierWithGroupSync();

    if (input.groupIndex < 24)
    {
        if (gMins[input.groupIndex] != 0xFFFFFFFFu)
        {
            InterlockedMin(_cascadeBounds[input.groupIndex], gMins[input.groupIndex]);
        }
        if (gMaxs[input.groupIndex] != 0u)
        {
            InterlockedMax(_cascadeBounds[24 + input.groupIndex], gMaxs[input.groupIndex]);
        }
    }
}
