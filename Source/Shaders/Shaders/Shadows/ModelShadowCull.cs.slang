#include "DescriptorSet/Global.inc.slang"
#include "Shadows/ModelShadow.inc.slang"
#include "Shadows/SVSM.inc.slang"

struct Constants
{
    uint queueCapacity;
    uint frameIndex;
};
[[vk::push_constant]] Constants _constants;

[[vk::binding(0, PER_PASS)]] StructuredBuffer<ModelShadowChunk> _shadowCullChunks0;
[[vk::binding(1, PER_PASS)]] StructuredBuffer<ModelInstanceRecord> _shadowCullInstances;
[[vk::binding(2, PER_PASS)]] StructuredBuffer<ModelRecord> _shadowCullModels;
[[vk::binding(3, PER_PASS)]] StructuredBuffer<ModelMeshlet> _shadowCullMeshlets;
[[vk::binding(4, PER_PASS)]] StructuredBuffer<SVSMData> _shadowCullSVSMData;
[[vk::binding(5, PER_PASS)]] RWStructuredBuffer<ModelShadowRecord> _shadowRecords0;
[[vk::binding(6, PER_PASS)]] RWStructuredBuffer<uint> _shadowCullStats0;
[[vk::binding(7, PER_PASS)]] StructuredBuffer<ModelShadowChunk> _shadowCullChunks1;
[[vk::binding(8, PER_PASS)]] RWStructuredBuffer<ModelShadowRecord> _shadowRecords1;
[[vk::binding(9, PER_PASS)]] RWStructuredBuffer<uint> _shadowCullStats1;
[[vk::binding(10, PER_PASS)]] RWStructuredBuffer<uint> _shadowQueue0_0;
[[vk::binding(11, PER_PASS)]] RWStructuredBuffer<uint> _shadowQueue1_0;
[[vk::binding(12, PER_PASS)]] RWStructuredBuffer<uint> _shadowQueue2_0;
[[vk::binding(13, PER_PASS)]] RWStructuredBuffer<uint> _shadowQueue3_0;
[[vk::binding(14, PER_PASS)]] RWStructuredBuffer<uint> _shadowQueue4_0;
[[vk::binding(15, PER_PASS)]] RWStructuredBuffer<uint> _shadowQueue5_0;
[[vk::binding(16, PER_PASS)]] RWStructuredBuffer<uint> _shadowQueue6_0;
[[vk::binding(17, PER_PASS)]] RWStructuredBuffer<uint> _shadowQueue7_0;
[[vk::binding(18, PER_PASS)]] RWStructuredBuffer<uint> _shadowQueue0_1;
[[vk::binding(19, PER_PASS)]] RWStructuredBuffer<uint> _shadowQueue1_1;
[[vk::binding(20, PER_PASS)]] RWStructuredBuffer<uint> _shadowQueue2_1;
[[vk::binding(21, PER_PASS)]] RWStructuredBuffer<uint> _shadowQueue3_1;
[[vk::binding(22, PER_PASS)]] RWStructuredBuffer<uint> _shadowQueue4_1;
[[vk::binding(23, PER_PASS)]] RWStructuredBuffer<uint> _shadowQueue5_1;
[[vk::binding(24, PER_PASS)]] RWStructuredBuffer<uint> _shadowQueue6_1;
[[vk::binding(25, PER_PASS)]] RWStructuredBuffer<uint> _shadowQueue7_1;

void AddStat(uint index, uint value)
{
    if (_constants.frameIndex == 0u) InterlockedAdd(_shadowCullStats0[index], value);
    else InterlockedAdd(_shadowCullStats1[index], value);
}

void AddStat(uint index, uint value, out uint previous)
{
    if (_constants.frameIndex == 0u) InterlockedAdd(_shadowCullStats0[index], value, previous);
    else InterlockedAdd(_shadowCullStats1[index], value, previous);
}

ModelShadowChunk LoadChunk(uint index)
{
    return _constants.frameIndex == 0u ? _shadowCullChunks0[index] : _shadowCullChunks1[index];
}

uint LoadStat(uint index)
{
    return _constants.frameIndex == 0u ? _shadowCullStats0[index] : _shadowCullStats1[index];
}

