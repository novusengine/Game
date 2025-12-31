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
    uint numTotalInstances;
    uint occlusionCull;
    uint instanceCountOffset; // Byte offset into drawCalls where the instanceCount is stored
    uint drawCallSize;
    uint baseInstanceLookupOffset; // Byte offset into drawCallDatas where the baseInstanceLookup is stored
    uint modelIDOffset; // Byte offset into drawCallDatas where the modelID is stored
    uint drawCallDataSize;
    uint cullingDataIsWorldspace; // TODO: This controls two things, are both needed? I feel like one counters the other but I'm not sure...
    uint debugDrawColliders;
};

struct InstanceRef
{
    uint instanceID;
    uint drawID;
    uint extraID;
    uint padding;
};

struct PackedCullingData
{
    uint data0; // half min.x, half min.y, 
    uint data1; // half min.z, half max.x, 
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
[[vk::binding(0, PER_PASS)]] StructuredBuffer<InstanceRef> _instanceRefTable;
[[vk::binding(1, PER_PASS)]] ByteAddressBuffer _drawCalls;
[[vk::binding(2, PER_PASS)]] ByteAddressBuffer _drawCallDatas;
[[vk::binding(3, PER_PASS)]] StructuredBuffer<PackedCullingData> _cullingDatas;
[[vk::binding(4, PER_PASS)]] StructuredBuffer<float4x4> _instanceMatrices;
[[vk::binding(5, PER_PASS)]] SamplerState _depthSampler;
[[vk::binding(6, PER_PASS)]] Texture2D<float> _depthPyramid; // TODO: Occlusion culling for shadow cascades?

//#if USE_BITMASKS
// TODO: When instanceCounts resizes we need to invalidate (zero out) _prevCulledInstancesBitMask since a single index in current and prev would point to different actual drawcalls
[[vk::binding(7, PER_PASS)]] StructuredBuffer<uint> _prevCulledDrawCallsBitMask; // TODO: Rename to _prevCulledInstancesBitMask, with instanced rendering we need to count these per instance instead of per draw

// Outputs
[[vk::binding(8, PER_PASS)]] RWStructuredBuffer<uint> _culledDrawCallsBitMask; // TODO: Rename to _culledInstancesBitMask, with instanced rendering we need to count these per instance instead of per draw
//#endif

[[vk::binding(9, PER_PASS)]] RWByteAddressBuffer _culledInstanceCounts; // One uint per draw call
[[vk::binding(12, PER_PASS)]] RWStructuredBuffer<uint> _culledInstanceLookupTable; // One uint per instance, contains instanceRefID of what survives culling, and thus can get reordered

uint GetDrawCallInstanceCount(uint drawCallID)
{
    uint byteOffset = (_constants.drawCallSize * drawCallID) + _constants.instanceCountOffset;
    uint instanceCount = _drawCalls.Load(byteOffset);
    return instanceCount;
}

uint GetBaseInstanceLookup(uint drawCallID)
{
    uint byteOffset = (_constants.drawCallDataSize * drawCallID) + _constants.baseInstanceLookupOffset;
    uint baseInstanceLookup = _drawCallDatas.Load(byteOffset);
    return baseInstanceLookup;
}

uint GetModelID(uint drawCallID)
{
    uint byteOffset = (_constants.drawCallDataSize * drawCallID) + _constants.modelIDOffset;
    uint modelID = _drawCallDatas.Load(byteOffset);
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

/*bool IsSphereInsideFrustum(Camera camera, float4 sphere)
{
    [unroll]
    for (int i = 0; i < 6; ++i)
    {
        if (!SphereIsForwardPlane(camera.frustum[i], sphere))
        {
            return false;
        }
    }

    return true;
}*/

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
    uint instanceCount;
    float4 sphere;
    AABBMinMax aabb;
    float4x4 instanceMatrix;
    bool shouldOcclusionCull;
    uint instanceRefID;
};

#if USE_BITMASKS
struct BitmaskInput
{
    bool useBitmasks;
    StructuredBuffer<uint> prevCulledDrawCallsBitMask;
    RWStructuredBuffer<uint> outCulledDrawCallsBitMask;
};
#endif

struct CullOutput
{
    uint countIndex; // Used to index into instanceCounts
    RWByteAddressBuffer instanceCounts;
    uint baseInstanceLookup; // Used to index into instanceLookupTable, + atomic_add(instanceCounts[drawCallID])
    RWStructuredBuffer<uint> instanceLookupTable;
};

void CullForCamera(DrawInput drawInput,
    Camera camera,
#if USE_BITMASKS
    BitmaskInput bitmaskInput,
#endif
    CullOutput cullOutput)
{
    bool isVisible = drawInput.instanceCount > 0;
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
            float4x4 mvp = mul(camera.worldToClip, drawInput.instanceMatrix); // TODO: This is bad and unnecessary
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
            bitmaskInput.outCulledDrawCallsBitMask[drawInput.csInput.groupID.x] = bitMask;
        }

