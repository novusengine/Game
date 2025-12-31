permutation TEX_TYPE = [float, float2, float3, float4, int, int2, int3, int4, uint, uint2, uint3, uint4];

#include "Include/Common.inc.slang"

[[vk::binding(0, PER_PASS)]] SamplerState _sampler;
[[vk::binding(1, PER_PASS)]] Texture2D<TEX_TYPE> _texture;

struct Constants
{
    float4 colorMultiplier;
    float4 additiveColor;
    float4 uvOffsetAndExtent;
    uint channelRedirectors;
};

[[vk::push_constant]] Constants _constants;

struct VSOutput
{
    float2 uv : TEXCOORD0;
};

[shader("fragment")]
float4 main(VSOutput input) : SV_Target
{
    float2 dimensions;
    _texture.GetDimensions(dimensions.x, dimensions.y);

    float2 uv = float2(input.uv.x, 1.0 - input.uv.y);
    uv = uv * _constants.uvOffsetAndExtent.zw + _constants.uvOffsetAndExtent.xy;

    int3 location = int3((int2)(uv * dimensions), 0);

    float4 color = ToFloat4(_texture.Load(location), 1.0f);

    int rRedirector = _constants.channelRedirectors & 0xFF;
    int gRedirector = (_constants.channelRedirectors >> 8) & 0xFF;
    int bRedirector = (_constants.channelRedirectors >> 16) & 0xFF;
    int aRedirector = (_constants.channelRedirectors >> 24) & 0xFF;

    color = color * _constants.colorMultiplier + _constants.additiveColor;

    float4 redirectedColor = float4(0, 0, 0, 0);
    redirectedColor.r += color[rRedirector];
    redirectedColor.g += color[gRedirector];
    redirectedColor.b += color[bRedirector];
    redirectedColor.a += color[aRedirector];

    return redirectedColor;
}