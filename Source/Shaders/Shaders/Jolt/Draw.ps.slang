
#include "Include/Common.inc.slang"

struct PSInput
{
	float3 normal : TEXCOORD0;
	float4 color : Color;
};

struct PSOutput
{
	uint4 vBuffer : SV_Target0;
};

[shader("fragment")]
PSOutput main(PSInput input) : SV_Target0
{
	float4 color = input.color;
	//if (color.a > 0.5f)
	{
		float light = clamp(dot(input.normal, normalize(float3(1, 1, 0))), 0.5f, 1.0f);
		color.rgb = saturate(color.rgb) * light;
	}

	PSOutput output;
	output.vBuffer.xzw = 0;
	output.vBuffer.x |= uint(ObjectType::JoltDebug) << 28;
	output.vBuffer.y = Float4ToPackedUnorms(color);
	return output;
}