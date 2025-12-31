
#include "DescriptorSet/Global.inc.slang"

[[vk::binding(0, PER_PASS)]] StructuredBuffer<float4> _vertices;
[[vk::binding(1, PER_PASS)]] StructuredBuffer<float4> _widgetWorldPositions;

struct PanelDrawData
{
    uint4 packed0; // x: textureIndex & additiveTextureIndex, y: clipMaskTextureIndex, z: color, w: textureScaleToWidgetSizeXY
    float4 texCoord;
    float4 slicingCoord;
    float4 cornerRadiusAndBorder; // xy: cornerRadius, zw: border
    uint4 packed1; // x: clipRegionMinXY, y: clipRegionMaxXY, z: clipMaskRegionMinXY, w: clipMaskRegionMaxXY
    int4 packed2; // x: worldPositionIndex, yzw: unused
};
[[vk::binding(2, PER_PASS)]] StructuredBuffer<PanelDrawData> _panelDrawDatas;

struct VertexInput
{
    uint vertexID : SV_VulkanVertexID;
    uint drawDataID : SV_VulkanInstanceID;
};

struct VertexOutput
{
    float4 position : SV_POSITION;
    float4 uvAndScreenPos : TEXCOORD0;
    uint drawDataID : TEXCOORD1;
};

[shader("vertex")]
VertexOutput main(VertexInput input)
{
    float4 vertex = _vertices[input.vertexID];

    float2 position = vertex.xy;
    float2 uv = vertex.zw;

    PanelDrawData drawData = _panelDrawDatas[input.drawDataID];
    int worldPositionIndex = drawData.packed2.x;
    float4 finalPos;

    if (worldPositionIndex >= 0)
    {
        float3 worldPos = _widgetWorldPositions[worldPositionIndex].xyz;

        // Transform the world position to clip space.
        float4 clipPos = mul(float4(worldPos, 1.0), _cameras[0].worldToClip);
        clipPos.xyz /= clipPos.w; // Perform perspective division.

        finalPos = float4(clipPos.xy + position, 0.0, 1.0);
    }
    else
    {
        finalPos = float4(position, 0.0, 1.0);
    }

    VertexOutput output;
    output.position = finalPos;

    float2 screenPos = (finalPos.xy + 1.0f) * 0.5f;
    output.uvAndScreenPos = float4(uv, screenPos);
    output.drawDataID = input.drawDataID;

    return output;
}