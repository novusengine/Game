
#include "common.inc.hlsl"

struct PanelDrawData
{
    uint4 packed; // x: textureIndex, y: additiveTextureIndex, y: color
    float4 slicingOffset;
    float4 cornerRadiusAndBorder; // xy: cornerRadius, zw: border
};
[[vk::binding(1, PER_PASS)]] StructuredBuffer<PanelDrawData> _panelDrawDatas;
[[vk::binding(2, PER_PASS)]] SamplerState _sampler;
[[vk::binding(3, PER_PASS)]] Texture2D<float4> _textures[4096];

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

    uint textureIndex = drawData.packed.x;
    uint additiveTextureIndex = drawData.packed.y;
    uint packedColor = drawData.packed.z;

    float4 colorMultiplier = PackedUnormsToFloat4(packedColor);

    float4 color = _textures[textureIndex].Sample(_sampler, uv);
    color.rgb *= colorMultiplier.rgb;

    float4 additiveColor = _textures[additiveTextureIndex].Sample(_sampler, uv);
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