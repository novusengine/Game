#include "Include/VisibilityBuffers.inc.slang"
#include "Include/TypedVisibility.inc.slang"
#include "Model/ModelVisibilityReconstruct.inc.slang"

struct Constants
{
    float4 renderInfo;
    uint resourceIndex;
    uint debugMode;
};
[[vk::push_constant]] Constants _constants;

[[vk::binding(3, PER_PASS)]] RWTexture2D<float4> _modelVisibilityResolvedColor;

uint MixID(uint seed, uint value)
{
    seed ^= value + 0x9E3779B9u + (seed << 6u) + (seed >> 2u);
    seed ^= seed >> 16u;
    seed *= 0x7FEB352Du;
    return seed ^ (seed >> 15u);
}

float3 IDColor(uint value)
{
    uint seed = MixID(0xA511E9B3u, value);
    return 0.2f + 0.8f * float3(seed & 0xFFu, (seed >> 8u) & 0xFFu, (seed >> 16u) & 0xFFu) / 255.0f;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 pixelPos = dispatchThreadID.xy;
    if (any(pixelPos >= uint2(_constants.renderInfo.xy)))
        return;

    uint targetValue = LoadVisibilityBuffer(pixelPos);
    if (!IsModelVisibility(targetValue))
        return;

    TypedVisibilityPayload payload = UnpackTypedVisibility(targetValue);
    ReconstructedModelPixel pixel;
    if (!ReconstructModelPixel(payload, float2(pixelPos) + 0.5f, _constants.renderInfo.xy, 0u,
                               _constants.resourceIndex, pixel))
    {
        _modelVisibilityResolvedColor[pixelPos] = float4(1.0f, 0.0f, 1.0f, 1.0f);
        return;
    }

    float3 color;
    switch (_constants.debugMode)
    {
        case 0u: color = IDColor(pixel.record.instanceIndex); break;
        case 1u: color = IDColor(pixel.materialIndex); break;
        case 2u: color = IDColor(pixel.record.lodIndex); break;
        case 3u: color = IDColor(pixel.record.submeshIndex); break;
        case 4u: color = IDColor(pixel.record.meshletIndex); break;
        case 5u: color = IDColor(pixel.triangleID); break;
        case 6u: color = pixel.worldNormal * 0.5f + 0.5f; break;
        case 7u: color = float3(frac(pixel.uv0), frac(pixel.uv0.x + pixel.uv0.y)); break;
        default: color = float3(1.0f, 0.0f, 1.0f); break;
    }
    _modelVisibilityResolvedColor[pixelPos] = float4(color, 1.0f);
}
