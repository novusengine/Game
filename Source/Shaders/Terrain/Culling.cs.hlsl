#include "common.inc.hlsl"
#include "Include/Culling.inc.hlsl"
#include "Include/PyramidCulling.inc.hlsl"
#include "Include/Debug.inc.hlsl"
#include "globalData.inc.hlsl"
#include "Terrain/Shared.inc.hlsl"

struct Constants
{
    uint numCascades;
    uint occlusionCull;
};

[[vk::push_constant]] Constants _constants;

// Inputs
[[vk::binding(0, TERRAIN)]] StructuredBuffer<InstanceData> _instances;
[[vk::binding(1, TERRAIN)]] StructuredBuffer<uint> _heightRanges;
[[vk::binding(2, TERRAIN)]] SamplerState _depthSampler;
[[vk::binding(3, TERRAIN)]] Texture2D<float> _depthPyramid;
[[vk::binding(4, TERRAIN)]] StructuredBuffer<uint> _prevCulledInstancesBitMask;

// Outputs
[[vk::binding(5, TERRAIN)]] RWStructuredBuffer<uint> _culledInstancesBitMask;
[[vk::binding(6, TERRAIN)]] RWByteAddressBuffer _arguments;
[[vk::binding(7, TERRAIN)]] RWByteAddressBuffer _culledInstances[NUM_CULL_VIEWS];

float2 ReadHeightRange(uint instanceIndex)
{
    const uint packed = _heightRanges[instanceIndex];
    const float min = f16tof32(packed >> 16);
    const float max = f16tof32(packed);
    return float2(min, max);
}

bool SphereIsForwardPlane(float4 plane, float4 sphere)
{
    return dot(plane.xyz, sphere.xyz) + plane.w > -sphere.w;
}

bool IsSphereInsideFrustum(float4 frustum[6], float4 sphere)
{
    for (int i = 0; i < 6; ++i)
    {
        const float4 plane = frustum[i];

        if (!SphereIsForwardPlane(plane, sphere))
        {
            return false;
        }
    }

    return true;
}

struct CSInput
{
    uint3 dispatchThreadID : SV_DispatchThreadID;
    uint3 groupID : SV_GroupID;
    uint3 groupThreadID : SV_GroupThreadID;
};

struct DrawInput
{
    CSInput csInput;
    InstanceData instance;
    float4 sphere;
    AABB aabb;
    bool shouldOcclusionCull;
    bool isShadowPass;
};

struct BitmaskInput
{
    bool useBitmasks;
    StructuredBuffer<uint> prevCulledInstancesBitMask;
    RWStructuredBuffer<uint> outCulledInstancesBitMask;
};

struct CullOutput
{
    RWByteAddressBuffer instanceBuffer;
    uint argumentIndex;
    RWByteAddressBuffer argumentBuffer;
    RWByteAddressBuffer triangleCount;
};

void CullForView(DrawInput drawInput,
    Camera camera,
    BitmaskInput bitmaskInput,
    CullOutput cullOutput)
{
    bool isVisible = true;

    if (!IsSphereInsideFrustum(camera.frustum, drawInput.sphere))
    {
        isVisible = false;
    }
    else if (drawInput.shouldOcclusionCull)
    {
        bool isIntersectingNearZ = IsIntersectingNearZ(drawInput.aabb.min, drawInput.aabb.max, camera.worldToClip);

        if (!isIntersectingNearZ && !IsVisible(drawInput.aabb.min, drawInput.aabb.max, camera.eyePosition.xyz, _depthPyramid, _depthSampler, camera.worldToClip))
        {
            isVisible = false;
        }
    }

    bool shouldRender = isVisible;
    if (bitmaskInput.useBitmasks)
    {
        uint bitMask = WaveActiveBallot(isVisible).x;

        // The first thread writes the bitmask
        if (drawInput.csInput.groupThreadID.x == 0)
        {
            bitmaskInput.outCulledInstancesBitMask[drawInput.csInput.groupID.x] = bitMask;
        }

        uint occluderBitMask = bitmaskInput.prevCulledInstancesBitMask[drawInput.csInput.groupID.x];
        uint renderBitMask = bitMask & ~occluderBitMask; // This should give us all currently visible objects that were not occluders

        // We only want to render objects that are visible and not occluders since they were already rendered this frame
        shouldRender = renderBitMask & (1u << drawInput.csInput.groupThreadID.x);
    }

    if (shouldRender)
    {
        uint argumentByteOffset = cullOutput.argumentIndex * (sizeof(uint) * 5); // VkDrawIndexedIndirectCommand

        uint culledInstanceIndex;
        cullOutput.argumentBuffer.InterlockedAdd(argumentByteOffset + 4, 1, culledInstanceIndex);

        uint firstInstanceOffset = cullOutput.argumentBuffer.Load(argumentByteOffset + 16);
        WriteCellInstanceToByteAdressBuffer(cullOutput.instanceBuffer, firstInstanceOffset + culledInstanceIndex, drawInput.instance);
    }

    // Debug draw AABB boxes
    /*if (isVisible)
    {
        DrawAABB3D(drawInput.aabb, DebugColor::GREEN);
    }
    else
    {
        DrawAABB3D(drawInput.aabb, DebugColor::RED);
    }*/
}

[numthreads(32, 1, 1)]
void main(CSInput input)
{
    const uint instanceIndex = input.dispatchThreadID.x;
    InstanceData instance = _instances[instanceIndex];

    const uint cellID = instance.packedChunkCellID & 0xffff;
    const uint chunkID = instance.packedChunkCellID >> 16;

    const float2 heightRange = ReadHeightRange(instanceIndex);
    AABB aabb = GetCellAABB(chunkID, cellID, heightRange);

    float4 sphere;
    sphere.xyz = (aabb.min + aabb.max) / 2.0f;
    sphere.w = distance(aabb.max, aabb.min);// / 2.0f;

    DrawInput drawInput;
    drawInput.csInput = input;
    drawInput.instance = instance;
    drawInput.sphere = sphere;
    drawInput.aabb = aabb;
    drawInput.shouldOcclusionCull = _constants.occlusionCull;
    drawInput.isShadowPass = false;

    BitmaskInput bitmaskInput;
    bitmaskInput.useBitmasks = true;
    bitmaskInput.prevCulledInstancesBitMask = _prevCulledInstancesBitMask;
    bitmaskInput.outCulledInstancesBitMask = _culledInstancesBitMask;

    CullOutput cullOutput;
    cullOutput.instanceBuffer = _culledInstances[0];
    cullOutput.argumentIndex = 0;
    cullOutput.argumentBuffer = _arguments;

    // Main camera view
    {
        CullForView(drawInput, _cameras[0], bitmaskInput, cullOutput);
    }

    // Shadow cascades
    /*for (uint i = 0; i < _constants.numCascades; i++)
    {
        Camera cascadeCamera = _cameras[i+1];

        drawInput.shouldOcclusionCull = false; // No occlusion culling for shadow cascades... yet?
        drawInput.isShadowPass = true;
        bitmaskInput.useBitmasks = false;

        cullOutput.instanceBuffer = _culledInstances[i + 1];
        cullOutput.argumentIndex = i + 1;

        CullForView(drawInput, cascadeCamera, bitmaskInput, cullOutput);
    }*/
}