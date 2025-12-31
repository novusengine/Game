permutation EDITOR_PASS = [0, 1];
permutation SHADOW_PASS = [0, 1];
#define GEOMETRY_PASS 1

#include "DescriptorSet/Global.inc.slang"

#include "Terrain/TerrainShared.inc.slang"

struct Constants
{
    uint viewIndex;
};

[[vk::push_constant]] Constants _constants;

struct VSInput
{
    uint vertexID : SV_VulkanVertexID;
    uint culledInstanceID : SV_VulkanInstanceID;
};

struct VSOutput
{
    float4 position : SV_Position;
#if !EDITOR_PASS && !SHADOW_PASS
    uint instanceID : TEXCOORD0;
#endif
};

[shader("vertex")]
VSOutput main(VSInput input)
{
    InstanceData instanceData = _culledInstanceDatas[input.culledInstanceID];

    VSOutput output;

    CellData cellData = LoadCellData(instanceData.globalCellID);
    if (IsHoleVertex(input.vertexID, cellData.holes))
    {
        const float NaN = asfloat(0b01111111100000000000000000000000);
        output.position = float4(NaN, NaN, NaN, NaN);
        return output;
    }

    const uint cellID = instanceData.packedChunkCellID & 0xFFFF;
    const uint chunkID = instanceData.packedChunkCellID >> 16;

    uint vertexBaseOffset = instanceData.globalCellID * NUM_VERTICES_PER_CELL;
    TerrainVertex vertex = LoadTerrainVertex(chunkID, cellID, vertexBaseOffset, input.vertexID);

    output.position = mul(float4(vertex.position, 1.0f), _cameras[_constants.viewIndex].worldToClip);

#if !EDITOR_PASS && !SHADOW_PASS
    output.instanceID = instanceData.globalCellID;
#endif

    return output;
}