struct PSInput
{
    float3 color : COLOR;
};

[shader("fragment")]
float4 main(PSInput input) : SV_Target0
{
    return float4(input.color, 1.0f);
}
