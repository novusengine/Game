
#include "common.inc.hlsl"

struct CharDrawData
{
    uint4 packed0; // x: textureIndex, y: charIndex, z: textColor, w: borderColor
    float4 packed1; // x: borderSize, y: borderFade
};
[[vk::binding(1, PER_PASS)]] StructuredBuffer<CharDrawData> _charDrawDatas;

[[vk::binding(2, PER_PASS)]] SamplerState _sampler;
[[vk::binding(3, PER_PASS)]] Texture2D<float4> _fontTextures[4096];

struct VertexOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    uint charDrawDataID : TEXCOORD1;
};

float4 main(VertexOutput input) : SV_Target
{
    CharDrawData drawData = _charDrawDatas[input.charDrawDataID];

    uint textureIndex = drawData.packed0.x;
    uint packedTextColor = drawData.packed0.z;
    uint packedBorderColor = drawData.packed0.w;

    float4 textColor = PackedUnormsToFloat4(packedTextColor);
    float4 borderColor = PackedUnormsToFloat4(packedBorderColor);

    float distance = _fontTextures[textureIndex].SampleLevel(_sampler, input.uv, 0).r;

    const float fontOnEdgeValue = 192.0f;
    const float fontBorderSteps = 6.0f; // TODO: When we have font objects, we need to pass this from the CPU
    const float fontBorderValue = fontOnEdgeValue / fontBorderSteps; // 36.0f

    float borderSize = drawData.packed1.x;
    float borderFade = drawData.packed1.y;
    float borderSteps = borderFade * fontBorderSteps;
    bool hasBorder = borderSize > 0.0;

    return float4(0, 0, 1, 1);

    if (distance <= ((1.0 - borderSize) * hasBorder))
    {
        discard;
    }

    bool isOpaque = distance == 1.0 || !hasBorder;
    float comparisonValue = ((1.0 - borderFade) * borderSize) * !isOpaque;
    bool isOutline = !isOpaque && distance < comparisonValue && hasBorder && borderFade != 1.0;
    bool isFade = !isOutline && distance < 1.0 && borderFade > 0.0;

    // isOpaque
    // Color is textColor.rgb
    // Alpha is 1.0

    // IsOutline
    // Between edge of character and middle of border
    // Fade between transparent and border color

    // !IsOutline
    // Between middle of border and inside of character
    // Fade between border color and text color

    // Non Branchless Version
    /*
        float comparisonValue = ((1.0 - borderFade) * borderSize);
        if (distance < comparisonValue))
        {
            // Between edge of character and middle of border
            // Fade between transparent and border color

            float newAlpha = Map(distance, 0.0, comparisonValue, 0.0, 1.0);
            rgb = float3(newAlpha, newAlpha, newAlpha) * borderColor.rgb;
            alpha = newAlpha;
        }
        else if (distance < 1.0)
        {
            // Between middle of border and inside of character
            // Fade between border color and text color

            float fade = Map(distance, comparisonValue, 1.0, 0.0, 1.0);
            rgb = lerp(borderColor.rgb, rgb, fade);
        }
    */

    float newAlpha = Map(distance, 1.0 * isOpaque, comparisonValue, 0.0, 1.0);
    float fade = Map(distance, comparisonValue, 1.0, 0.0, 1.0);

    float3 opaqueColor = textColor.rgb * isOpaque;
    float3 outlineColor = (float3(newAlpha, newAlpha, newAlpha) * borderColor.rgb) * isOutline;
    float3 fadeColor = lerp(borderColor.rgb, textColor.rgb, fade) * isFade;
    float opaqueAlpha = 1.0 * (isOpaque || isFade);
    float outlineAlpha = (newAlpha * isOutline);

    float3 rgb = opaqueColor + outlineColor + fadeColor;
    float alpha = opaqueAlpha + outlineAlpha;

    return float4(rgb, alpha);
}