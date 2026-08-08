#include "DescriptorSet/Global.inc.slang"
#include "Model/ModelVisibility.inc.slang"

struct Constants
{
    uint viewIndex;
    uint queueCapacity;
    uint resourceIndex;
    uint enableConeCulling;
};
[[vk::push_constant]] Constants _constants;

[[vk::binding(0, PER_PASS)]] StructuredBuffer<ModelMeshletChunk> _chunkQueue0;
[[vk::binding(1, PER_PASS)]] StructuredBuffer<ModelInstanceRecord> _modelInstances;
[[vk::binding(2, PER_PASS)]] StructuredBuffer<ModelRecord> _modelRecords;
[[vk::binding(3, PER_PASS)]] StructuredBuffer<ModelMesh> _modelMeshes;
[[vk::binding(4, PER_PASS)]] StructuredBuffer<ModelMeshlet> _modelMeshlets;
[[vk::binding(5, PER_PASS)]] RWStructuredBuffer<uint> _oneSidedQueue0;
[[vk::binding(6, PER_PASS)]] RWStructuredBuffer<uint> _twoSidedQueue0;
[[vk::binding(7, PER_PASS)]] RWStructuredBuffer<uint> _alphaTestOneSidedQueue0;
[[vk::binding(8, PER_PASS)]] RWStructuredBuffer<uint> _alphaTestTwoSidedQueue0;
[[vk::binding(9, PER_PASS)]] RWStructuredBuffer<uint> _workStats0;
[[vk::binding(10, PER_PASS)]] StructuredBuffer<ModelMeshletChunk> _chunkQueue1;
[[vk::binding(11, PER_PASS)]] RWStructuredBuffer<uint> _oneSidedQueue1;
[[vk::binding(12, PER_PASS)]] RWStructuredBuffer<uint> _twoSidedQueue1;
[[vk::binding(13, PER_PASS)]] RWStructuredBuffer<uint> _alphaTestOneSidedQueue1;
[[vk::binding(14, PER_PASS)]] RWStructuredBuffer<uint> _alphaTestTwoSidedQueue1;
[[vk::binding(15, PER_PASS)]] RWStructuredBuffer<uint> _workStats1;
[[vk::binding(16, PER_PASS)]] RWStructuredBuffer<ModelVisibilityRecord> _visibilityRecords0;
[[vk::binding(17, PER_PASS)]] RWStructuredBuffer<ModelVisibilityRecord> _visibilityRecords1;

void AddStat(uint index, uint value)
{
    if (_constants.resourceIndex == 0u) InterlockedAdd(_workStats0[index], value);
    else InterlockedAdd(_workStats1[index], value);
}

void AddStat(uint index, uint value, out uint previous)
{
    if (_constants.resourceIndex == 0u) InterlockedAdd(_workStats0[index], value, previous);
    else InterlockedAdd(_workStats1[index], value, previous);
}

ModelMeshletChunk LoadChunk(uint index)
{
    return _constants.resourceIndex == 0u ? _chunkQueue0[index] : _chunkQueue1[index];
}

void StoreWork(uint rasterClass, uint index, uint recordID)
{
    if (_constants.resourceIndex == 0u)
    {
        if (rasterClass == MODEL_RASTER_SOLID_ONE_SIDED) _oneSidedQueue0[index] = recordID;
        else if (rasterClass == MODEL_RASTER_SOLID_TWO_SIDED) _twoSidedQueue0[index] = recordID;
        else if (rasterClass == MODEL_RASTER_ALPHA_TEST_ONE_SIDED) _alphaTestOneSidedQueue0[index] = recordID;
        else _alphaTestTwoSidedQueue0[index] = recordID;
    }
    else
    {
        if (rasterClass == MODEL_RASTER_SOLID_ONE_SIDED) _oneSidedQueue1[index] = recordID;
        else if (rasterClass == MODEL_RASTER_SOLID_TWO_SIDED) _twoSidedQueue1[index] = recordID;
        else if (rasterClass == MODEL_RASTER_ALPHA_TEST_ONE_SIDED) _alphaTestOneSidedQueue1[index] = recordID;
        else _alphaTestTwoSidedQueue1[index] = recordID;
    }
}

void StoreVisibilityRecord(uint index, ModelVisibilityRecord record)
{
    if (_constants.resourceIndex == 0u) _visibilityRecords0[index] = record;
    else _visibilityRecords1[index] = record;
}

