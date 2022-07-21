permutation WIREFRAME = [0, 1];

#include "Model/Shared.inc.hlsl"

struct VSOutput
{
    float3 normal : TEXCOORD0;
    float2 uv : TEXCOORD1;
    nointerpolation uint meshletID : TEXCOORD2;
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
    float3 color = IDToColor3(input.meshletID);
    return float4(color, 1.0f);
#endif
}