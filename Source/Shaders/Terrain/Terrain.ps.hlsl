
struct VSOutput
{
	float4 position : SV_POSITION;
	float3 color : TEXCOORD0;
};

float4 main(VSOutput input) : SV_Target
{
	return float4(input.color.rgb, 1.0f);
}