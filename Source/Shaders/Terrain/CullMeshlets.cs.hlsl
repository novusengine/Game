#include "Terrain/Shared.inc.hlsl"

struct PaddedDispatch
{
    uint numSurvivedChunks;
    uint x;
    uint y;
    uint z;
};

[[vk::binding(0, PER_DRAW)]] StructuredBuffer<ChunkData> _chunkDatas;
[[vk::binding(1, PER_DRAW)]] StructuredBuffer<MeshletData> _meshletDatas;
[[vk::binding(2, PER_DRAW)]] StructuredBuffer<PaddedDispatch> _dispatchArguments;

[[vk::binding(3, PER_DRAW)]] RWStructuredBuffer<uint> _culledIndices;
[[vk::binding(4, PER_DRAW)]] RWStructuredBuffer<SurvivedChunkData> _survivedChunkDatas;
[[vk::binding(5, PER_DRAW)]] RWStructuredBuffer<SurvivedMeshletData> _survivedMeshletDatas;
[[vk::binding(6, PER_DRAW)]] RWByteAddressBuffer _meshletChunkCount;
[[vk::binding(7, PER_DRAW)]] RWByteAddressBuffer _drawArguments;

struct CSInput
{
    int3 dispatchThreadID : SV_DispatchThreadID;
};

[numthreads(64, 1, 1)]
void main(CSInput input)
{
    uint survivedChunkID = input.dispatchThreadID.x;
    uint numSurvivedChunks = _dispatchArguments[0].numSurvivedChunks;

    if (survivedChunkID >= numSurvivedChunks)
        return;

    SurvivedChunkData survivedChunkData = _survivedChunkDatas[survivedChunkID];
    ChunkData chunkData = _chunkDatas[survivedChunkData.chunkDataID];

    uint offset;
    uint meshletBaseChunkID;
    _meshletChunkCount.InterlockedAdd(0, chunkData.meshletCount, meshletBaseChunkID);

    for (int i = 0; i < chunkData.meshletCount; i++)
    {
        uint meshletIndex = chunkData.meshletOffset + i;
        MeshletData meshletData = _meshletDatas[meshletIndex];

        // Do Per Meshlet Culling Here
        {

        }

        uint meshletChunkID = meshletBaseChunkID + i;

        SurvivedMeshletData survivedMeshletData;
        survivedMeshletData.meshletDataID = meshletIndex;
        survivedMeshletData.chunkDataID = survivedChunkData.chunkDataID;
        _survivedMeshletDatas[meshletChunkID] = survivedMeshletData;

        _drawArguments.InterlockedAdd(0, meshletData.indexCount, offset);

        for (uint j = 0; j < meshletData.indexCount; j++)
        {
            uint packedData = meshletChunkID | (j << 23);
            _culledIndices[offset + j] = packedData;
        }
    }
}