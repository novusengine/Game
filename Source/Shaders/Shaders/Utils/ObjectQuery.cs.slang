
#include "DescriptorSet/Model.inc.slang"

#include "Include/VisibilityBuffers.inc.slang"
#include "Include/TypedVisibility.inc.slang"
#include "Model/ModelVisibility.inc.slang"
#include "Model/ModelShared.inc.slang"
#include "Terrain/TerrainShared.inc.slang"

struct Constants
{
    uint2 requests[15];
    uint numRequests;
    uint resourceIndex;
};

struct ObjectData
{
    uint type;
    uint value;
    uint generation;
    uint reserved;
};

[[vk::push_constant]] Constants _constants;
[[vk::binding(0, PER_PASS)]] RWStructuredBuffer<ObjectData> _result;
[[vk::binding(4, PER_PASS)]] StructuredBuffer<ModelVisibilityRecord> _queryModelVisibilityRecords0;
[[vk::binding(5, PER_PASS)]] StructuredBuffer<ModelVisibilityRecord> _queryModelVisibilityRecords1;
[[vk::binding(6, PER_PASS)]] StructuredBuffer<ModelInstanceRecord> _queryModelInstances;

[shader("compute")]
[numthreads(1, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint numRequests = _constants.numRequests;

    for (uint i = 0; i < numRequests; i++)
    {
        uint2 pixelPos = _constants.requests[i];

        uint vBufferData = LoadVisibilityBuffer(pixelPos);
        _result[i].generation = 0u;
        _result[i].reserved = 0u;
        if (IsModelVisibility(vBufferData))
        {
            TypedVisibilityPayload payload = UnpackTypedVisibility(vBufferData);
            ModelVisibilityRecord packedRecord = _constants.resourceIndex == 0u
                ? _queryModelVisibilityRecords0[payload.recordID]
                : _queryModelVisibilityRecords1[payload.recordID];
            DecodedModelVisibilityRecord record = DecodeModelVisibilityRecord(packedRecord);
            ModelInstanceRecord instance = _queryModelInstances[record.instanceIndex];
            _result[i].type = 4u;
            _result[i].value = record.instanceIndex;
            _result[i].generation = instance.generation;
            continue;
        }
        VisibilityBuffer vBuffer = UnpackVisibilityBuffer(vBufferData);

        _result[i].type = vBuffer.typeID;
        _result[i].reserved = 0xFFFFFFFFu;

        uint objectID = 0;
        if (vBuffer.typeID == ObjectType::Terrain)
        {
            InstanceData instanceData = _instanceDatas[vBuffer.instanceID];
            objectID = instanceData.packedChunkCellID;
        }
        else if (vBuffer.typeID == ObjectType::ModelOpaque)
        {
            InstanceRef instanceRef = _opaqueInstanceRefTable[vBuffer.instanceID];
            objectID = instanceRef.instanceID;
        }

        _result[i].value = objectID;
    }
}
