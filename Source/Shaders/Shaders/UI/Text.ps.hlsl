
#include "common.inc.hlsl"

struct CharDrawData
{
    uint4 packed0; // x: textureIndex, y: charIndex, z: textColor, w: borderColor
    float4 packed1; // x: borderSize, y: padding, zw: unitRangeXY
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

float median(float a, float b, float c)
{
    return max(min(a, b), min(max(a, b), c));
}

float screenPxRange(float2 uv, float2 unitRange) 
{
    float2 screenTexSize = float2(1.0f, 1.0f) / fwidth(uv);
    return max(0.5 * dot(unitRange, screenTexSize), 1.0);
}

float4 main(VertexOutput input) : SV_Target
{
    CharDrawData drawData = _charDrawDatas[input.charDrawDataID];

    uint textureIndex = drawData.packed0.x;
    uint packedTextColor = drawData.packed0.z;
    uint packedBorderColor = drawData.packed0.w;

    float4 textColor = PackedUnormsToFloat4(packedTextColor);
    float4 borderColor = PackedUnormsToFloat4(packedBorderColor);

    float borderSize = drawData.packed1.x;
    float2 unitRange = drawData.packed1.zw;

    float4 distances = _fontTextures[textureIndex].Sample(_sampler, input.uv).rgba;

    const float roundedInlines = 0.0f;
    const float roundedOutlines = 1.0f;
    const float outBias = 1.0 / 4.0;
    
    float distMsdf = median(distances.r, distances.g, distances.b);
    float distSdf = distances.a; // mtsdf format only
    distMsdf = min(distMsdf, distSdf + 0.1f); // HACK: to fix glitch in msdf near edges, see https://www.redblobgames.com/x/2404-distance-field-effects/

    // Blend between sharp and rounded corners
    float distInner = lerp(distMsdf, distSdf, roundedInlines);
    float distOuter = lerp(distMsdf, distSdf, roundedOutlines);

    // Typically 0.5 is the threshold, > 0.5 is inside, < 0.5 is outside
    const float threshold = 0.5f;
    float width = screenPxRange(input.uv, unitRange);

    float inner = width * (distInner - threshold) + 0.5f + outBias;
    float outer = width * (distOuter - threshold) + 0.5f + outBias + borderSize;

    float innerOpacity = saturate(inner);
    float4 innerColor = textColor;
    float outerOpacity = saturate(outer);
    float4 outerColor = float4(borderColor.rgb, 1.0f);

    float4 color = (innerColor * innerOpacity) + (outerColor * (outerOpacity - innerOpacity));
    return color;
}