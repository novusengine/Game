permutation USE_BITMASKS = [0, 1];

#include "DescriptorSet/Global.inc.slang"

#include "Include/Common.inc.slang"
#include "Include/Culling.inc.slang"
#include "Include/Debug.inc.slang"
#include "Include/PyramidCulling.inc.slang"

struct Constants
{
    uint viewportSizeX;
    uint viewportSizeY;
    uint maxDrawCount;
    uint numCascades;
    uint occlusionCull;
    uint instanceIDOffset;
    uint modelIDOffset;
    uint drawCallDataSize;
    uint modelIDIsDrawCallID;
    uint cullingDataIsWorldspace;
    uint debugDrawColliders;
    uint bitMaskBufferUintsPerView;
};

struct PackedCullingData
{
    uint data0; // half min.x, half half.y, 
    uint data1; // half half.z, half max.x, 
    uint data2; // half max.y, half max.z, 
    float sphereRadius;
}; // 16 bytes

struct CullingData
{
    AABBMinMax boundingBox;
    float sphereRadius;
};

// Inputs
[[vk::push_constant]] Constants _constants;
[[vk::binding(0, PER_PASS)]] StructuredBuffer<IndexedDraw> _drawCalls;
[[vk::binding(1, PER_PASS)]] ByteAddressBuffer _drawCallDatas;
[[vk::binding(2, PER_PASS)]] StructuredBuffer<PackedCullingData> _cullingDatas;
[[vk::binding(3, PER_PASS)]] StructuredBuffer<float4x4> _instanceMatrices;
[[vk::binding(4, PER_PASS)]] SamplerState _depthSampler;
[[vk::binding(5, PER_PASS)]] Texture2D<float> _depthPyramid; // TODO: Occlusion culling for shadow cascades?

#if USE_BITMASKS
[[vk::binding(6, PER_PASS)]] StructuredBuffer<uint> _prevCulledDrawCallsBitMask;

// Outputs
[[vk::binding(7, PER_PASS)]] RWStructuredBuffer<uint> _culledDrawCallsBitMask;
#endif

[[vk::binding(8, PER_PASS)]] RWByteAddressBuffer _drawCount;
[[vk::binding(9, PER_PASS)]] RWByteAddressBuffer _triangleCount;

[[vk::binding(10, PER_PASS)]] RWByteAddressBuffer _culledDrawCalls;

uint GetInstanceID(uint drawCallID)
{
    uint offset = _constants.instanceIDOffset + (_constants.drawCallDataSize * drawCallID);
    uint instanceID = _drawCallDatas.Load(offset);
    return instanceID;
}

uint GetModelID(uint drawCallID)
{
    uint offset = _constants.modelIDOffset + (_constants.drawCallDataSize * drawCallID);
    uint modelID = _drawCallDatas.Load(offset);
    return modelID;
}

CullingData LoadCullingData(uint instanceIndex)
{
    PackedCullingData packed = _cullingDatas[instanceIndex];
    CullingData cullingData;

    cullingData.boundingBox.min.x = f16tof32(packed.data0);
    cullingData.boundingBox.min.y = f16tof32(packed.data0 >> 16);
    cullingData.boundingBox.min.z = f16tof32(packed.data1);

    cullingData.boundingBox.max.x = f16tof32(packed.data1 >> 16);
    cullingData.boundingBox.max.y = f16tof32(packed.data2);
    cullingData.boundingBox.max.z = f16tof32(packed.data2 >> 16);

    cullingData.sphereRadius = packed.sphereRadius;

    return cullingData;
}

bool SphereIsForwardPlane(float4 plane, float4 sphere)
{
    return (dot(plane.xyz, sphere.xyz) - plane.w) > -sphere.w;
}

bool IsSphereInsideFrustum(Camera camera, float4 sphere)
{
    // Due to a Slang compiler bug we cannot use loops with unroll here
    if (!SphereIsForwardPlane(camera.frustum[0], sphere)) return false;
    if (!SphereIsForwardPlane(camera.frustum[1], sphere)) return false;
    if (!SphereIsForwardPlane(camera.frustum[2], sphere)) return false;
    if (!SphereIsForwardPlane(camera.frustum[3], sphere)) return false;
    if (!SphereIsForwardPlane(camera.frustum[4], sphere)) return false;
    if (!SphereIsForwardPlane(camera.frustum[5], sphere)) return false;

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
    IndexedDraw drawCall;
    float4 sphere;
    AABBMinMax aabb;
    float4x4 instanceMatrix;
    bool shouldOcclusionCull;
};

#if USE_BITMASKS
struct BitmaskInput
{
    bool useBitmasks;
    uint bitmaskOffset;
    StructuredBuffer<uint> prevCulledDrawCallsBitMask;
    RWStructuredBuffer<uint> outCulledDrawCallsBitMask;
};
#else
struct CullOutput
{
    //RWByteAddressBuffer visibleInstanceMask;
    RWByteAddressBuffer drawCalls;
    uint countIndex;
    RWByteAddressBuffer drawCount;
    RWByteAddressBuffer triangleCount;
};
#endif

