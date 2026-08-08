#include "Include/VisibilityBuffers.inc.slang"
#include "Include/TypedVisibility.inc.slang"
#include "DescriptorSet/Material.inc.slang"
#include "Model/ModelVisibilityReconstruct.inc.slang"

struct Constants
{
    float4 renderInfo;
    uint resourceIndex;
    uint tileCapacity;
    uint tilesX;
};
[[vk::push_constant]] Constants _constants;

[[vk::binding(3, PER_PASS)]] RWTexture2D<uint> _materialIDs;
[[vk::binding(21, PER_PASS)]] RWStructuredBuffer<uint> _materialTileQueue0;
[[vk::binding(22, PER_PASS)]] RWStructuredBuffer<uint> _materialTileQueue1;
[[vk::binding(23, PER_PASS)]] RWStructuredBuffer<uint> _materialCounters0;
[[vk::binding(24, PER_PASS)]] RWStructuredBuffer<uint> _materialCounters1;

groupshared uint _groupMask;

void AddCounter(uint index, uint value, out uint previous)
{
    if (_constants.resourceIndex == 0u) InterlockedAdd(_materialCounters0[index], value, previous);
    else InterlockedAdd(_materialCounters1[index], value, previous);
}

void StoreTile(uint index, uint tile)
{
    if (_constants.resourceIndex == 0u) _materialTileQueue0[index] = tile;
    else _materialTileQueue1[index] = tile;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 groupID : SV_GroupID, uint3 groupThreadID : SV_GroupThreadID,
          uint3 dispatchThreadID : SV_DispatchThreadID)
{
    if (all(groupThreadID.xy == uint2(0u)))
        _groupMask = 0u;
    GroupMemoryBarrierWithGroupSync();

    uint2 pixelPos = dispatchThreadID.xy;
    if (all(pixelPos < uint2(_constants.renderInfo.xy)))
    {
        uint visibility = LoadVisibilityBuffer(pixelPos);
        if (IsModelVisibility(visibility))
        {
            TypedVisibilityPayload payload = UnpackTypedVisibility(visibility);
            ModelVisibilityRecord packedRecord;
            if (LoadResolvedModelRecord(payload.recordID, _constants.resourceIndex, packedRecord))
            {
                DecodedModelVisibilityRecord record = DecodeModelVisibilityRecord(packedRecord);
                ModelInstanceRecord instance = _resolvedModelInstances[record.instanceIndex];
                ModelRecord model = _resolvedModels[instance.modelIndex];
                ModelMesh mesh = _resolvedModelMeshes[model.meshBase + record.meshIndex];
                ModelLOD lod = _resolvedModelLODs[model.meshLODBase + mesh.lodOffset + record.lodIndex];
                ModelSubmesh submesh = _resolvedModelSubmeshes[model.submeshBase + lod.submeshOffset + record.submeshIndex];
                uint instanceIndex = _resolvedModelMaterialTable[instance.materialTableOffset +
                    mesh.materialSlotOffset + submesh.materialSlotIndex];
                MaterialInstanceRecord materialInstance = _materialInstances[instanceIndex];
                uint executionGroup = MaterialExecutionGroup(materialInstance);
                if (executionGroup < MATERIAL_EXECUTION_GROUP_COUNT)
                {
                    _materialIDs[pixelPos] = MaterialHandle(materialInstance);
                    InterlockedOr(_groupMask, 1u << executionGroup);
                    uint ignored;
                    AddCounter(MATERIAL_EXECUTION_GROUP_COUNT + executionGroup, 1u, ignored);
                }
            }
        }
    }

    GroupMemoryBarrierWithGroupSync();
    if (all(groupThreadID.xy == uint2(0u)))
    {
        uint tile = groupID.x + groupID.y * _constants.tilesX;
        for (uint group = 0u; group < MATERIAL_EXECUTION_GROUP_COUNT; ++group)
        {
            if ((_groupMask & (1u << group)) == 0u)
                continue;
            uint queueOffset;
            AddCounter(group, 1u, queueOffset);
            if (queueOffset < _constants.tileCapacity)
                StoreTile(group * _constants.tileCapacity + queueOffset, tile);
        }
    }
}
