
#include "Include/Common.inc.slang"

#define WIDGET_TYPE_PANEL 0u
#define WIDGET_TYPE_TEXT  1u

struct WidgetDrawData
{
    uint4 packed0; // x: type, y: vertexBase, z: clipMaskTextureIndex, w: worldPositionIndex (int reinterpret)
    uint4 packed1; // Panel: x: textureIndex & additiveTextureIndex, z: color, w: textureScaleToWidgetSize (half2). Text: x: fontTextureIndex, z: textColor, w: borderColor
    float4 texCoord;            // Panel only
    float4 slicingCoord;        // Panel only
    float4 cornerRadiusAndBorder; // Panel: xy: cornerRadius. Text: x: borderSize, zw: unitRange
    uint4 packed2; // x: clipRegionMinXY, y: clipRegionMaxXY, z: clipMaskRegionMinXY, w: clipMaskRegionMaxXY
};
[[vk::binding(2, PER_PASS)]] StructuredBuffer<WidgetDrawData> _widgetDrawDatas;
[[vk::binding(3, PER_PASS)]] SamplerState _sampler;
[[vk::binding(4, PER_PASS)]] Texture2D<float4> _textures[4096];
[[vk::binding(5, PER_PASS)]] Texture2D<float4> _fontTextures[4096];

float NineSliceAxis(float coord, float pixelSizeUV, float texCoordMin, float texCoordMax, float borderSizeMin, float borderSizeMax)
{
    // Branchless Version
    float scaledBorderMin = texCoordMin + (borderSizeMin * pixelSizeUV);
    float scaledBorderMax = texCoordMax - (borderSizeMax * pixelSizeUV);

    bool isBorderMin = coord < scaledBorderMin;
    bool isCenter = !isBorderMin && coord < scaledBorderMax;
    bool isBorderMax = !isBorderMin && !isCenter;

    float originalMin = (texCoordMin * isBorderMin) + (scaledBorderMin * isCenter) + (scaledBorderMax * isBorderMax);
    float originalMax = (scaledBorderMin * isBorderMin) + (scaledBorderMax * isCenter) + (texCoordMax * isBorderMax);
    float newMin = (texCoordMin * isBorderMin) + ((texCoordMin + borderSizeMin) * isCenter) + ((texCoordMax - borderSizeMax) * isBorderMax);
    float newMax = ((texCoordMin + borderSizeMin) * isBorderMin) + ((texCoordMax - borderSizeMax) * isCenter) + (texCoordMax * isBorderMax);

    return Map(coord, originalMin, originalMax, newMin, newMax);
}

bool ShouldDiscard(float2 pos, float2 clipMin, float2 clipMax)
{
    return pos.x < clipMin.x || pos.x > clipMax.x || pos.y < clipMin.y || pos.y > clipMax.y;
}

float Median(float a, float b, float c)
{
    return max(min(a, b), min(max(a, b), c));
}

float ScreenPxRange(float2 uv, float2 unitRange)
{
    float2 screenTexSize = float2(1.0f, 1.0f) / fwidth(uv);
    return max(0.5 * dot(unitRange, screenTexSize), 1.0);
}

struct VertexOutput
{
    float4 position : SV_POSITION;
    float4 uvAndScreenPos : TEXCOORD0;
    nointerpolation uint drawDataID : TEXCOORD1;
};

