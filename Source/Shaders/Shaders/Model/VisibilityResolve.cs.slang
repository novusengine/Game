#include "Include/VisibilityBuffers.inc.slang"
#include "Include/TypedVisibility.inc.slang"
#include "Model/ModelVisibilityReconstruct.inc.slang"

struct Constants
{
    float4 renderInfo;
    uint resourceIndex;
    uint viewIndex;
    uint debugMode;
};
[[vk::push_constant]] Constants _constants;

[[vk::binding(3, PER_PASS)]] RWTexture2D<float4> _modelVisibilityResolvedColor;
[[vk::binding(19, PER_PASS)]] StructuredBuffer<uint> _resolvedModelCullReasons0;
[[vk::binding(20, PER_PASS)]] StructuredBuffer<uint> _resolvedModelCullReasons1;

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

float3 CullReasonColor(uint reason)
{
    if (reason == MODEL_CULL_REASON_FRUSTUM) return float3(1.0f, 0.05f, 0.05f);
    if (reason == MODEL_CULL_REASON_CONE) return float3(1.0f, 0.45f, 0.0f);
    if (reason == MODEL_CULL_REASON_OCCLUSION) return float3(0.05f, 0.35f, 1.0f);
    return float3(1.0f, 0.0f, 1.0f);
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
    if (!ReconstructModelPixel(payload, float2(pixelPos) + 0.5f, _constants.renderInfo.xy, _constants.viewIndex,
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
        case 8u:
        {
            uint reason = _constants.resourceIndex == 0u ? _resolvedModelCullReasons0[payload.recordID] : _resolvedModelCullReasons1[payload.recordID];
            color = CullReasonColor(reason);
            break;
        }
        default: color = float3(1.0f, 0.0f, 1.0f); break;
    }
    _modelVisibilityResolvedColor[pixelPos] = float4(color, 1.0f);
}
