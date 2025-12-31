
struct PSInput
{
	float4 color : Color;
};

[shader("fragment")]
float4 main(PSInput input) : SV_Target0
{
	return input.color;
}