permutation SUPPORTS_EXTENDED_TEXTURES = [0, 1];
#include "common.inc.hlsl"
#include "globalData.inc.hlsl"
#include "Model/Shared.inc.hlsl"

struct VSInput
{
    uint vertexID : SV_VertexID;
    uint instanceID : SV_InstanceID;
};

struct VSOutput
{
    float4 position : SV_Position;

    nointerpolation uint drawCallID : TEXCOORD0;
    float4 uv01 : TEXCOORD1;
};

VSOutput main(VSInput input)
{
    uint drawCallID = input.instanceID;
    ModelVertex vertex = LoadModelVertex(input.vertexID);

    ModelDrawCallData drawCallData = LoadModelDrawCallData(drawCallID);
    ModelInstanceData instanceData = _modelInstanceDatas[drawCallData.instanceID];
    float4x4 instanceMatrix = _modelInstanceMatrices[drawCallData.instanceID];

    float4 position = float4(vertex.position, 1.0f);
    position = mul(position, instanceMatrix);

    float4 UVs = vertex.uv01;

    // Pass data to pixelshader
    VSOutput output;
    output.position = mul(position, _cameras[0].worldToClip);
    output.drawCallID = drawCallID;
    output.uv01 = UVs;

    return output;
}