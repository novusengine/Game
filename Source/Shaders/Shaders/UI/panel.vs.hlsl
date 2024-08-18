
[[vk::binding(0, PER_PASS)]] StructuredBuffer<float4> _vertices;

struct VertexInput
{
    uint vertexID : SV_VertexID;
    uint drawDataID : SV_InstanceID;
};

struct VertexOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0 ;
    uint drawDataID : TEXCOORD1;
};

VertexOutput main(VertexInput input)
{
    float4 vertex = _vertices[input.vertexID];

    float2 position = vertex.xy;
    float2 uv = vertex.zw;

    VertexOutput output;
    output.position = float4(position, 0.0f, 1.0f);
    output.uv = uv;
    output.drawDataID = input.drawDataID;

    return output;
}