
#include "DescriptorSet/Global.inc.slang"

#define WIDGET_TYPE_PANEL 0u
#define WIDGET_TYPE_TEXT 1u

[[vk::binding(0, PER_PASS)]] StructuredBuffer<float4> _vertices;
[[vk::binding(1, PER_PASS)]] StructuredBuffer<float4> _widgetWorldPositions;
[[vk::binding(8, PER_PASS)]] StructuredBuffer<float4x4> _widgetLocalMatrices;
[[vk::binding(9, PER_PASS)]] StructuredBuffer<uint> _widgetParentSlots;

struct WidgetDrawData
{
    uint4 packed0; // x: type, y: vertexBase, z: matrix slot (into _widgetLocalMatrices), w: worldPositionIndex (int reinterpret)
    uint4 packed1; // Panel: x: textureIndex & additiveTextureIndex, z: color, w: textureScaleToWidgetSize (half2). Text: x: fontTextureIndex, z: textColor, w: borderColor
    float4 texCoord;            // Panel only
    float4 sliceCoords;         // Panel only
    float4 cornerRadiusAndBorder; // Panel: xy: cornerRadius. Text: x: borderSize, zw: unitRange
    uint4 packed2; // x: clipBoundsIndex (into _widgetClipRects), y: maskBoundsIndex (into _widgetMaskInfo), z/w: reserved
};
[[vk::binding(2, PER_PASS)]] StructuredBuffer<WidgetDrawData> _widgetDrawDatas;

struct Constants
{
    float2 viewportSize;
};
[[vk::push_constant]] Constants _constants;

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
        if (clipPos.w <= 0.0f)
        {
            // Keep world widgets behind the camera outside the clip volume without CPU visibility churn.
            finalPos = float4(2.0, 2.0, 0.0, 1.0);
        }
        else
        {
            // The glyph offset is authored in NDC. Convert it back to a homogeneous clip-space
            // offset. The rasterizer's perspective divide then produces continuous inverse-depth
            // growth, including the reference client's extreme close range, without either clamp.
            float2 clipOffset = position * 15.0f;

            finalPos = float4(clipPos.xy + clipOffset, clipPos.z, clipPos.w);
        }
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

        if (drawData.packed0.x == WIDGET_TYPE_PANEL)
        {
            float2 localMin = _vertices[vertexBase + 0].xy;
            float2 localMax = _vertices[vertexBase + 1].xy;
            float2 ndcA = mul(float4(localMin, 0.0, 1.0), acc).xy;
            float2 ndcB = mul(float4(localMax, 0.0, 1.0), acc).xy;

            float2 ndcX = mul(float4(localMin + float2(1.0, 0.0), 0.0, 1.0), acc).xy - ndcA;
            float2 ndcY = mul(float4(localMin + float2(0.0, 1.0), 0.0, 1.0), acc).xy - ndcA;
            bool axisAligned = abs(ndcX.y) <= 1e-6f && abs(ndcY.x) <= 1e-6f;
            if (axisAligned)
            {
                float2 pixelA = (ndcA + 1.0f) * 0.5f * _constants.viewportSize;
                float2 pixelB = (ndcB + 1.0f) * 0.5f * _constants.viewportSize;
                float2 pixelMin = min(pixelA, pixelB);
                float2 pixelMax = max(pixelA, pixelB);
                float2 snappedMin = round(pixelMin);
                float2 snappedMax = max(round(pixelMax), snappedMin + 1.0f);
                float2 pixelPosition = (ndc.xy + 1.0f) * 0.5f * _constants.viewportSize;

                float2 snappedPosition;
                snappedPosition.x = abs(pixelPosition.x - pixelMin.x) <= abs(pixelPosition.x - pixelMax.x) ? snappedMin.x : snappedMax.x;
                snappedPosition.y = abs(pixelPosition.y - pixelMin.y) <= abs(pixelPosition.y - pixelMax.y) ? snappedMin.y : snappedMax.y;

                finalPos.xy = (snappedPosition / _constants.viewportSize) * 2.0f - 1.0f;
            }
        }
        else if (drawData.packed0.x == WIDGET_TYPE_TEXT)
        {
            float2 originNdc = mul(float4(0.0, 0.0, 0.0, 1.0), acc).xy;
            float2 originPixels = (originNdc + 1.0f) * 0.5f * _constants.viewportSize;
            float2 snappedOriginPixels = round(originPixels);
            float2 snapOffsetNdc = ((snappedOriginPixels - originPixels) / _constants.viewportSize) * 2.0f;
            finalPos.xy += snapOffsetNdc;
        }
    }

    VertexOutput output;
    output.position = finalPos;

    float2 ndcPosition = finalPos.xy / finalPos.w;
    float2 screenPos = (ndcPosition + 1.0f) * 0.5f;
    output.uvAndScreenPos = float4(uv, screenPos);
    output.drawDataID = input.drawDataID;

    return output;
}
