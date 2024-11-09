
#include "common.inc.hlsl"

struct PanelDrawData
{
    uint4 packed; // x: textureIndex, y: additiveTextureIndex, z: color, w: textureScaleToWidgetSize
    float4 texCoord;
    float4 slicingCoord;
    float4 cornerRadiusAndBorder; // xy: cornerRadius, zw: border
};
[[vk::binding(1, PER_PASS)]] StructuredBuffer<PanelDrawData> _panelDrawDatas;
[[vk::binding(2, PER_PASS)]] SamplerState _sampler;
[[vk::binding(3, PER_PASS)]] Texture2D<float4> _textures[4096];

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

struct VertexOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    nointerpolation uint drawDataID : TEXCOORD1;
};

float4 main(VertexOutput input) : SV_Target
{
    PanelDrawData drawData = _panelDrawDatas[input.drawDataID];

    float2 uv = input.uv;
    float2 texCoordMin = drawData.texCoord.xy;
    float2 texCoordMax = drawData.texCoord.zw;
    float2 slicingCoordMin = max(drawData.slicingCoord.xy, texCoordMin);
    float2 slicingCoordMax = min(drawData.slicingCoord.zw, texCoordMax);

    float2 borderSizeLeftTop = slicingCoordMin - texCoordMin;
    float2 borderSizeRightBottom = texCoordMax - slicingCoordMax;
    
    uint packedTextureScaleToWidgetSize = drawData.packed.w;
    float2 scale = float2(f16tof32(packedTextureScaleToWidgetSize), f16tof32(packedTextureScaleToWidgetSize >> 16));

    float2 scaledUV = float2(
        NineSliceAxis(input.uv.x, scale.x, texCoordMin.x, texCoordMax.x, borderSizeLeftTop.x, borderSizeRightBottom.x),
        NineSliceAxis(input.uv.y, scale.y, texCoordMin.y, texCoordMax.y, borderSizeLeftTop.y, borderSizeRightBottom.y)
    );

    uint textureIndex = drawData.packed.x;
    uint additiveTextureIndex = drawData.packed.y;
    uint packedColor = drawData.packed.z;
    
    float4 colorMultiplier = PackedUnormsToFloat4(packedColor);

    float4 color = _textures[textureIndex].Sample(_sampler, scaledUV);
    color.rgb *= colorMultiplier.rgb;

    float4 additiveColor = _textures[additiveTextureIndex].Sample(_sampler, scaledUV);
    color.rgb += additiveColor.rgb;

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

    return saturate(color);
}