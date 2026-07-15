permutation EDITOR_PASS = [0, 1];
permutation SHADOW_PASS = [0, 1];
permutation SVSM_PASS = [0, 1]; // 1 adds per-draw clip-rect distances for the SVSM page render
#define GEOMETRY_PASS 1

#include "DescriptorSet/Global.inc.slang"

#include "Terrain/TerrainShared.inc.slang"
#if SVSM_PASS
#include "Shadows/SVSM.inc.slang"
#endif

struct Constants
{
    uint viewIndex;
#if SVSM_PASS
    uint svsmRectIndex; // Which clip rect this draw renders, SVSM_CLIP_RECT_DISABLED = no clipping
#endif
};

[[vk::push_constant]] Constants _constants;

#if SVSM_PASS
[[vk::binding(0, PER_PASS)]] StructuredBuffer<SVSMData> _svsmData; // Same binding the SVSM pixel shader declares
#endif

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
#if SVSM_PASS
    float4 clipDistances : SV_ClipDistance; // Fixed-function culls fragments outside the draw's clip rect
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
#if SVSM_PASS
        output.clipDistances = float4(-1.0f, -1.0f, -1.0f, -1.0f);
#endif
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
#if SVSM_PASS
    output.clipDistances = SVSMRectClipDistances(vertex.position, _constants.viewIndex - 1, _constants.svsmRectIndex, _cameras[0].eyePosition.xyz, _svsmData);
#endif

    return output;
}
