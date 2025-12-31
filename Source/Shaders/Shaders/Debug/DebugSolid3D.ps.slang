
struct PSInput
{
	float3 normal : TEXCOORD0;
	float4 color : Color;
};

[shader("fragment")]
float4 main(PSInput input) : SV_Target0
{
	float4 color = input.color;
	//if (color.a > 0.5f)
	{
		float light = clamp(dot(input.normal, normalize(float3(1, 1, 0))), 0.5f, 1.0f);
		color.rgb = color.rgb * light;
	}

	return float4(color.rgb, 1.0f);
}