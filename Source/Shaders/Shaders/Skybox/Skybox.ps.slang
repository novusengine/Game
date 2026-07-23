
#include "DescriptorSet/Global.inc.slang"

#include "Include/Common.inc.slang"

struct SkybandColors
{
    float4 top;
    float4 middle;
    float4 bottom;
    float4 aboveHorizon;
    float4 horizon;
    float4 sunDirection; // xyz = direction to the sun, w = enabled
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

    // Sun disc where the view ray aligns with the directional light
    if (_skybandColors.sunDirection.w > 0.0f)
    {
        // Same NDC convention as ScreenToView in Culling.inc.slang
        float2 ndc = float2(input.uv.x, 1.0f - input.uv.y) * 2.0f - 1.0f;

        // Build the ray in view space (camera at origin) and rotate it to world as a direction,
        // going through clipToWorld instead subtracts two huge world positions and the
        // cancellation noise makes the disc shimmer and facet
        float4 viewPos = mul(float4(ndc, 1.0f, 1.0f), _cameras[0].clipToView); // Reversed Z, depth 1 is the near plane
        float3 viewRay = normalize(viewPos.xyz / viewPos.w);
        float3 viewDir = normalize(mul(float4(viewRay, 0.0f), _cameras[0].viewToWorld).xyz);

        float cosSun = dot(viewDir, _skybandColors.sunDirection.xyz);

        const float discEdge = 0.9993f; // ~2.1 degree angular radius
        const float glowEdge = 0.997f;

        float disc = smoothstep(discEdge, discEdge + 0.0003f, cosSun);
        float glow = saturate((cosSun - glowEdge) / (1.0f - glowEdge));
        glow = glow * glow * 0.3f;

        const float3 sunColor = float3(1.0f, 0.95f, 0.8f);
        color.rgb = lerp(color.rgb, sunColor, saturate(disc + glow));
    }

    PSOutput output;
    output.color = float4(color.rgb, 1.0f);
    return output;
}