        uint occluderBitMask = bitmaskInput.prevCulledDrawCallsBitMask[drawInput.csInput.groupID.x];
        uint renderBitMask = bitMask & ~occluderBitMask; // This should give us all currently visible objects that were not occluders

        shouldRender = renderBitMask & (1u << drawInput.csInput.groupThreadID.x);
    }
#endif

    if (shouldRender)
    {
        // Update culledInstanceCounts
        uint countByteOffset = cullOutput.countIndex * sizeof(uint);

        // This index reserves a spot in the instance lookup table
        // Each drawcall has its instances in a contiguous block in the instance lookup table
        uint instanceIndex; 
        cullOutput.instanceCounts.InterlockedAdd(countByteOffset, 1, instanceIndex);

        // instanceIndex shows where in the block we can write the instanceRefID, but we need to add the offset to know where this block starts
        uint instanceLookupOffset = cullOutput.baseInstanceLookup + instanceIndex;

        // Write the instanceRefID to the instance lookup table
        _culledInstanceLookupTable[instanceLookupOffset] = drawInput.instanceRefID;
    }

    // Debug draw AABB boxes
    if (_constants.debugDrawColliders)
    {
        if (isVisible)
        {
            if (shouldRender)
            {
                DrawAABB3D(drawInput.aabb, DebugColor::GREEN);
            }
            else
            {
                DrawAABB3D(drawInput.aabb, DebugColor::BLUE);
            }
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
    // Load InstanceRef
    uint instanceRefID = input.dispatchThreadID.x;
    if (instanceRefID >= _constants.numTotalInstances)
    {
        return;
    }

    InstanceRef instanceRef = _instanceRefTable[instanceRefID];

    // Load CullingData which contains AABB of the drawcall
    uint modelID = GetModelID(instanceRef.drawID);
    CullingData cullingData = LoadCullingData(modelID);

    // Get AABB from CullingData
    AABBMinMax aabb = cullingData.boundingBox;
    float4x4 instanceMatrix = { {1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f} }; // Default to identity
    if (!_constants.cullingDataIsWorldspace)
    {
        instanceMatrix = _instanceMatrices[instanceRef.instanceID];

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
    else
    {
        float4x4 m = _instanceMatrices[instanceRef.instanceID];

        float3 center = cullingData.boundingBox.min;
        float3 extents = cullingData.boundingBox.max;

        extents = float3(extents.x, extents.y, extents.z);
        center = float3(center.x, center.y, center.z);

        const float3x3 absMatrix = float3x3(abs(m[0].xyz), abs(m[1].xyz), abs(m[2].xyz));
        float3 transformedExtents = mul(extents, absMatrix);

        transformedExtents = float3(transformedExtents.x, transformedExtents.y, transformedExtents.z);

        // Transform to min/max AABB representation
        aabb.min = center - extents;
        aabb.max = center + extents;
    }

    // Calculate bounding sphere from AABB
    float4 sphere;
    sphere.xyz = (aabb.min + aabb.max) / 2.0f;
    sphere.w = distance(aabb.min, aabb.max) / 2.0f;

    // Load DrawCalls instanceCount
    uint instanceCount = GetDrawCallInstanceCount(instanceRef.drawID);

    // Set up DrawInput
    DrawInput drawInput;
    drawInput.csInput = input;
    drawInput.instanceCount = instanceCount;
    drawInput.sphere = sphere;
    drawInput.aabb = aabb;
    drawInput.instanceMatrix = instanceMatrix; // TODO: Can I get rid of this?
    drawInput.shouldOcclusionCull = _constants.occlusionCull;
    drawInput.instanceRefID = instanceRefID;

#if USE_BITMASKS
    // Set up BitmaskInput
    BitmaskInput bitmaskInput;
    bitmaskInput.useBitmasks = true;
    bitmaskInput.prevCulledDrawCallsBitMask = _prevCulledDrawCallsBitMask; // TODO: These are not per drawcall anymore, they are per InstanceRef
    bitmaskInput.outCulledDrawCallsBitMask = _culledDrawCallsBitMask;
#endif

    // Set up CullOutput
    CullOutput cullOutput;
    cullOutput.countIndex = instanceRef.drawID; // Used to index into instanceCounts
    cullOutput.instanceCounts = _culledInstanceCounts;
    cullOutput.baseInstanceLookup = GetBaseInstanceLookup(instanceRef.drawID); // Used to index into instanceLookupTable, + atomic_add(instanceCounts[drawCallID])
    cullOutput.instanceLookupTable = _culledInstanceLookupTable;
    
    // Cull Main Camera
    {
        CullForCamera(drawInput,
            _cameras[0],
#if USE_BITMASKS
            bitmaskInput,
#endif
            cullOutput);
    }
}