
#include "Include/Common.inc.slang"

struct CharDrawData
{
    uint4 packed0; // x: textureIndex & charIndex, y: clipMaskTextureIndex, z: textColor, w: borderColor
    float4 packed1; // x: borderSize, y: padding, zw: unitRangeXY
    uint4 packed2; // x: clipRegionMinXY, y: clipRegionMaxXY, z: clipMaskRegionMinXY, w: clipMaskRegionMaxXY
    int4 packed3; // x: worldPositionIndex, yzw: unused
};
[[vk::binding(2, PER_PASS)]] StructuredBuffer<CharDrawData> _charDrawDatas;

[[vk::binding(3, PER_PASS)]] SamplerState _sampler;
[[vk::binding(4, PER_PASS)]] Texture2D<float4> _fontTextures[4096];
[[vk::binding(5, PER_PASS)]] Texture2D<float4> _textures[4096];

struct VertexOutput
{
    float4 position : SV_POSITION;
    float4 uvAndScreenPos : TEXCOORD0;
    uint charDrawDataID : TEXCOORD1;
};

float Median(float a, float b, float c)
{
    return max(min(a, b), min(max(a, b), c));
}

float ScreenPxRange(float2 uv, float2 unitRange) 
{
    float2 screenTexSize = float2(1.0f, 1.0f) / fwidth(uv);
    return max(0.5 * dot(unitRange, screenTexSize), 1.0);
}

bool ShouldDiscard(float2 pos, float2 clipMin, float2 clipMax)
{
    // Check if the position is outside the clip rect
    return pos.x < clipMin.x || pos.x > clipMax.x || pos.y < clipMin.y || pos.y > clipMax.y;
}

[shader("fragment")]
float4 main(VertexOutput input) : SV_Target
{
    //return float4(1.0f, 0.0f, 0.0f, 0.3f);
    CharDrawData drawData = _charDrawDatas[input.charDrawDataID];

    float2 screenPos = input.uvAndScreenPos.zw;
    float2 clipRegionMin = float2(f16tof32(drawData.packed2.x), f16tof32(drawData.packed2.x >> 16));
    float2 clipRegionMax = float2(f16tof32(drawData.packed2.y), f16tof32(drawData.packed2.y >> 16));
    if (ShouldDiscard(screenPos, clipRegionMin, clipRegionMax))
    {
        //return float4(1.0f, 0.0f, 0.0f, 0.3f);
        discard;
    }

    uint textureIndex = drawData.packed0.x & 0xFFFF;

    uint packedTextColor = drawData.packed0.z;
    uint packedBorderColor = drawData.packed0.w;

    float4 textColor = PackedUnormsToFloat4(packedTextColor);
    float4 borderColor = PackedUnormsToFloat4(packedBorderColor);

    float borderSize = drawData.packed1.x;
    float2 unitRange = drawData.packed1.zw;

    float4 distances = _fontTextures[textureIndex].Sample(_sampler, input.uvAndScreenPos.xy).rgba;

    const float roundedInlines = 0.0f;
    const float roundedOutlines = 1.0f;
    const float outBias = 1.0 / 4.0;
    
    float distMsdf = Median(distances.r, distances.g, distances.b);
    float distSdf = distances.a; // mtsdf format only
    distMsdf = min(distMsdf, distSdf + 0.1f); // HACK: to fix glitch in msdf near edges, see https://www.redblobgames.com/x/2404-distance-field-effects/

    // Blend between sharp and rounded corners
    float distInner = lerp(distMsdf, distSdf, roundedInlines);
    float distOuter = lerp(distMsdf, distSdf, roundedOutlines);

    // Typically 0.5 is the threshold, > 0.5 is inside, < 0.5 is outside
    const float threshold = 0.5f;
    float width = ScreenPxRange(input.uvAndScreenPos.xy, unitRange);

    float inner = width * (distInner - threshold) + 0.5f + outBias;
    float outer = width * (distOuter - threshold) + 0.5f + outBias + borderSize;

    float innerOpacity = saturate(inner);
    float4 innerColor = textColor;
    float outerOpacity = saturate(outer);
    float4 outerColor = float4(borderColor.rgb, 1.0f);

    float4 color = (innerColor * innerOpacity) + (outerColor * (outerOpacity - innerOpacity));

    // Apply the clipMask
    float2 clipMaskRegionMin = float2(f16tof32(drawData.packed2.z), f16tof32(drawData.packed2.z >> 16));
    float2 clipMaskRegionMax = float2(f16tof32(drawData.packed2.w), f16tof32(drawData.packed2.w >> 16));
    float2 maskUV = (screenPos - clipMaskRegionMin) / (clipMaskRegionMax - clipMaskRegionMin);

    uint clipMaskTextureIndex = drawData.packed0.y;
    float clipMask = _textures[clipMaskTextureIndex].Sample(_sampler, maskUV).a;
    if (clipMask < 0.5f)
    {
        discard;
    }
    color.a *= clipMask;

    // Multiply the color channels by alpha for pre-multiplied alpha output
    color.rgb *= color.a;

    return saturate(color);
}