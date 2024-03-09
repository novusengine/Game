
struct Vertex
{
    float4 posAndUV;
};
[[vk::binding(0, PER_PASS)]] StructuredBuffer<Vertex> _vertices;

struct VertexInput
{
    uint vertexID : SV_VertexID;
};

struct VertexOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    uint charIndex : TEXCOORD1;
};

VertexOutput main(VertexInput input)
{
    VertexOutput output;

    Vertex vertex = _vertices[input.vertexID];

    output.position = float4((vertex.posAndUV.xy * 2.0f) - 1.0f, 0.0f, 1.0f);
    output.uv = vertex.posAndUV.zw;
    output.charIndex = floor(input.vertexID / 6); // 6 vertices per character

    return output;
}