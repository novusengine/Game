#include "DescriptorSet/Global.inc.slang"
#include "Include/PyramidCulling.inc.slang"
#include "Model/ModelVisibility.inc.slang"

struct Constants
{
    uint viewIndex;
    uint queueCapacity;
    uint resourceIndex;
    uint enableConeCulling;
    uint enableOcclusionCulling;
    uint viewportWidth;
    uint viewportHeight;
    uint enableTemporal;
    uint showCullReasons;
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
[[vk::binding(18, PER_PASS)]] RWStructuredBuffer<uint> _meshletHistory0;
[[vk::binding(19, PER_PASS)]] RWStructuredBuffer<uint> _meshletHistory1;
[[vk::binding(22, PER_PASS)]] Texture2D<float> _depthPyramid;
[[vk::binding(23, PER_PASS)]] SamplerState _depthSampler;
[[vk::binding(24, PER_PASS)]] RWStructuredBuffer<uint> _cullReasons0;
[[vk::binding(25, PER_PASS)]] RWStructuredBuffer<uint> _cullReasons1;
[[vk::binding(26, PER_PASS)]] RWStructuredBuffer<uint> _geometryOneSidedQueue0;
[[vk::binding(27, PER_PASS)]] RWStructuredBuffer<uint> _geometryTwoSidedQueue0;
[[vk::binding(28, PER_PASS)]] RWStructuredBuffer<uint> _geometryAlphaTestOneSidedQueue0;
[[vk::binding(29, PER_PASS)]] RWStructuredBuffer<uint> _geometryAlphaTestTwoSidedQueue0;
[[vk::binding(30, PER_PASS)]] RWStructuredBuffer<uint> _geometryOneSidedQueue1;
[[vk::binding(31, PER_PASS)]] RWStructuredBuffer<uint> _geometryTwoSidedQueue1;
[[vk::binding(32, PER_PASS)]] RWStructuredBuffer<uint> _geometryAlphaTestOneSidedQueue1;
[[vk::binding(33, PER_PASS)]] RWStructuredBuffer<uint> _geometryAlphaTestTwoSidedQueue1;

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

void StoreGeometryWork(uint rasterClass, uint index, uint recordID)
{
    if (_constants.resourceIndex == 0u)
    {
        if (rasterClass == MODEL_RASTER_SOLID_ONE_SIDED) _geometryOneSidedQueue0[index] = recordID;
        else if (rasterClass == MODEL_RASTER_SOLID_TWO_SIDED) _geometryTwoSidedQueue0[index] = recordID;
        else if (rasterClass == MODEL_RASTER_ALPHA_TEST_ONE_SIDED) _geometryAlphaTestOneSidedQueue0[index] = recordID;
        else _geometryAlphaTestTwoSidedQueue0[index] = recordID;
    }
    else
    {
        if (rasterClass == MODEL_RASTER_SOLID_ONE_SIDED) _geometryOneSidedQueue1[index] = recordID;
        else if (rasterClass == MODEL_RASTER_SOLID_TWO_SIDED) _geometryTwoSidedQueue1[index] = recordID;
        else if (rasterClass == MODEL_RASTER_ALPHA_TEST_ONE_SIDED) _geometryAlphaTestOneSidedQueue1[index] = recordID;
        else _geometryAlphaTestTwoSidedQueue1[index] = recordID;
    }
}

void StoreVisibilityRecord(uint index, ModelVisibilityRecord record)
{
    if (_constants.resourceIndex == 0u) _visibilityRecords0[index] = record;
    else _visibilityRecords1[index] = record;
}

void StoreCullReason(uint index, uint reason)
{
    if (_constants.resourceIndex == 0u) _cullReasons0[index] = reason;
    else _cullReasons1[index] = reason;
}

bool PreviousHistoryVisible(uint bitIndex)
{
    uint wordIndex = bitIndex >> 5u;
    uint bit = 1u << (bitIndex & 31u);
    return _constants.resourceIndex == 0u ? (_meshletHistory1[wordIndex] & bit) != 0u :
                                            (_meshletHistory0[wordIndex] & bit) != 0u;
}

void MarkCurrentHistoryVisible(uint bitIndex)
{
    uint wordIndex = bitIndex >> 5u;
    uint bit = 1u << (bitIndex & 31u);
    if (_constants.resourceIndex == 0u) InterlockedOr(_meshletHistory0[wordIndex], bit);
    else InterlockedOr(_meshletHistory1[wordIndex], bit);
}

[shader("compute")]
[numthreads(16, 1, 1)]
void main(uint3 GroupID : SV_GroupID, uint3 GroupThreadID : SV_GroupThreadID)
{
    ModelMeshletChunk chunk = LoadChunk(GroupID.x);
    uint instanceIndex = ModelVisibilityInstanceIndex(chunk.packedInstanceAndMesh);
    uint meshIndex = ModelVisibilityMeshIndex(chunk.packedInstanceAndMesh);
    uint meshletCount = chunk.packedCountRasterAndLOD & MODEL_CHUNK_MESHLET_COUNT_MASK;
    if (GroupThreadID.x == 0u)
        AddStat(MODEL_WORK_STAT_TESTED_CHUNKS, 1u);

    ModelInstanceRecord instance = _modelInstances[instanceIndex];
    uint rasterClass =
        (chunk.packedCountRasterAndLOD >> MODEL_CHUNK_RASTER_CLASS_SHIFT) & MODEL_CHUNK_RASTER_CLASS_MASK;
    uint resolveClass = (chunk.packedCountRasterAndLOD >> MODEL_CHUNK_RESOLVE_CLASS_SHIFT) & 1u;
    uint outputRasterClass = _constants.showCullReasons != 0u ? MODEL_RASTER_SOLID_TWO_SIDED : rasterClass;
    ModelRecord model = _modelRecords[instance.modelIndex];
    uint boundMeshletCount;
    uint boundMeshletStride;
    _modelMeshlets.GetDimensions(boundMeshletCount, boundMeshletStride);
    for (uint meshletBase = 0u; meshletBase < meshletCount; meshletBase += 16u)
    {
        uint meshletIndex = chunk.meshletBase + meshletBase + GroupThreadID.x;
        uint localMeshletIndex = meshletIndex - model.meshletBase;
        uint historyBit = instance.meshletHistoryWordOffset * 32u + localMeshletIndex;
        bool visible = meshletBase + GroupThreadID.x < meshletCount && meshletIndex < boundMeshletCount;
        uint testedMeshletCount = WaveActiveCountBits(visible);
        if (WaveIsFirstLane() && testedMeshletCount > 0u)
            AddStat(MODEL_WORK_STAT_TESTED_MESHLETS, testedMeshletCount);

        uint cullReason = MODEL_CULL_REASON_VISIBLE;
        ModelMeshlet meshlet;
        if (visible)
        {
            meshlet = _modelMeshlets[meshletIndex];
            AddStat(MODEL_WORK_STAT_TESTED_TRIANGLES, ModelMeshletTriangleCount(meshlet));
            float4 sphere = TransformSphere(meshlet.boundsCenter, meshlet.boundsRadius, instance.currentWorld);
            Camera camera = _cameras[_constants.viewIndex];
            if (!ModelSphereInsideFrustum(camera, sphere))
            {
                AddStat(MODEL_WORK_STAT_REJECTED_FRUSTUM_MESHLETS, 1u);
                cullReason = MODEL_CULL_REASON_FRUSTUM;
            }
            else if (_constants.enableConeCulling != 0u && (rasterClass & 1u) == 0u &&
                     ModelConeBackfacing(meshlet.packedNormalCone, meshlet.boundsCenter, meshlet.boundsRadius,
                                         instance.currentWorld, camera.cullingEyePosition.xyz))
            {
                AddStat(MODEL_WORK_STAT_REJECTED_CONE_MESHLETS, 1u);
                cullReason = MODEL_CULL_REASON_CONE;
            }
            else if (_constants.enableOcclusionCulling != 0u)
            {
                float3 boundsMin = sphere.xyz - sphere.www;
                float3 boundsMax = sphere.xyz + sphere.www;
                bool intersectsNear = IsIntersectingNearZ(boundsMin, boundsMax, camera.cullingWorldToClip);
                if (!intersectsNear && !IsVisible(boundsMin, boundsMax, camera.cullingEyePosition.xyz, _depthPyramid,
                                                   _depthSampler, camera.cullingWorldToClip,
                                                   uint2(camera.cullingViewportSize.xy)))
                {
                    AddStat(MODEL_WORK_STAT_REJECTED_OCCLUSION_MESHLETS, 1u);
                    cullReason = MODEL_CULL_REASON_OCCLUSION;
                }
            }
            visible = _constants.showCullReasons != 0u ? cullReason != MODEL_CULL_REASON_VISIBLE :
                                                         cullReason == MODEL_CULL_REASON_VISIBLE;
        }

        uint visibleCount = WaveActiveCountBits(visible);
        uint waveOffset = 0u;
        uint recordOffset = 0u;
        uint committedCount = 0u;
        uint countIndex = MODEL_WORK_STAT_RASTER_MESHLETS_BASE + outputRasterClass;
        if (WaveIsFirstLane() && visibleCount > 0u)
        {
            AddStat(countIndex, visibleCount, waveOffset);
            AddStat(MODEL_WORK_STAT_VISIBILITY_RECORDS, visibleCount, recordOffset);
            if (_constants.enableTemporal != 0u)
                AddStat(MODEL_WORK_STAT_SURVIVOR_COUNT, visibleCount);
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
            AddStat(MODEL_WORK_STAT_COMMITTED_RASTER_MESHLETS_BASE + outputRasterClass, committedCount);

        uint waveIndex = WavePrefixCountBits(visible);
        bool committed = visible && waveIndex < committedCount;
        bool geometryVisible = committed && (_constants.enableTemporal == 0u || !PreviousHistoryVisible(historyBit));
        uint geometryCount = WaveActiveCountBits(geometryVisible);
        uint geometryOffset = 0u;
        uint geometryCommitted = 0u;
        if (WaveIsFirstLane() && geometryCount > 0u)
        {
            AddStat(MODEL_WORK_STAT_GEOMETRY_MESHLETS_BASE + outputRasterClass, geometryCount, geometryOffset);
            geometryCommitted = min(geometryCount,
                _constants.queueCapacity - min(geometryOffset, _constants.queueCapacity));
            if (geometryCommitted < geometryCount)
                AddStat(MODEL_WORK_STAT_QUEUE_OVERFLOWS, geometryCount - geometryCommitted);
        }
        geometryOffset = WaveReadLaneFirst(geometryOffset);
        geometryCommitted = WaveReadLaneFirst(geometryCommitted);
        uint geometryIndex = WavePrefixCountBits(geometryVisible);

        if (committed)
        {
            uint triangleCount = ModelMeshletTriangleCount(meshlet);
            AddStat(MODEL_WORK_STAT_COMMITTED_TRIANGLES, triangleCount);

            uint localLOD = (chunk.packedCountRasterAndLOD >> MODEL_CHUNK_LOD_SHIFT) & MODEL_CHUNK_LOD_MASK;
            uint submeshIndex = (chunk.packedCountRasterAndLOD >> MODEL_CHUNK_SUBMESH_SHIFT) & MODEL_CHUNK_SUBMESH_MASK;
            uint recordID = recordOffset + waveIndex;
            ModelVisibilityRecord visibility = PackModelVisibilityRecord(instanceIndex, meshIndex, localLOD,
                                                                         submeshIndex, localMeshletIndex);
            visibility.instanceGeneration = instance.generation;
            visibility.replayRevision = instance.replayRevision;
            StoreVisibilityRecord(recordID, visibility);
            if (_constants.showCullReasons != 0u)
                StoreCullReason(recordID, cullReason);
            StoreWork(outputRasterClass, waveOffset + waveIndex, PackModelRasterQueueEntry(recordID, resolveClass));
            if (_constants.enableTemporal != 0u)
            {
                MarkCurrentHistoryVisible(historyBit);
            }
        }
        if (geometryVisible && geometryIndex < geometryCommitted)
        {
            uint recordID = recordOffset + waveIndex;
            StoreGeometryWork(outputRasterClass, geometryOffset + geometryIndex,
                              PackModelRasterQueueEntry(recordID, resolveClass));
            AddStat(MODEL_WORK_STAT_PHASE2_ADDED_MESHLETS, 1u);
            AddStat(MODEL_WORK_STAT_PHASE2_ADDED_TRIANGLES, ModelMeshletTriangleCount(meshlet));
        }
    }
}
