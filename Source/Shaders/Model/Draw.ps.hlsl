struct VSOutput
{
    float3 normal : TEXCOORD0;
    float2 uv : TEXCOORD1;
};

float4 main(VSOutput input) : SV_Target0
{
    //return float4(input.uv, 0.0f, 1.0f);
    return float4(1.0f, 0.0f, 0.0f, 1.0f);
}