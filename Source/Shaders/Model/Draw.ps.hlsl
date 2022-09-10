permutation WIREFRAME = [0, 1];

#include "Model/Shared.inc.hlsl"

[[vk::binding(4,  PER_DRAW)]] StructuredBuffer<ModelData> _modelDatas;
[[vk::binding(19, PER_DRAW)]] SamplerState _sampler;
[[vk::binding(20, PER_DRAW)]] Texture2D<float4> _textures[4096];

struct VSOutput
{
    float3 normal : TEXCOORD0;
    float2 uv : TEXCOORD1;
    nointerpolation uint meshletDataID : TEXCOORD2;
};

float4 main(VSOutput input) : SV_Target0
{
/*
    const float3 sunDir = normalize(float3(0.0f, -1.0f, 0.0f));
    float light = saturate(dot(input.normal, sunDir));
    
    float3 darkColor = float3(0.1f, 0.1f, 0.0f);
    float3 brightColor = float3(0.5f, 0.5f, 0.0f);
    float3 color = lerp(darkColor, brightColor, light);
    return float4(color, 1.0f);
*/
    
#if WIREFRAME
    return float4(1.0f, 1.0f, 1.0f, 1.0f);
#else
    ModelData modelData = _modelDatas[input.meshletDataID];
    
    uint textureID = (modelData.packedData >> 16) & 0xFFFF;
    float4 texture1 = _textures[NonUniformResourceIndex(textureID)].Sample(_sampler, input.uv.xy);
    
    //float3 color = IDToColor3(input.meshletDataID);
    float3 color = texture1.xyz;
    return float4(color, 1.0f);
#endif
}