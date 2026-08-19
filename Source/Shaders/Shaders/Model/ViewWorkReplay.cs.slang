#include "DescriptorSet/Global.inc.slang"
#include "Model/ModelVisibility.inc.slang"

struct Constants
{
    uint queueCapacity;
    uint resourceIndex;
    uint resetHistory;
    uint reserved;
};
[[vk::push_constant]] Constants _constants;

[[vk::binding(0, PER_PASS)]] StructuredBuffer<ModelInstanceRecord> _modelInstances;
[[vk::binding(1, PER_PASS)]] StructuredBuffer<ModelRecord> _modelRecords;
[[vk::binding(2, PER_PASS)]] StructuredBuffer<uint> _lodHistory;
[[vk::binding(3, PER_PASS)]] RWStructuredBuffer<uint> _instanceVisibility0;
[[vk::binding(4, PER_PASS)]] RWStructuredBuffer<uint> _instanceVisibility1;
[[vk::binding(5, PER_PASS)]] RWStructuredBuffer<uint> _meshletHistory0;
[[vk::binding(6, PER_PASS)]] RWStructuredBuffer<uint> _meshletHistory1;
[[vk::binding(7, PER_PASS)]] RWStructuredBuffer<ModelMeshletSurvivor> _survivors0;
[[vk::binding(8, PER_PASS)]] RWStructuredBuffer<ModelMeshletSurvivor> _survivors1;
[[vk::binding(9, PER_PASS)]] RWStructuredBuffer<uint> _oneSidedQueue0;
[[vk::binding(10, PER_PASS)]] RWStructuredBuffer<uint> _twoSidedQueue0;
[[vk::binding(11, PER_PASS)]] RWStructuredBuffer<uint> _alphaTestOneSidedQueue0;
[[vk::binding(12, PER_PASS)]] RWStructuredBuffer<uint> _alphaTestTwoSidedQueue0;
[[vk::binding(13, PER_PASS)]] RWStructuredBuffer<uint> _oneSidedQueue1;
[[vk::binding(14, PER_PASS)]] RWStructuredBuffer<uint> _twoSidedQueue1;
[[vk::binding(15, PER_PASS)]] RWStructuredBuffer<uint> _alphaTestOneSidedQueue1;
[[vk::binding(16, PER_PASS)]] RWStructuredBuffer<uint> _alphaTestTwoSidedQueue1;
[[vk::binding(17, PER_PASS)]] RWStructuredBuffer<ModelVisibilityRecord> _visibilityRecords0;
[[vk::binding(18, PER_PASS)]] RWStructuredBuffer<ModelVisibilityRecord> _visibilityRecords1;
[[vk::binding(19, PER_PASS)]] RWStructuredBuffer<uint> _workStats0;
[[vk::binding(20, PER_PASS)]] RWStructuredBuffer<uint> _workStats1;
[[vk::binding(21, PER_PASS)]] StructuredBuffer<ModelMeshlet> _modelMeshlets;

void AddStat(uint index, uint value, out uint previous)
{
    if (_constants.resourceIndex == 0u) InterlockedAdd(_workStats0[index], value, previous);
    else InterlockedAdd(_workStats1[index], value, previous);
}

void AddStat(uint index, uint value)
{
    uint ignored;
    AddStat(index, value, ignored);
}

bool LoadBit(RWStructuredBuffer<uint> bits, uint bitIndex)
{
    return (bits[bitIndex >> 5u] & (1u << (bitIndex & 31u))) != 0u;
}

ModelMeshletSurvivor LoadPreviousSurvivor(uint index)
{
    return _constants.resourceIndex == 0u ? _survivors1[index] : _survivors0[index];
}

bool LoadCurrentInstanceVisibility(uint instanceIndex)
{
    uint wordCount;
    uint wordStride;
    if (_constants.resourceIndex == 0u) _instanceVisibility0.GetDimensions(wordCount, wordStride);
    else _instanceVisibility1.GetDimensions(wordCount, wordStride);
    if ((instanceIndex >> 5u) >= wordCount)
        return false;
    return _constants.resourceIndex == 0u ? LoadBit(_instanceVisibility0, instanceIndex) :
                                            LoadBit(_instanceVisibility1, instanceIndex);
}

bool LoadPreviousMeshletHistory(uint bitIndex)
{
    uint wordCount;
    uint wordStride;
    if (_constants.resourceIndex == 0u) _meshletHistory1.GetDimensions(wordCount, wordStride);
    else _meshletHistory0.GetDimensions(wordCount, wordStride);
    if ((bitIndex >> 5u) >= wordCount)
        return false;
    return _constants.resourceIndex == 0u ? LoadBit(_meshletHistory1, bitIndex) :
                                            LoadBit(_meshletHistory0, bitIndex);
}

void MarkCurrentMeshletHistory(uint bitIndex)
{
    uint wordIndex = bitIndex >> 5u;
    uint bit = 1u << (bitIndex & 31u);
    if (_constants.resourceIndex == 0u) InterlockedOr(_meshletHistory0[wordIndex], bit);
    else InterlockedOr(_meshletHistory1[wordIndex], bit);
}

void StoreCurrentSurvivor(uint index, ModelMeshletSurvivor survivor)
{
    if (_constants.resourceIndex == 0u) _survivors0[index] = survivor;
    else _survivors1[index] = survivor;
}

