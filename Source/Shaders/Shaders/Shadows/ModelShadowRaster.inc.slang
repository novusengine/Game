#ifndef MODEL_SHADOW_RASTER_INCLUDED
#define MODEL_SHADOW_RASTER_INCLUDED

#include "DescriptorSet/Global.inc.slang"
#include "Shadows/ModelShadow.inc.slang"

struct Constants
{
    uint queueIndex;
    uint frameIndex;
    uint dynamicCaster;
};
[[vk::push_constant]] Constants _constants;

[[vk::binding(0, PER_PASS)]] StructuredBuffer<uint> _shadowRasterQueue0_0;
[[vk::binding(1, PER_PASS)]] StructuredBuffer<uint> _shadowRasterQueue1_0;
[[vk::binding(2, PER_PASS)]] StructuredBuffer<uint> _shadowRasterQueue2_0;
[[vk::binding(3, PER_PASS)]] StructuredBuffer<uint> _shadowRasterQueue3_0;
[[vk::binding(4, PER_PASS)]] StructuredBuffer<uint> _shadowRasterQueue4_0;
[[vk::binding(5, PER_PASS)]] StructuredBuffer<uint> _shadowRasterQueue5_0;
[[vk::binding(6, PER_PASS)]] StructuredBuffer<uint> _shadowRasterQueue6_0;
[[vk::binding(7, PER_PASS)]] StructuredBuffer<uint> _shadowRasterQueue7_0;
[[vk::binding(8, PER_PASS)]] StructuredBuffer<uint> _shadowRasterQueue0_1;
[[vk::binding(9, PER_PASS)]] StructuredBuffer<uint> _shadowRasterQueue1_1;
[[vk::binding(10, PER_PASS)]] StructuredBuffer<uint> _shadowRasterQueue2_1;
[[vk::binding(11, PER_PASS)]] StructuredBuffer<uint> _shadowRasterQueue3_1;
[[vk::binding(12, PER_PASS)]] StructuredBuffer<uint> _shadowRasterQueue4_1;
[[vk::binding(13, PER_PASS)]] StructuredBuffer<uint> _shadowRasterQueue5_1;
[[vk::binding(14, PER_PASS)]] StructuredBuffer<uint> _shadowRasterQueue6_1;
[[vk::binding(15, PER_PASS)]] StructuredBuffer<uint> _shadowRasterQueue7_1;
[[vk::binding(16, PER_PASS)]] StructuredBuffer<ModelShadowRecord> _shadowRasterRecords0;
[[vk::binding(17, PER_PASS)]] StructuredBuffer<ModelShadowRecord> _shadowRasterRecords1;
[[vk::binding(18, PER_PASS)]] StructuredBuffer<ModelInstanceRecord> _shadowRasterInstances;
[[vk::binding(19, PER_PASS)]] StructuredBuffer<ModelRecord> _shadowRasterModels;
[[vk::binding(20, PER_PASS)]] StructuredBuffer<ModelMesh> _shadowRasterMeshes;
[[vk::binding(21, PER_PASS)]] StructuredBuffer<ModelLOD> _shadowRasterLODs;
[[vk::binding(22, PER_PASS)]] StructuredBuffer<ModelSubmesh> _shadowRasterSubmeshes;
[[vk::binding(23, PER_PASS)]] StructuredBuffer<ModelMeshlet> _shadowRasterMeshlets;
[[vk::binding(24, PER_PASS)]] StructuredBuffer<uint2> _shadowRasterPositions;
[[vk::binding(25, PER_PASS)]] StructuredBuffer<uint> _shadowRasterVertexIndices;
[[vk::binding(26, PER_PASS)]] StructuredBuffer<uint> _shadowRasterTriangles;

uint LoadModelShadowRecordIndex(uint queueOffset)
{
    if (_constants.frameIndex == 0u)
    {
        if (_constants.queueIndex == 0u) return _shadowRasterQueue0_0[queueOffset];
        if (_constants.queueIndex == 1u) return _shadowRasterQueue1_0[queueOffset];
        if (_constants.queueIndex == 2u) return _shadowRasterQueue2_0[queueOffset];
        if (_constants.queueIndex == 3u) return _shadowRasterQueue3_0[queueOffset];
        if (_constants.queueIndex == 4u) return _shadowRasterQueue4_0[queueOffset];
        if (_constants.queueIndex == 5u) return _shadowRasterQueue5_0[queueOffset];
        if (_constants.queueIndex == 6u) return _shadowRasterQueue6_0[queueOffset];
        return _shadowRasterQueue7_0[queueOffset];
    }
    if (_constants.queueIndex == 0u) return _shadowRasterQueue0_1[queueOffset];
    if (_constants.queueIndex == 1u) return _shadowRasterQueue1_1[queueOffset];
    if (_constants.queueIndex == 2u) return _shadowRasterQueue2_1[queueOffset];
    if (_constants.queueIndex == 3u) return _shadowRasterQueue3_1[queueOffset];
    if (_constants.queueIndex == 4u) return _shadowRasterQueue4_1[queueOffset];
    if (_constants.queueIndex == 5u) return _shadowRasterQueue5_1[queueOffset];
    if (_constants.queueIndex == 6u) return _shadowRasterQueue6_1[queueOffset];
    return _shadowRasterQueue7_1[queueOffset];
}

ModelShadowRecord LoadModelShadowRecord(uint recordIndex)
{
    return _constants.frameIndex == 0u ? _shadowRasterRecords0[recordIndex] : _shadowRasterRecords1[recordIndex];
}

#endif
