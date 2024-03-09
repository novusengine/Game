
struct DrawData
{
    uint4 posAndSize;
    int4 textureIndex;
    float4 slicingOffset;
};
[[vk::binding(0, PER_PASS)]] StructuredBuffer<DrawData> _drawData;
[[vk::binding(1, PER_PASS)]] SamplerState _sampler;
[[vk::binding(2, PER_PASS)]] Texture2D<float4> _textures[4096];

float Map(float value, float originalMin, float originalMax, float newMin, float newMax)
{
    return (value - originalMin) / (originalMax - originalMin) * (newMax - newMin) + newMin;
}

float NineSliceAxis(float coord, float quadBorderMin, float quadBorderMax, float textureBorderMin, float textureBorderMax)
{
    // "Min" side of axis
    if (coord < quadBorderMin)
        return Map(coord, 0.0f, quadBorderMin, 0.0f, textureBorderMin);

    // Middle part of axis
    if (coord < 1.0f - quadBorderMax)
        return Map(coord, quadBorderMin, 1.0f - quadBorderMax, textureBorderMin, 1.0f - textureBorderMax);

    // "Max" side of axis
    return Map(coord, 1.0f - quadBorderMax, 1.0f, 1.0f - textureBorderMax, 1.0f);
}

struct VertexOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    nointerpolation uint drawID : TEXCOORD1;
};

float4 main(VertexOutput input) : SV_Target
{
    DrawData drawData = _drawData[input.drawID];

    float2 textureDimension; // Size of sampled texture in pixels
    _textures[drawData.textureIndex.x].GetDimensions(textureDimension.x, textureDimension.y);

    float2 quadDimension = drawData.posAndSize.zw; // Size of drawn quad in pixels

    float2 textureSlicingOffsetXY = drawData.slicingOffset.xy / textureDimension;
    float2 textureSlicingOffsetZW = drawData.slicingOffset.zw / textureDimension;

    float2 quadSlicingOffsetXY = drawData.slicingOffset.xy / quadDimension;
    float2 quadSlicingOffsetZW = drawData.slicingOffset.zw / quadDimension;

    float2 scaledUV = float2(
        NineSliceAxis(input.uv.x, quadSlicingOffsetXY.x, quadSlicingOffsetZW.x, textureSlicingOffsetXY.x, textureSlicingOffsetZW.x),
        NineSliceAxis(input.uv.y, quadSlicingOffsetXY.y, quadSlicingOffsetZW.y, textureSlicingOffsetXY.y, textureSlicingOffsetZW.y)
    );

    float4 color = float4(0, 0, 0, 1);
    color.rgb = _textures[drawData.textureIndex.x].Sample(_sampler, scaledUV).rgb;

    return saturate(color);
}