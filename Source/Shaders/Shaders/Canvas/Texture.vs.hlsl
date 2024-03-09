
struct Constants
{
    uint4 resolution;
};
[[vk::push_constant]] Constants _constants;

struct DrawData
{
    uint4 posAndSize;
    int4 textureIndex;
    float4 slicingOffset;
};
[[vk::binding(0, PER_PASS)]] StructuredBuffer<DrawData> _drawData;

struct VertexInput
{
    uint vertexID : SV_VertexID;
    //uint instanceID : SV_InstanceID;
};

struct VertexOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0 ;
    uint drawID : TEXCOORD1;
};

float2 ConvertPixelToNDC(float2 pixelPos, float2 screenSize)
{
    return float2(2.0 * pixelPos.x / screenSize.x - 1.0, -2.0 * pixelPos.y / screenSize.y + 1.0);
}

VertexOutput main(VertexInput input)
{
    uint instanceID = floor(input.vertexID / 6.0f);
    DrawData drawData = _drawData[instanceID];

    uint localVertexID = input.vertexID % 6;

    float2 uvs[6] = {
        // Triangle 1
        float2(0, 0), // Top Left
        float2(0, 1), // Bottom Left
        float2(1, 0), // Top Right
        // Triangle 2
        float2(1, 0), // Top Left
        float2(0, 1), // Bottom Left
        float2(1, 1), // Bottom Right
    };

    float2 uv = uvs[localVertexID];

    // Convert the top-left corner position and size from pixel to NDC
    float2 topLeftNDC = ConvertPixelToNDC(drawData.posAndSize.xy, _constants.resolution.xy);
    float2 sizeNDC = ConvertPixelToNDC(drawData.posAndSize.zw, _constants.resolution.xy) - ConvertPixelToNDC(float2(0, 0), _constants.resolution.xy);

    VertexOutput output;
    output.position = float4(topLeftNDC + sizeNDC * uv, 0.0f, 1.0f);
    output.uv = float2(uv.x, uv.y);
    output.drawID = instanceID;

    return output;
}