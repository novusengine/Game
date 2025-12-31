
#include "Include/Common.inc.slang"

struct PanelDrawData
{
    uint4 packed0; // x: textureIndex & additiveTextureIndex, y: clipMaskTextureIndex, z: color, w: textureScaleToWidgetSizeXY
    float4 texCoord;
    float4 slicingCoord;
    float4 cornerRadiusAndBorder; // xy: cornerRadius, zw: border
    uint4 packed1; // x: clipRegionMinXY, y: clipRegionMaxXY, z: clipMaskRegionMinXY, w: clipMaskRegionMaxXY
    int4 packed2; // x: worldPositionIndex, y: half2 anchorPos, z: half2 relativePos
};
[[vk::binding(2, PER_PASS)]] StructuredBuffer<PanelDrawData> _panelDrawDatas;
[[vk::binding(3, PER_PASS)]] SamplerState _sampler;
[[vk::binding(4, PER_PASS)]] Texture2D<float4> _textures[4096];

float NineSliceAxis(float coord, float pixelSizeUV, float texCoordMin, float texCoordMax, float borderSizeMin, float borderSizeMax)
{
    /* Original Code
    float scaledBorderMin = texCoordMin + (borderSizeMin * pixelSizeUV);
    if (coord < scaledBorderMin) // Min
        return Map(coord, texCoordMin, scaledBorderMin, texCoordMin, texCoordMin + borderSizeMin);

    float scaledBorderMax = texCoordMax - (borderSizeMax * pixelSizeUV);
    if (coord < scaledBorderMax) // Center
        return Map(coord, scaledBorderMin, scaledBorderMax, texCoordMin + borderSizeMin, texCoordMax - borderSizeMax);

    // Max
    return Map(coord, scaledBorderMax, texCoordMax, texCoordMax - borderSizeMax, texCoordMax);
    */
    
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
    // Check if the position is outside the clip rect
    return pos.x < clipMin.x || pos.x > clipMax.x || pos.y < clipMin.y || pos.y > clipMax.y;
}

struct VertexOutput
{
    float4 position : SV_POSITION;
    float4 uvAndScreenPos : TEXCOORD0;
    nointerpolation uint drawDataID : TEXCOORD1;
};

[shader("fragment")]
float4 main(VertexOutput input) : SV_Target
{
    PanelDrawData drawData = _panelDrawDatas[input.drawDataID];

    float2 screenPos = input.uvAndScreenPos.zw;
    float2 clipRegionMin = float2(f16tof32(drawData.packed1.x), f16tof32(drawData.packed1.x >> 16));
    float2 clipRegionMax = float2(f16tof32(drawData.packed1.y), f16tof32(drawData.packed1.y >> 16));
    if (ShouldDiscard(screenPos, clipRegionMin, clipRegionMax))
    {
        //return float4(1, 0, 0, 0.3f);
        discard;
    }

    float2 uv = input.uvAndScreenPos.xy;
    float2 texCoordMin = drawData.texCoord.xy;
    float2 texCoordMax = drawData.texCoord.zw;
    float2 slicingCoordMin = max(drawData.slicingCoord.xy, texCoordMin);
    float2 slicingCoordMax = min(drawData.slicingCoord.zw, texCoordMax);

    float2 borderSizeLeftTop = slicingCoordMin - texCoordMin;
    float2 borderSizeRightBottom = texCoordMax - slicingCoordMax;
    
    uint packedTextureScaleToWidgetSize = drawData.packed0.w;
    float2 scale = float2(f16tof32(packedTextureScaleToWidgetSize), f16tof32(packedTextureScaleToWidgetSize >> 16));

    float2 scaledUV = float2(
        NineSliceAxis(input.uvAndScreenPos.x, scale.x, texCoordMin.x, texCoordMax.x, borderSizeLeftTop.x, borderSizeRightBottom.x),
        NineSliceAxis(input.uvAndScreenPos.y, scale.y, texCoordMin.y, texCoordMax.y, borderSizeLeftTop.y, borderSizeRightBottom.y)
    );

    uint textureIndex = drawData.packed0.x & 0xFFFF;
    uint additiveTextureIndex = drawData.packed0.x >> 16;
    uint packedColor = drawData.packed0.z;
    
    float4 colorMultiplier = PackedUnormsToFloat4(packedColor);

    float4 color = _textures[textureIndex].Sample(_sampler, scaledUV);
    color *= colorMultiplier;

    float4 additiveColor = _textures[additiveTextureIndex].Sample(_sampler, scaledUV);
    float additiveIntensity = dot(additiveColor.rgb, float3(0.299, 0.587, 0.114)) * 2.5f; // Constants from https://en.wikipedia.org/wiki/Grayscale#Luma_coding_in_video_systems
    additiveIntensity = saturate(additiveIntensity);

    // Add the additive color to the base color
    color.rgb += additiveColor.rgb;

    // Blend in the intensity
    color.a = max(color.a, additiveIntensity);

    float2 cornerRadius = drawData.cornerRadiusAndBorder.xy; // Specified in UV space

    // Calculate distance to nearest edge
    float2 edgeDist = min(uv, 1.0 - uv);

    // Check if cornerRadius is greater than zero
    if (cornerRadius.x > 0 && cornerRadius.y > 0)
    {
        // Check if within the rounded corner area
        if (edgeDist.x < cornerRadius.x && edgeDist.y < cornerRadius.y)
        {
            // Calculate distance from the corner using an elliptical formula
            float2 normalizedDist = 1.0 - ((edgeDist) / cornerRadius);
            float distToCorner = length(normalizedDist);

            // Discard pixel if it's outside the rounded corner radius
            if (distToCorner > 1.0)
            {
                discard;
            }
        }
    }

    // Apply the clipMask
    float2 clipMaskRegionMin = float2(f16tof32(drawData.packed1.z), f16tof32(drawData.packed1.z >> 16));
    float2 clipMaskRegionMax = float2(f16tof32(drawData.packed1.w), f16tof32(drawData.packed1.w >> 16));
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