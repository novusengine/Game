
#include "DescriptorSet/Global.inc.slang"

[[vk::binding(0, PER_PASS)]] StructuredBuffer<float4> _vertices;
[[vk::binding(1, PER_PASS)]] StructuredBuffer<float4> _widgetWorldPositions;

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
    WidgetDrawData drawData = _widgetDrawDatas[input.drawDataID];

    uint vertexBase = drawData.packed0.y;
    float4 vertex = _vertices[vertexBase + input.vertexID];

    float2 position = vertex.xy;
    float2 uv = vertex.zw;

    int worldPositionIndex = (int)drawData.packed0.w;
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