void StoreRecord(uint index, ModelShadowRecord record)
{
    if (_constants.frameIndex == 0u) _shadowRecords0[index] = record;
    else _shadowRecords1[index] = record;
}

void StoreQueue(uint queueIndex, uint index, uint recordIndex)
{
    if (_constants.frameIndex == 0u)
    {
        if (queueIndex == 0u) _shadowQueue0_0[index] = recordIndex;
        else if (queueIndex == 1u) _shadowQueue1_0[index] = recordIndex;
        else if (queueIndex == 2u) _shadowQueue2_0[index] = recordIndex;
        else if (queueIndex == 3u) _shadowQueue3_0[index] = recordIndex;
        else if (queueIndex == 4u) _shadowQueue4_0[index] = recordIndex;
        else if (queueIndex == 5u) _shadowQueue5_0[index] = recordIndex;
        else if (queueIndex == 6u) _shadowQueue6_0[index] = recordIndex;
        else _shadowQueue7_0[index] = recordIndex;
    }
    else
    {
        if (queueIndex == 0u) _shadowQueue0_1[index] = recordIndex;
        else if (queueIndex == 1u) _shadowQueue1_1[index] = recordIndex;
        else if (queueIndex == 2u) _shadowQueue2_1[index] = recordIndex;
        else if (queueIndex == 3u) _shadowQueue3_1[index] = recordIndex;
        else if (queueIndex == 4u) _shadowQueue4_1[index] = recordIndex;
        else if (queueIndex == 5u) _shadowQueue5_1[index] = recordIndex;
        else if (queueIndex == 6u) _shadowQueue6_1[index] = recordIndex;
        else _shadowQueue7_1[index] = recordIndex;
    }
}

bool IntersectsRect(int2 meshletMin, int2 meshletMax, int2 rectMin, int2 rectMax)
{
    return all(rectMax >= rectMin) && all(meshletMax >= rectMin) && all(meshletMin <= rectMax);
}

bool IntersectsPages(float4 sphere, uint clipmapIndex, bool dynamicCaster)
{
    int2 pageMin;
    int2 pageMax;
    int2 anchor = int2(_shadowCullSVSMData[0].anchorPageMinX[clipmapIndex],
                       _shadowCullSVSMData[0].anchorPageMinY[clipmapIndex]);
    if (!SVSMComputeAABBPageRect(sphere.xyz - sphere.www, sphere.xyz + sphere.www,
                                 _shadowCullSVSMData[0].prevLightDirection.xyz,
                                 _shadowCullSVSMData[0].pageWorld[clipmapIndex], anchor,
                                 _shadowCullSVSMData[0].configPageTableSize, pageMin, pageMax))
        return false;

    if (dynamicCaster)
    {
        int2 rectMin = int2(_shadowCullSVSMData[0].dynamicRectMinX[clipmapIndex],
                            _shadowCullSVSMData[0].dynamicRectMinY[clipmapIndex]);
        int2 rectMax = int2(_shadowCullSVSMData[0].dynamicRectMaxX[clipmapIndex],
                            _shadowCullSVSMData[0].dynamicRectMaxY[clipmapIndex]);
        return IntersectsRect(pageMin, pageMax, rectMin, rectMax);
    }

    for (uint rect = 0u; rect < 3u; ++rect)
    {
        uint slot = rect * MODEL_SHADOW_MAX_CLIPMAPS + clipmapIndex;
        int2 rectMin = int2(_shadowCullSVSMData[0].clipRectMinX[slot],
                            _shadowCullSVSMData[0].clipRectMinY[slot]);
        int2 rectMax = int2(_shadowCullSVSMData[0].clipRectMaxX[slot],
                            _shadowCullSVSMData[0].clipRectMaxY[slot]);
        if (IntersectsRect(pageMin, pageMax, rectMin, rectMax))
            return true;
    }
    return false;
}