float4 ShadePanel(WidgetDrawData drawData, VertexOutput input)
{
    float2 uv = input.uvAndScreenPos.xy;
    float2 texCoordMin = drawData.texCoord.xy;
    float2 texCoordMax = drawData.texCoord.zw;
    float2 slicingCoordMin = max(drawData.slicingCoord.xy, texCoordMin);
    float2 slicingCoordMax = min(drawData.slicingCoord.zw, texCoordMax);

    float2 borderSizeLeftTop = slicingCoordMin - texCoordMin;
    float2 borderSizeRightBottom = texCoordMax - slicingCoordMax;

    uint packedTextureScaleToWidgetSize = drawData.packed1.w;
    float2 scale = float2(f16tof32(packedTextureScaleToWidgetSize), f16tof32(packedTextureScaleToWidgetSize >> 16));

    float2 scaledUV = float2(
        NineSliceAxis(input.uvAndScreenPos.x, scale.x, texCoordMin.x, texCoordMax.x, borderSizeLeftTop.x, borderSizeRightBottom.x),
        NineSliceAxis(input.uvAndScreenPos.y, scale.y, texCoordMin.y, texCoordMax.y, borderSizeLeftTop.y, borderSizeRightBottom.y)
    );

    uint textureIndex = drawData.packed1.x & 0xFFFF;
    uint additiveTextureIndex = drawData.packed1.x >> 16;
    uint packedColor = drawData.packed1.z;

    float4 colorMultiplier = PackedUnormsToFloat4(packedColor);

    float4 color = _textures[textureIndex].Sample(_sampler, scaledUV);
    color *= colorMultiplier;

    float4 additiveColor = _textures[additiveTextureIndex].Sample(_sampler, scaledUV);
    float additiveIntensity = dot(additiveColor.rgb, float3(0.299, 0.587, 0.114)) * 2.5f;
    additiveIntensity = saturate(additiveIntensity);

    color.rgb += additiveColor.rgb;
    color.a = max(color.a, additiveIntensity);

    float2 cornerRadius = drawData.cornerRadiusAndBorder.xy;

    // Calculate distance to nearest edge
    float2 edgeDist = min(uv, 1.0 - uv);

    if (cornerRadius.x > 0 && cornerRadius.y > 0)
    {
        if (edgeDist.x < cornerRadius.x && edgeDist.y < cornerRadius.y)
        {
            float2 normalizedDist = 1.0 - ((edgeDist) / cornerRadius);
            float distToCorner = length(normalizedDist);

            if (distToCorner > 1.0)
            {
                discard;
            }
        }
    }

    return color;
}

float4 ShadeText(WidgetDrawData drawData, VertexOutput input)
{
    uint fontTextureIndex = drawData.packed1.x & 0xFFFF;

    uint packedTextColor = drawData.packed1.z;
    uint packedBorderColor = drawData.packed1.w;

    float4 textColor = PackedUnormsToFloat4(packedTextColor);
    float4 borderColor = PackedUnormsToFloat4(packedBorderColor);

    float borderSize = drawData.cornerRadiusAndBorder.x;
    float2 unitRange = drawData.cornerRadiusAndBorder.zw;

    float4 distances = _fontTextures[fontTextureIndex].Sample(_sampler, input.uvAndScreenPos.xy).rgba;

    const float roundedInlines = 0.0f;
    const float roundedOutlines = 1.0f;
    const float outBias = 1.0 / 4.0;

    float distMsdf = Median(distances.r, distances.g, distances.b);
    float distSdf = distances.a; // mtsdf format only
    distMsdf = min(distMsdf, distSdf + 0.1f); // HACK: to fix glitch in msdf near edges, see https://www.redblobgames.com/x/2404-distance-field-effects/

    // Blend between sharp and rounded corners
    float distInner = lerp(distMsdf, distSdf, roundedInlines);
    float distOuter = lerp(distMsdf, distSdf, roundedOutlines);

    const float threshold = 0.5f;
    float width = ScreenPxRange(input.uvAndScreenPos.xy, unitRange);

    float inner = width * (distInner - threshold) + 0.5f + outBias;
    float outer = width * (distOuter - threshold) + 0.5f + outBias + borderSize;

    float innerOpacity = saturate(inner);
    float4 innerColor = textColor;
    float outerOpacity = saturate(outer);
    float4 outerColor = float4(borderColor.rgb, 1.0f);

    return (innerColor * innerOpacity) + (outerColor * (outerOpacity - innerOpacity));
}

[shader("fragment")]
float4 main(VertexOutput input) : SV_Target
{
    WidgetDrawData drawData = _widgetDrawDatas[input.drawDataID];

    // Shared clip-region discard
    float2 screenPos = input.uvAndScreenPos.zw;
    float2 clipRegionMin = float2(f16tof32(drawData.packed2.x), f16tof32(drawData.packed2.x >> 16));
    float2 clipRegionMax = float2(f16tof32(drawData.packed2.y), f16tof32(drawData.packed2.y >> 16));
    if (ShouldDiscard(screenPos, clipRegionMin, clipRegionMax))
    {
        discard;
    }

    uint type = drawData.packed0.x;
    float4 color;
    if (type == WIDGET_TYPE_PANEL)
    {
        color = ShadePanel(drawData, input);
    }
    else // WIDGET_TYPE_TEXT
    {
        color = ShadeText(drawData, input);
    }

    // Shared clipMask
    float2 clipMaskRegionMin = float2(f16tof32(drawData.packed2.z), f16tof32(drawData.packed2.z >> 16));
    float2 clipMaskRegionMax = float2(f16tof32(drawData.packed2.w), f16tof32(drawData.packed2.w >> 16));
    float2 maskUV = (screenPos - clipMaskRegionMin) / (clipMaskRegionMax - clipMaskRegionMin);

    uint clipMaskTextureIndex = drawData.packed0.z;
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
