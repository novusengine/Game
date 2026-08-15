#include "DescriptorSet/Global.inc.slang"
#include "Material/Fog.inc.slang"

struct Constants
{
    uint2 dimensions;
    uint viewIndex;
    uint reserved;
    float4 fogColor;
    float4 fogSettings;
};
[[vk::push_constant]] Constants _constants;

[[vk::binding(0, PER_PASS)]] Texture2D<float> _selectionDepth;
[[vk::binding(1, PER_PASS)]] Texture2D<float> _selectionOpaqueDepth;
[[vk::binding(2, PER_PASS)]] Texture2D<float> _selectionRevealage;
[[vk::binding(3, PER_PASS)]] RWTexture2D<float4> _selectionSceneColor;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadID.xy;
    if (any(pixel >= _constants.dimensions))
        return;

    float centerDepth = _selectionDepth.Load(uint3(pixel, 0));
    float nearestSelectedDepth = centerDepth;
    bool edge = false;
    const int2 offsets[4] = { int2(-1, 0), int2(1, 0), int2(0, -1), int2(0, 1) };
    for (uint index = 0; index < 4u; ++index)
    {
        int2 neighbor = clamp(int2(pixel) + offsets[index], int2(0, 0), int2(_constants.dimensions) - 1);
        float depth = _selectionDepth.Load(uint3(uint2(neighbor), 0));
        nearestSelectedDepth = max(nearestSelectedDepth, depth);
        edge = edge || (centerDepth == 0.0f && depth > 0.0f);
    }
    if (!edge)
        return;

    float opaqueDepth = _selectionOpaqueDepth.Load(uint3(pixel, 0));
    if (opaqueDepth > nearestSelectedDepth)
        return;

    float attenuation = saturate(_selectionRevealage.Load(uint3(pixel, 0)));
    float2 clipXY = (float2(pixel) + 0.5f) / float2(_constants.dimensions) * 2.0f - 1.0f;
    clipXY.y = -clipXY.y;
    float4 worldPositionH = mul(float4(clipXY, nearestSelectedDepth, 1.0f),
                                _cameras[_constants.viewIndex].clipToWorld);
    float3 worldPosition = worldPositionH.xyz / worldPositionH.w;
    float fogVisibility = 1.0f - FogIntensity(worldPosition, _cameras[_constants.viewIndex].eyePosition.xyz,
                                              _constants.fogSettings);
    float4 color = _selectionSceneColor[pixel];
    color.rgb += float3(0.35f, 0.35f, 0.35f) * attenuation * fogVisibility;
    _selectionSceneColor[pixel] = color;
}
