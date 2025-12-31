
#include "DescriptorSet/Global.inc.slang"

#include "Include/Common.inc.slang"

struct SkybandColors
{
    float4 top;
    float4 middle;
    float4 bottom;
    float4 aboveHorizon;
    float4 horizon;
};
[[vk::push_constant]] SkybandColors _skybandColors;

struct VertexOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

struct PSOutput
{
    float4 color : SV_Target0;
};

[shader("fragment")]
PSOutput main(VertexOutput input) : SV_Target
{
    float3 rotation = _cameras[0].eyeRotation.xyz;

    float fovY = 90.0f;
    float halfFovY = fovY / 2.0f;

    float uvRotationOffset = ((1.0f - input.uv.y) * fovY) - halfFovY;
    float val = (-rotation.y + uvRotationOffset + 89.0f) / 178.0f;
    val = clamp(val, 0.0f, 1.0f);

    float4 color = float4(0, 0, 0, 0);
    if (val < 0.50f)
    {
        color = _skybandColors.horizon;
    }
    else if (val < 0.515f)
    {
        float blendFactor = Map(val, 0.50f, 0.515f, 0.0f, 1.0f);
        color = lerp(_skybandColors.horizon, _skybandColors.aboveHorizon, blendFactor);
    }
    else if (val < 0.60f)
    {
        float blendFactor = Map(val, 0.515f, 0.60f, 0.0f, 1.0f);
        color = lerp(_skybandColors.aboveHorizon, _skybandColors.bottom, blendFactor);
    }
    else if (val < 0.75f)
    {
        float blendFactor = Map(val, 0.60f, 0.75f, 0.0f, 1.0f);
        color = lerp(_skybandColors.bottom, _skybandColors.middle, blendFactor);
    }
    else
    {
        float blendFactor = Map(val, 0.75f, 1.0f, 0.0f, 1.0f);
        color = lerp(_skybandColors.middle, _skybandColors.top, blendFactor);
    }

    PSOutput output;
    output.color = float4(color.rgb, 1.0f);
    return output;
}