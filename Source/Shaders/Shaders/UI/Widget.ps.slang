
#include "Include/Common.inc.slang"

#define WIDGET_TYPE_PANEL 0u
#define WIDGET_TYPE_TEXT  1u

struct WidgetDrawData
{
    uint4 packed0; // x: type, y: vertexBase, z: reserved, w: worldPositionIndex (int reinterpret)
    uint4 packed1; // Panel: x: textureIndex & additiveTextureIndex, y: borderColor, z: color, w: textureScaleToWidgetSize (half2). Text: x: fontTextureIndex, z: textColor, w: borderColor
    float4 texCoord;            // Panel only
    float4 sliceCoords;         // Panel only
    float4 cornerRadiusAndBorder; // Panel: xy: cornerRadius, zw: borderSize (normalized per-axis). Text: x: borderSize, zw: unitRange
    uint4 packed2; // x: clipBoundsIndex (into _widgetClipRects), y: maskBoundsIndex (into _widgetMaskInfo), z/w: reserved
};

struct WidgetMaskInfo
{
    uint regionMinXY; // two f16 packed
    uint regionMaxXY; // two f16 packed
    uint textureIndex;
    uint reserved;
};

[[vk::binding(2, PER_PASS)]] StructuredBuffer<WidgetDrawData> _widgetDrawDatas;
[[vk::binding(3, PER_PASS)]] SamplerState _sampler;
[[vk::binding(4, PER_PASS)]] Texture2D<float4> _textures[4096];
[[vk::binding(5, PER_PASS)]] Texture2D<float4> _fontTextures[4096];
[[vk::binding(6, PER_PASS)]] StructuredBuffer<float4> _widgetClipRects;
[[vk::binding(7, PER_PASS)]] StructuredBuffer<WidgetMaskInfo> _widgetMaskInfo;

struct Constants
{
    float2 viewportSize;
};
[[vk::push_constant]] Constants _constants;

float RemapRange(float value, float originalMin, float originalMax, float newMin, float newMax)
{
    float originalRange = originalMax - originalMin;
    if (abs(originalRange) <= 1e-6f)
        return newMin;

    return ((value - originalMin) / originalRange) * (newMax - newMin) + newMin;
}

