
[[vk::binding(0, PER_PASS)]] StructuredBuffer<float4> _vertices;

struct CharDrawData
{
    uint4 packed0; // x: textureIndex & charIndex, y: clipMaskTextureIndex, z: textColor, w: borderColor
    float4 packed1; // x: borderSize, y: padding, zw: unitRangeXY
    uint4 packed2; // x: clipRegionMinXY, y: clipRegionMaxXY, z: clipMaskRegionMinXY, w: clipMaskRegionMaxXY
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
    float4 uvAndScreenPos : TEXCOORD0;
    uint charDrawDataID : TEXCOORD1;
};

VertexOutput main(VertexInput input)
{
    CharDrawData charDrawData = _charDrawDatas[input.charDrawDataID];

    uint charIndex = charDrawData.packed0.x >> 16;

    uint vertexID = input.vertexID + (charIndex * 6); // 6 vertices per character
    float4 vertex = _vertices[vertexID];

    float2 position = vertex.xy;
    float2 uv = vertex.zw;

    VertexOutput output;
    output.position = float4(position, 0.0f, 1.0f);
    float2 screenPos = (position + 1.0f) * 0.5f;
    output.uvAndScreenPos = float4(uv, screenPos);
    output.charDrawDataID = input.charDrawDataID;

    return output;
}