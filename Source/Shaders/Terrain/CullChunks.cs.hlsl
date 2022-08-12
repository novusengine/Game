#include "Terrain/Shared.inc.hlsl"

struct Constants
{
    uint numChunks;
};
[[vk::push_constant]] Constants _constants;

[[vk::binding(0, PER_DRAW)]] StructuredBuffer<ChunkData> _chunkDatas;
[[vk::binding(2, PER_DRAW)]] StructuredBuffer<uint64_t> _dispatchArguments;

[[vk::binding(3, PER_DRAW)]] RWStructuredBuffer<SurvivedChunkData> _survivedChunkDatas;

struct CSInput
{
    int3 dispatchThreadID : SV_DispatchThreadID;
};

[numthreads(64, 1, 1)]
void main(CSInput input)
{
    uint chunkDataID = input.dispatchThreadID.x;
    if (chunkDataID >= _constants.numChunks)
        return;

    ChunkData chunkData = _chunkDatas[chunkDataID];

    // Do Instance Culling Here
    {

    }

    uint64_t meshletCount = chunkData.meshletCount;
    uint64_t value = 1 | (meshletCount << 32);
    uint64_t originalValue;
    InterlockedAdd(_dispatchArguments[0], value, originalValue);

    uint survivedChunkDataID = uint(originalValue);

    SurvivedChunkData survivedChunkData;
    survivedChunkData.chunkDataID = chunkDataID;
    survivedChunkData.numMeshletsBeforeChunk = uint(originalValue >> 32);

    _survivedChunkDatas[survivedChunkDataID] = survivedChunkData;
}