[shader("compute")]
[numthreads(MODEL_CHUNK_MESHLET_COUNT, 1, 1)]
void main(uint3 GroupID : SV_GroupID, uint3 GroupThreadID : SV_GroupThreadID)
{
    ModelMeshletChunk chunk = LoadChunk(GroupID.x);
    uint meshletCount = chunk.packedCountRasterAndLOD & MODEL_CHUNK_MESHLET_COUNT_MASK;
    if (GroupThreadID.x == 0u)
    {
        AddStat(MODEL_WORK_STAT_TESTED_CHUNKS, 1u);
        AddStat(MODEL_WORK_STAT_TESTED_MESHLETS, meshletCount);
    }

    uint meshletIndex = chunk.meshletBase + GroupThreadID.x;
    ModelInstanceRecord instance = _modelInstances[chunk.instanceIndex];
    Camera camera = _cameras[_constants.viewIndex];
    uint rasterClass =
        (chunk.packedCountRasterAndLOD >> MODEL_CHUNK_RASTER_CLASS_SHIFT) & MODEL_CHUNK_RASTER_CLASS_MASK;
    bool visible = GroupThreadID.x < meshletCount;
    ModelMeshlet meshlet;
    if (visible)
    {
        meshlet = _modelMeshlets[meshletIndex];
        float4 sphere = TransformSphere(meshlet.boundsCenter, meshlet.boundsRadius, instance.currentWorld);
        if (!ModelSphereInsideFrustum(camera, sphere))
        {
            AddStat(MODEL_WORK_STAT_REJECTED_FRUSTUM_MESHLETS, 1u);
            visible = false;
        }
        else if (_constants.enableConeCulling != 0u && (rasterClass & 1u) == 0u &&
                 ModelConeBackfacing(meshlet.packedNormalCone, meshlet.boundsCenter,
                                                          meshlet.boundsRadius, instance.currentWorld,
                                                          camera.eyePosition.xyz))
        {
            AddStat(MODEL_WORK_STAT_REJECTED_CONE_MESHLETS, 1u);
            visible = false;
        }
    }

    uint visibleCount = WaveActiveCountBits(visible);
    uint waveOffset = 0u;
    uint recordOffset = 0u;
    uint committedCount = 0u;
    uint countIndex = MODEL_WORK_STAT_RASTER_MESHLETS_BASE + rasterClass;
    if (WaveIsFirstLane())
    {
        AddStat(countIndex, visibleCount, waveOffset);
        AddStat(MODEL_WORK_STAT_VISIBILITY_RECORDS, visibleCount, recordOffset);
        uint queueCommitted = min(visibleCount, _constants.queueCapacity - min(waveOffset, _constants.queueCapacity));
        uint recordCommitted = min(visibleCount, _constants.queueCapacity - min(recordOffset, _constants.queueCapacity));
        committedCount = min(queueCommitted, recordCommitted);
        if (queueCommitted < visibleCount)
            AddStat(MODEL_WORK_STAT_QUEUE_OVERFLOWS, visibleCount - queueCommitted);
        if (recordCommitted < visibleCount)
            AddStat(MODEL_WORK_STAT_VISIBILITY_RECORD_OVERFLOWS, visibleCount - recordCommitted);
    }
    waveOffset = WaveReadLaneFirst(waveOffset);
    recordOffset = WaveReadLaneFirst(recordOffset);
    committedCount = WaveReadLaneFirst(committedCount);
    if (WaveIsFirstLane() && committedCount > 0u)
        AddStat(MODEL_WORK_STAT_COMMITTED_RASTER_MESHLETS_BASE + rasterClass, committedCount);

    if (!visible)
        return;
    uint waveIndex = WavePrefixCountBits(visible);
    if (waveIndex >= committedCount)
        return;

    ModelRecord model = _modelRecords[instance.modelIndex];
    uint localLOD = (chunk.packedCountRasterAndLOD >> MODEL_CHUNK_LOD_SHIFT) & MODEL_CHUNK_LOD_MASK;
    uint submeshIndex = (chunk.packedCountRasterAndLOD >> MODEL_CHUNK_SUBMESH_SHIFT) & MODEL_CHUNK_SUBMESH_MASK;
    uint recordID = recordOffset + waveIndex;
    StoreVisibilityRecord(recordID, PackModelVisibilityRecord(chunk.instanceIndex, chunk.meshIndex, localLOD,
                                                               submeshIndex, meshletIndex - model.meshletBase));
    StoreWork(rasterClass, waveOffset + waveIndex, recordID);
}