void CullForCamera(DrawInput drawInput,
    Camera camera,
#if USE_BITMASKS
    BitmaskInput bitmaskInput
#else
    CullOutput cullOutput
#endif
)
{
    bool isVisible = drawInput.drawCall.instanceCount > 0;
    if (!IsSphereInsideFrustum(camera, drawInput.sphere))
    {
        isVisible = false;
    }
    else if (drawInput.shouldOcclusionCull)
    {
        bool isIntersectingNearZ = false;
        if (_constants.cullingDataIsWorldspace)
        {
            bool isIntersectingNearZ = IsIntersectingNearZ(drawInput.aabb.min, drawInput.aabb.max, camera.worldToClip);
        }
        else
        {
            float4x4 mvp = mul(camera.worldToClip, drawInput.instanceMatrix);
            bool isIntersectingNearZ = IsIntersectingNearZ(drawInput.aabb.min, drawInput.aabb.max, mvp);
        }

        uint2 viewportSize = uint2(_constants.viewportSizeX, _constants.viewportSizeY);
        if (!isIntersectingNearZ && !IsVisible(drawInput.aabb.min, drawInput.aabb.max, camera.eyePosition.xyz, _depthPyramid, _depthSampler, camera.worldToClip, viewportSize))
        {
            isVisible = false;
        }
    }

    bool shouldRender = isVisible;
#if USE_BITMASKS
    if (bitmaskInput.useBitmasks)
    {
        uint bitMask = WaveActiveBallot(isVisible).x;

        // The first thread writes the bitmask
        if (drawInput.csInput.groupThreadID.x == 0)
        {
            uint bitMaskIndex = drawInput.csInput.groupID.x + bitmaskInput.bitmaskOffset;
            bitmaskInput.outCulledDrawCallsBitMask[bitMaskIndex] = bitMask;
        }
    }
#else
    if (shouldRender)
    {
        uint countByteOffset = cullOutput.countIndex * sizeof(uint);

        // Update triangle count
        uint outTriangles;
        cullOutput.triangleCount.InterlockedAdd(countByteOffset, (drawInput.drawCall.indexCount / 3) * drawInput.drawCall.instanceCount, outTriangles);

        // Store DrawCall
        uint outIndex;
        cullOutput.drawCount.InterlockedAdd(countByteOffset, 1, outIndex);
        WriteDrawToByteAdressBuffer(cullOutput.drawCalls, outIndex, drawInput.drawCall);
    }
#endif

    // Debug draw AABB boxes
    if (_constants.debugDrawColliders)
    {
        if (isVisible)
        {
            DrawAABB3D(drawInput.aabb, DebugColor::GREEN);
        }
        else
        {
            DrawAABB3D(drawInput.aabb, DebugColor::RED);
        }
    }
}

[shader("compute")]
[numthreads(32, 1, 1)]
void main(CSInput input)
{
    if (input.dispatchThreadID.x >= _constants.maxDrawCount)
    {
        return;
    }

    // Load DrawCall
    const uint drawCallIndex = input.dispatchThreadID.x;

    IndexedDraw drawCall = _drawCalls[drawCallIndex];
    uint drawCallID = drawCall.firstInstance;
    uint modelID = drawCallID;
    
    if (!_constants.modelIDIsDrawCallID)
    {
        modelID = GetModelID(drawCallID);
    }

    const CullingData cullingData = LoadCullingData(modelID);

    AABBMinMax aabb;
    float4x4 instanceMatrix;
    if (_constants.cullingDataIsWorldspace)
    {
        aabb = cullingData.boundingBox;
    }
    else
    {
        uint instanceID = GetInstanceID(drawCallID);
        instanceMatrix = _instanceMatrices[instanceID];

        // Get center and extents (Center is stored in min & Extents is stored in max)
        float3 center = cullingData.boundingBox.min;
        float3 extents = cullingData.boundingBox.max;

        // Transform center
        const float4x4 m = instanceMatrix;
        float3 transformedCenter = mul(float4(center, 1.0f), m).xyz;

        // Transform extents (take maximum)
        const float3x3 absMatrix = float3x3(abs(m[0].xyz), abs(m[1].xyz), abs(m[2].xyz));
        float3 transformedExtents = mul(extents, absMatrix);

        // Transform to min/max AABB representation
        aabb.min = transformedCenter - transformedExtents;
        aabb.max = transformedCenter + transformedExtents;
    }

    float4 sphere;
    sphere.xyz = (aabb.min + aabb.max) / 2.0f;
    sphere.w = distance(aabb.max, aabb.min) / 2.0f;

    DrawInput drawInput;
    drawInput.csInput = input;
    drawInput.drawCall = drawCall;
    drawInput.sphere = sphere;
    drawInput.aabb = aabb;
    drawInput.instanceMatrix = instanceMatrix;
    drawInput.shouldOcclusionCull = _constants.occlusionCull;

#if USE_BITMASKS
    BitmaskInput bitmaskInput;
    bitmaskInput.useBitmasks = true;
    bitmaskInput.bitmaskOffset = 0;
    bitmaskInput.prevCulledDrawCallsBitMask = _prevCulledDrawCallsBitMask;
    bitmaskInput.outCulledDrawCallsBitMask = _culledDrawCallsBitMask;
#else
    CullOutput cullOutput;
    cullOutput.drawCalls = _culledDrawCalls;
    cullOutput.countIndex = 0;
    cullOutput.drawCount = _drawCount;
    cullOutput.triangleCount = _triangleCount;
#endif

    // Main camera
    {
        CullForCamera(drawInput,
            _cameras[0],
#if USE_BITMASKS
            bitmaskInput
#else
            cullOutput
#endif
        );
    }

    // Shadow is only supported if we USE_BITMASKS
#if USE_BITMASKS
    // Shadow cascades
    for (uint i = 1; i < _constants.numCascades + 1; i++)
    {
        drawInput.shouldOcclusionCull = false; // No occlusion culling for shadow cascades... yet?

        bitmaskInput.bitmaskOffset = _constants.bitMaskBufferUintsPerView * i;

        CullForCamera(drawInput,
            _cameras[i],
            bitmaskInput);
    }
#endif
}