[shader("compute")]
[numthreads(MODEL_SHADOW_CHUNK_MESHLET_COUNT, 1, 1)]
void main(uint3 groupID : SV_GroupID, uint3 groupThreadID : SV_GroupThreadID)
{
    uint chunkIndex = groupID.x + groupID.y * MODEL_SHADOW_COMPUTE_DISPATCH_MAX_GROUPS_X;
    uint chunkCount = min(LoadStat(MODEL_SHADOW_STAT_EXPANDED_CHUNKS), _constants.queueCapacity);
    if (chunkIndex >= chunkCount)
        return;

    ModelShadowChunk chunk = LoadChunk(chunkIndex);
    uint count = chunk.packedCountRasterLODAndClipmap & MODEL_SHADOW_CHUNK_COUNT_MASK;
    if (groupThreadID.x == 0u)
        AddStat(MODEL_SHADOW_STAT_TESTED_MESHLETS, count);
    if (groupThreadID.x >= count)
        return;

    uint rasterClass = (chunk.packedCountRasterLODAndClipmap >> MODEL_SHADOW_CHUNK_RASTER_SHIFT) & 3u;
    uint lodIndex = (chunk.packedCountRasterLODAndClipmap >> MODEL_SHADOW_CHUNK_LOD_SHIFT) & 7u;
    uint clipmapIndex = (chunk.packedCountRasterLODAndClipmap >> MODEL_SHADOW_CHUNK_CLIPMAP_SHIFT) & 7u;
    bool dynamicCaster = chunk.dynamicCaster != 0u;
    uint meshletIndex = chunk.meshletBase + groupThreadID.x;
    ModelInstanceRecord instance = _shadowCullInstances[chunk.instanceIndex];
    ModelRecord model = _shadowCullModels[instance.modelIndex];
    ModelMeshlet meshlet = _shadowCullMeshlets[meshletIndex];
    AddStat(MODEL_SHADOW_STAT_TESTED_TRIANGLES, ModelMeshletTriangleCount(meshlet));

    float4 sphere = TransformSphere(meshlet.boundsCenter, meshlet.boundsRadius, instance.currentWorld);
    Camera camera = _cameras[clipmapIndex + 1u];
    if (!ModelSphereInsideFrustum(camera, sphere))
    {
        AddStat(MODEL_SHADOW_STAT_REJECTED_FRUSTUM, 1u);
        return;
    }
    if (!IntersectsPages(sphere, clipmapIndex, dynamicCaster))
    {
        AddStat(MODEL_SHADOW_STAT_REJECTED_PAGE, 1u);
        return;
    }
    uint recordIndex;
    AddStat(MODEL_SHADOW_STAT_VISIBILITY_RECORDS, 1u, recordIndex);
    uint queueIndex = ModelShadowQueueIndex(rasterClass, dynamicCaster);
    uint queueOffset;
    AddStat(MODEL_SHADOW_STAT_QUEUE_COUNTS_BASE + queueIndex, 1u, queueOffset);
    if (recordIndex >= _constants.queueCapacity || queueOffset >= _constants.queueCapacity)
    {
        if (recordIndex >= _constants.queueCapacity) AddStat(MODEL_SHADOW_STAT_RECORD_OVERFLOWS, 1u);
        if (queueOffset >= _constants.queueCapacity) AddStat(MODEL_SHADOW_STAT_QUEUE_OVERFLOWS, 1u);
        return;
    }

    uint localMeshletIndex = meshletIndex - model.meshletBase;
    ModelVisibilityRecord packed = PackModelVisibilityRecord(chunk.instanceIndex, chunk.meshIndex, lodIndex,
                                                              chunk.submeshIndex, localMeshletIndex);
    ModelShadowRecord record;
    record.packedInstanceAndMesh = packed.packedInstanceAndMesh;
    record.packedLODSubmeshAndMeshlet = packed.packedLODSubmeshAndMeshlet;
    record.clipmapIndex = clipmapIndex;
    record.reserved = 0u;
    StoreRecord(recordIndex, record);
    StoreQueue(queueIndex, queueOffset, recordIndex);
    AddStat(MODEL_SHADOW_STAT_SURVIVING_BASE + clipmapIndex * MODEL_SHADOW_QUEUE_COUNT + queueIndex, 1u);
    AddStat(MODEL_SHADOW_STAT_COMMITTED_TRIANGLES, ModelMeshletTriangleCount(meshlet));
}
