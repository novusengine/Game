
#include "DescriptorSet/Global.inc.slang"

#include "Terrain/TerrainShared.inc.slang"

struct PackedDrawCallData
{
    uint packed0; // u16 chunkID, u16 cellID
    uint packed1; // u16 textureStartIndex, u8 textureCount, u8 hasDepth
    uint packed2; // u16 liquidType, u16 padding
    uint packed3; // f16 uvAnim scrolling, f16 uvAnim rotation
};

struct DrawCallData
{
    uint chunkID;
    uint cellID;
    uint textureStartIndex;
    uint textureCount;
    uint hasDepth;
    uint liquidType;
    float2 uvAnim;
};

[[vk::binding(0, PER_PASS)]] StructuredBuffer<PackedDrawCallData> _drawCallDatas;

DrawCallData LoadDrawCallData(uint drawCallID)
{
    PackedDrawCallData packedDrawCallData = _drawCallDatas[drawCallID];

    DrawCallData drawCallData;
    drawCallData.chunkID = ((packedDrawCallData.packed0 >> 0) & 0xFFFF);
    drawCallData.cellID = ((packedDrawCallData.packed0 >> 16) & 0xFFFF);
    drawCallData.textureStartIndex = ((packedDrawCallData.packed1 >> 0) & 0xFFFF);
    drawCallData.textureCount = ((packedDrawCallData.packed1 >> 16) & 0xFF);
    drawCallData.hasDepth = ((packedDrawCallData.packed1 >> 24) & 0xFF);
    drawCallData.liquidType = ((packedDrawCallData.packed2 >> 0) & 0xFFFF);
    drawCallData.uvAnim.x = f16tof32(packedDrawCallData.packed3);
    drawCallData.uvAnim.y = f16tof32(packedDrawCallData.packed3 >> 16);

    return drawCallData;
}

struct PackedVertex
{
    uint packed0; // u8 xCellOffset, u8 yCellOffset, u16 padding
    float height;
    uint packed1; // f16 xUV, f16 yUV
}; // 12 bytes

struct Vertex
{
    float3 position;
    float2 uv;
};

[[vk::binding(1, PER_PASS)]] StructuredBuffer<PackedVertex> _vertices;

Vertex LoadVertex(DrawCallData drawCallData, uint vertexID)
{
    PackedVertex packedVertex = _vertices[vertexID];

    float2 cellOffsetPos = float2(((packedVertex.packed0 >> 0) & 0xFF) * PATCH_SIDE_SIZE, ((packedVertex.packed0 >> 8) & 0xFF) * PATCH_SIDE_SIZE);

    Vertex vertex;

    float2 cellBasePos = GetCellPosition(drawCallData.chunkID, drawCallData.cellID);
    vertex.position.xz = cellBasePos - cellOffsetPos;
    vertex.position.y = packedVertex.height;

    vertex.uv.x = f16tof32(packedVertex.packed1 >> 0);
    vertex.uv.y = f16tof32(packedVertex.packed1 >> 16);

    return vertex;
}

struct VSInput
{
    uint vertexID : SV_VulkanVertexID;
    uint instanceID : SV_VulkanInstanceID;
};

struct VSOutput
{
    float4 position : SV_Position;
    float2 textureUV : TEXCOORD0;
    uint drawCallID : TEXCOORD1;
    float3 posViewSpace : TEXCOORD2;
};

[shader("vertex")]
VSOutput main(VSInput input)
{
    VSOutput output;

    DrawCallData drawCallData = LoadDrawCallData(input.instanceID);
    Vertex vertex = LoadVertex(drawCallData, input.vertexID);
    float2 uv = vertex.uv.xy;

    output.position = mul(float4(vertex.position.xyz, 1.0f), _cameras[0].worldToClip);
    output.textureUV = uv;
    output.drawCallID = input.instanceID;
    output.posViewSpace = mul(float4(vertex.position.xyz, 1.0f), _cameras[0].worldToView).xyz;

    return output;
}