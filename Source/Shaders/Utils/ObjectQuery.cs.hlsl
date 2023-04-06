#include "Include/VisibilityBuffers.inc.hlsl"
#include "Terrain/Shared.inc.hlsl"

struct Constants
{
    uint2 requests[15];
    uint numRequests;
};

struct ObjectData
{
    uint type;
    uint value;
};

[[vk::push_constant]] Constants _constants;
[[vk::binding(0, PER_PASS)]] RWStructuredBuffer<ObjectData> _result;

[numthreads(1, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint numRequests = _constants.numRequests;

    for (uint i = 0; i < numRequests; i++)
    {
        uint2 pixelPos = _constants.requests[i];

        uint4 vBufferData = LoadVisibilityBuffer(pixelPos);
        VisibilityBuffer vBuffer = UnpackVisibilityBuffer(vBufferData);

        _result[i].type = vBuffer.typeID;

        uint objectID = 0;
        if (vBuffer.typeID == ObjectType::Terrain)
        {
            InstanceData instanceData = _instanceDatas[vBuffer.drawID];
            objectID = instanceData.packedChunkCellID;
        }
        else
        {
            objectID = vBuffer.drawID;//GetObjectID(vBuffer.typeID, vBuffer.drawID);
        }

        _result[i].value = objectID;
    }
}