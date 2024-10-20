
[[vk::binding(0, PER_PASS)]] StructuredBuffer<float4> _vertices;

struct CharDrawData
{
    uint4 packed0; // x: textureIndex, y: charIndex, z: textColor, w: borderColor
    float4 packed1; // x: borderSizeX, y: borderSizeY
};
[[vk::binding(1, PER_PASS)]] StructuredBuffer<CharDrawData> _charDrawDatas;

struct VertexInput
{
    uint vertexID : SV_VertexID;
    uint charDrawDataID : SV_InstanceID;
};

struct VertexOutput 
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    uint charDrawDataID : TEXCOORD1;
};

VertexOutput main(VertexInput input)
{
    CharDrawData charDrawData = _charDrawDatas[input.charDrawDataID];

    uint vertexID = input.vertexID + (charDrawData.packed0.y * 6); // 6 vertices per character
    float4 vertex = _vertices[vertexID];

    float2 position = vertex.xy;
    float2 uv = vertex.zw;

    VertexOutput output;
    output.position = float4(position, 0.0f, 1.0f);
    output.uv = uv;
    output.charDrawDataID = input.charDrawDataID;

    return output;
}