void StoreVisibilityRecord(uint index, ModelVisibilityRecord record)
{
    if (_constants.resourceIndex == 0u) _visibilityRecords0[index] = record;
    else _visibilityRecords1[index] = record;
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

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    if (_constants.resetHistory != 0u)
        return;
    uint previousCount = _constants.resourceIndex == 0u ? _workStats1[MODEL_WORK_STAT_SURVIVOR_COUNT] :
                                                          _workStats0[MODEL_WORK_STAT_SURVIVOR_COUNT];
    if (DispatchThreadID.x >= min(previousCount, _constants.queueCapacity))
        return;

    uint boundSurvivorCount;
    uint boundSurvivorStride;
    if (_constants.resourceIndex == 0u)
        _survivors1.GetDimensions(boundSurvivorCount, boundSurvivorStride);
    else
        _survivors0.GetDimensions(boundSurvivorCount, boundSurvivorStride);
    if (DispatchThreadID.x >= boundSurvivorCount)
        return;
    ModelMeshletSurvivor survivor = LoadPreviousSurvivor(DispatchThreadID.x);
    uint boundInstanceCount;
    uint boundInstanceStride;
    _modelInstances.GetDimensions(boundInstanceCount, boundInstanceStride);
    if (survivor.instanceIndex >= boundInstanceCount)
    {
        AddStat(MODEL_WORK_STAT_STALE_GENERATION_SKIPS, 1u);
        return;
    }
    ModelInstanceRecord instance = _modelInstances[survivor.instanceIndex];
    if (instance.generation != survivor.instanceGeneration)
    {
        AddStat(MODEL_WORK_STAT_STALE_GENERATION_SKIPS, 1u);
        return;
    }
    if (!LoadCurrentInstanceVisibility(survivor.instanceIndex))
        return;
    uint boundLODHistoryCount;
    uint boundLODHistoryStride;
    _lodHistory.GetDimensions(boundLODHistoryCount, boundLODHistoryStride);
    if (survivor.lodHistoryIndex >= boundLODHistoryCount)
        return;
    if (_lodHistory[survivor.lodHistoryIndex] != survivor.lodIndex)
    {
        AddStat(MODEL_WORK_STAT_LOD_MISMATCH_SKIPS, 1u);
        return;
    }

    uint boundModelCount;
    uint boundModelStride;
    _modelRecords.GetDimensions(boundModelCount, boundModelStride);
    if (instance.modelIndex >= boundModelCount)
        return;
    ModelRecord model = _modelRecords[instance.modelIndex];
    uint meshletIndex = model.meshletBase + survivor.meshletIndex;
    uint boundMeshletCount;
    uint boundMeshletStride;
    _modelMeshlets.GetDimensions(boundMeshletCount, boundMeshletStride);
    if (meshletIndex >= boundMeshletCount)
        return;

    uint historyBit = instance.meshletHistoryWordOffset * 32u + survivor.meshletIndex;
    if (!LoadPreviousMeshletHistory(historyBit))
        return;

    uint queueOffset;
    uint recordOffset;
    uint survivorOffset;
    AddStat(MODEL_WORK_STAT_RASTER_MESHLETS_BASE + survivor.rasterClass, 1u, queueOffset);
    AddStat(MODEL_WORK_STAT_VISIBILITY_RECORDS, 1u, recordOffset);
    AddStat(MODEL_WORK_STAT_SURVIVOR_COUNT, 1u, survivorOffset);
    if (queueOffset >= _constants.queueCapacity || recordOffset >= _constants.queueCapacity ||
        survivorOffset >= _constants.queueCapacity)
    {
        if (survivorOffset < _constants.queueCapacity)
        {
            ModelMeshletSurvivor invalidSurvivor = (ModelMeshletSurvivor)0;
            StoreCurrentSurvivor(survivorOffset, invalidSurvivor);
        }
        if (queueOffset >= _constants.queueCapacity) AddStat(MODEL_WORK_STAT_QUEUE_OVERFLOWS, 1u);
        if (recordOffset >= _constants.queueCapacity) AddStat(MODEL_WORK_STAT_VISIBILITY_RECORD_OVERFLOWS, 1u);
        if (survivorOffset >= _constants.queueCapacity) AddStat(MODEL_WORK_STAT_SURVIVOR_QUEUE_OVERFLOWS, 1u);
        return;
    }

    StoreVisibilityRecord(recordOffset, PackModelVisibilityRecord(survivor.instanceIndex, survivor.meshIndex,
                                                                  survivor.lodIndex, survivor.submeshIndex,
                                                                  survivor.meshletIndex));
    StoreWork(survivor.rasterClass, queueOffset, recordOffset);
    StoreCurrentSurvivor(survivorOffset, survivor);
    MarkCurrentMeshletHistory(historyBit);
    AddStat(MODEL_WORK_STAT_COMMITTED_RASTER_MESHLETS_BASE + survivor.rasterClass, 1u);
    uint triangleCount = ModelMeshletTriangleCount(_modelMeshlets[meshletIndex]);
    AddStat(MODEL_WORK_STAT_COMMITTED_TRIANGLES, triangleCount);
    AddStat(MODEL_WORK_STAT_PHASE1_REPLAYED_TRIANGLES, triangleCount);
    AddStat(MODEL_WORK_STAT_PHASE1_REPLAYED_MESHLETS, 1u);
}