float NineSliceAxis(float coord, float texturePixelsPerWidgetPixel, float texCoordMin, float texCoordMax, float sliceCoordMin, float sliceCoordMax)
{
    float texCoordRange = texCoordMax - texCoordMin;
    if (texCoordRange <= 1e-6f)
        return texCoordMin;

    float localCoord = saturate((coord - texCoordMin) / texCoordRange);
    float destinationBorderMin = max((sliceCoordMin - texCoordMin) * texturePixelsPerWidgetPixel, 0.0f);
    float destinationBorderMax = max((texCoordMax - sliceCoordMax) * texturePixelsPerWidgetPixel, 0.0f);

    float destinationBorderTotal = destinationBorderMin + destinationBorderMax;
    if (destinationBorderTotal > 1.0f)
    {
        float borderScale = 1.0f / destinationBorderTotal;
        destinationBorderMin *= borderScale;
        destinationBorderMax *= borderScale;
    }

    float destinationSliceMin = destinationBorderMin;
    float destinationSliceMax = 1.0f - destinationBorderMax;

    if (destinationBorderMin > 0.0f && localCoord < destinationSliceMin)
        return RemapRange(localCoord, 0.0f, destinationSliceMin, texCoordMin, sliceCoordMin);

    if (destinationBorderMax > 0.0f && localCoord > destinationSliceMax)
        return RemapRange(localCoord, destinationSliceMax, 1.0f, sliceCoordMax, texCoordMax);

    if (destinationSliceMax > destinationSliceMin)
        return RemapRange(localCoord, destinationSliceMin, destinationSliceMax, sliceCoordMin, sliceCoordMax);

    return localCoord <= destinationSliceMin ? sliceCoordMin : sliceCoordMax;
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
    float2 sliceCoordMin = clamp(drawData.sliceCoords.xy, texCoordMin, texCoordMax);
    float2 sliceCoordMax = clamp(drawData.sliceCoords.zw, sliceCoordMin, texCoordMax);

    uint packedTextureScaleToWidgetSize = drawData.packed1.w;
    float2 scale = float2(f16tof32(packedTextureScaleToWidgetSize), f16tof32(packedTextureScaleToWidgetSize >> 16));

    float2 scaledUV = float2(
        NineSliceAxis(input.uvAndScreenPos.x, scale.x, texCoordMin.x, texCoordMax.x, sliceCoordMin.x, sliceCoordMax.x),
        NineSliceAxis(input.uvAndScreenPos.y, scale.y, texCoordMin.y, texCoordMax.y, sliceCoordMin.y, sliceCoordMax.y)
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
    float2 normalizedBorderSize = drawData.cornerRadiusAndBorder.zw;

    // Calculate distance to nearest edge
    float2 edgeDist = min(uv, 1.0 - uv);

    bool inCornerZone = cornerRadius.x > 0 && cornerRadius.y > 0
        && edgeDist.x < cornerRadius.x && edgeDist.y < cornerRadius.y;

    if (inCornerZone)
    {
        float2 normalizedDist = 1.0 - (edgeDist / cornerRadius);
        float distToCorner = length(normalizedDist);

        if (distToCorner > 1.0)
        {
            discard;
        }
    }

    if (normalizedBorderSize.x > 0.0 || normalizedBorderSize.y > 0.0)
    {
        // A logical UI pixel can cover less than one physical pixel when the reference UI is
        // scaled to the viewport. Keep authored borders at least one fragment wide so subpixel
        // movement, such as scrolling, cannot make individual edges disappear.
        float2 effectiveBorderSize = max(normalizedBorderSize, fwidth(uv));
        float4 borderColor = PackedUnormsToFloat4(drawData.packed1.y);
        bool inBorder;

        if (inCornerZone)
        {
            // Corner zone: pixel is in the border ring if it lies outside
            // the inner corner ellipse (radii shrunken by borderSize).
            float2 innerR = max(cornerRadius - effectiveBorderSize, float2(1e-5, 1e-5));
            float2 toInner = 1.0 - edgeDist / innerR;
            bool insideInnerBox = toInner.x > 0.0 && toInner.y > 0.0;
            inBorder = !insideInnerBox || length(toInner) > 1.0;
        }
        else
        {
            inBorder = edgeDist.x < effectiveBorderSize.x || edgeDist.y < effectiveBorderSize.y;
        }

        if (inBorder)
        {
            color.rgb = lerp(color.rgb, borderColor.rgb, borderColor.a);
            color.a   = max(color.a, borderColor.a);
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

    if (borderSize <= 0.0f)
    {
        return innerColor * innerOpacity;
    }

    float outerOpacity = saturate(outer);
    outerOpacity = max(outerOpacity, innerOpacity);
    float4 outerColor = float4(borderColor.rgb, 1.0f);

    return (innerColor * innerOpacity) + (outerColor * (outerOpacity - innerOpacity));
}

[shader("fragment")]
float4 main(VertexOutput input) : SV_Target
{
    WidgetDrawData drawData = _widgetDrawDatas[input.drawDataID];

    // Shared clip-region discard. Index 0 is the "no clip" sentinel (0,0,1,1).
    float2 screenPos = input.uvAndScreenPos.zw;
    uint clipBoundsIndex = drawData.packed2.x;
    float4 clipRect = _widgetClipRects[clipBoundsIndex];
    if (clipBoundsIndex != 0)
    {
        float2 clipPixelMin = round(clipRect.xy * _constants.viewportSize);
        float2 clipPixelMax = max(round(clipRect.zw * _constants.viewportSize), clipPixelMin + 1.0f);
        clipRect = float4(clipPixelMin, clipPixelMax) / float4(_constants.viewportSize, _constants.viewportSize);
    }
    if (ShouldDiscard(screenPos, clipRect.xy, clipRect.zw))
    {
        discard;
    }

    uint type = drawData.packed0.x;
    float4 color;
    if (type == WIDGET_TYPE_PANEL)
    {
        color = ShadePanel(drawData, input);
        color.rgb *= color.a;
    }
    else // WIDGET_TYPE_TEXT
    {
        color = ShadeText(drawData, input);
    }

    // Shared clipMask. Index 0 is the sentinel (full region + default-white texture
    // at index 0), so widgets without a mask trivially pass.
    uint maskBoundsIndex = drawData.packed2.y;
    WidgetMaskInfo maskInfo = _widgetMaskInfo[maskBoundsIndex];
    float2 clipMaskRegionMin = float2(f16tof32(maskInfo.regionMinXY), f16tof32(maskInfo.regionMinXY >> 16));
    float2 clipMaskRegionMax = float2(f16tof32(maskInfo.regionMaxXY), f16tof32(maskInfo.regionMaxXY >> 16));
    float2 maskUV = (screenPos - clipMaskRegionMin) / (clipMaskRegionMax - clipMaskRegionMin);
    float clipMask = _textures[maskInfo.textureIndex].Sample(_sampler, maskUV).a;
    if (clipMask < 0.5f)
    {
        discard;
    }
    // Panel colors are converted to premultiplied alpha above. Text colors are already
    // premultiplied by their MSDF coverage in ShadeText, so the clip mask must scale both.
    color *= clipMask;

    return saturate(color);
}
