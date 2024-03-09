permutation EDITOR_PASS = [0, 1];
permutation SHADOW_PASS = [0, 1];
permutation SUPPORTS_EXTENDED_TEXTURES = [0, 1];
#define GEOMETRY_PASS 1

#include "globalData.inc.hlsl"
#include "Terrain/Shared.inc.hlsl"

struct Constants
{
    uint cascadeIndex;
};

[[vk::push_constant]] Constants _constants;

struct VSInput
{
    uint vertexID : SV_VertexID;
    uint instanceID : SV_InstanceID;
};

struct VSOutput
{
    float4 position : SV_Position;
#if !EDITOR_PASS && !SHADOW_PASS
    uint instanceID : TEXCOORD0;
    float3 worldPosition : TEXCOORD1;
#endif
};

VSOutput main(VSInput input)
{
    InstanceData instanceData = _instanceDatas[input.instanceID];

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

#if SHADOW_PASS
    output.position = float4(0, 0, 0, 1);// mul(float4(vertex.position, 1.0f), GetShadowViewProjectionMatrix(_constants.cascadeIndex));
#else
    output.position = mul(float4(vertex.position, 1.0f), _cameras[0].worldToClip);
#endif

#if !EDITOR_PASS && !SHADOW_PASS
    output.instanceID = input.instanceID;
    output.worldPosition = vertex.position;
#endif

    return output;
}