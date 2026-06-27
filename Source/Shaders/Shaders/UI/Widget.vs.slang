
#include "DescriptorSet/Global.inc.slang"

[[vk::binding(0, PER_PASS)]] StructuredBuffer<float4> _vertices;
[[vk::binding(1, PER_PASS)]] StructuredBuffer<float4> _widgetWorldPositions;
[[vk::binding(8, PER_PASS)]] StructuredBuffer<float4x4> _widgetLocalMatrices;
[[vk::binding(9, PER_PASS)]] StructuredBuffer<uint> _widgetParentSlots;

struct WidgetDrawData
{
    uint4 packed0; // x: type, y: vertexBase, z: matrix slot (into _widgetLocalMatrices), w: worldPositionIndex (int reinterpret)
    uint4 packed1; // Panel: x: textureIndex & additiveTextureIndex, z: color, w: textureScaleToWidgetSize (half2). Text: x: fontTextureIndex, z: textColor, w: borderColor
    float4 texCoord;            // Panel only
    float4 slicingCoord;        // Panel only
    float4 cornerRadiusAndBorder; // Panel: xy: cornerRadius. Text: x: borderSize, zw: unitRange
    uint4 packed2; // x: clipBoundsIndex (into _widgetClipRects), y: maskBoundsIndex (into _widgetMaskInfo), z/w: reserved
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

        // 3D-anchored: transform the world position to clip space (camera-projected).
        float4 clipPos = mul(float4(worldPos, 1.0), _cameras[0].worldToClip);
        clipPos.xyz /= clipPos.w; // Perform perspective division.

        finalPos = float4(clipPos.xy + position, 0.0, 1.0);
    }
    else
    {
        // Uniform 2D path: compose the parent chain of local matrices to screen/NDC. A chain root
        // (parent slot == ~0u) carries the pixel->NDC fold, so the accumulated matrix lands in NDC.
        // glm matrices upload transposed (column-major read as row-major), so mul(A,B) = (B*A)^T;
        // accumulating with acc on the LEFT (mul(acc, parent)) yields the correct root*...*leaf order.
        uint slot = drawData.packed0.z;
        float4x4 acc = _widgetLocalMatrices[slot];
        uint p = _widgetParentSlots[slot];

        [unroll(16)]
        for (uint i = 0; i < 16; i++)
        {
            if (p == 0xFFFFFFFFu)
                break;
            acc = mul(acc, _widgetLocalMatrices[p]);
            p = _widgetParentSlots[p];
        }

        float4 ndc = mul(float4(position, 0.0, 1.0), acc);
        finalPos = float4(ndc.xy, 0.0, 1.0);
    }

    VertexOutput output;
    output.position = finalPos;

    float2 screenPos = (finalPos.xy + 1.0f) * 0.5f;
    output.uvAndScreenPos = float4(uv, screenPos);
    output.drawDataID = input.drawDataID;

    return output;
}
