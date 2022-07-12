
struct VSOutput
{
	float4 position : SV_POSITION;
	float3 normal : TEXCOORD0;
};

float4 main(VSOutput input) : SV_Target
{
	return float4(input.normal, 1.0f);
}