#include "DescriptorSet/Global.inc.slang"

#include "Include/Common.inc.slang"
#include "Include/TypedVisibility.inc.slang"
#include "Include/VisibilityBuffers.inc.slang"

struct Constants
{
    float4 renderInfo;
    float4x4 previousWorldToClip;
    uint cameraIndex;
    uint cameraValid;
};

[[vk::push_constant]] Constants _constants;

[[vk::binding(0, PER_PASS)]] SamplerState _sampler;
[[vk::binding(3, PER_PASS)]] RWTexture2D<uint> _motionVectors;

uint PackMotion(float2 motion)
{
    return f32tof16(motion.x) | (f32tof16(motion.y) << 16u);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 pixelPos = dispatchThreadId.xy;
    if (any(pixelPos >= uint2(_constants.renderInfo.xy)))
        return;

    uint visibility = LoadVisibilityBuffer(pixelPos);
    if (IsModelVisibility(visibility) || _constants.cameraValid == 0u)
        return;

    const VisibilityBuffer vBuffer = UnpackVisibilityBuffer(visibility);
    if (vBuffer.typeID != ObjectType::Terrain && vBuffer.typeID != ObjectType::ModelOpaque)
        return;

    PixelVertexData pixel = GetPixelVertexData(pixelPos, vBuffer, 0, _constants.renderInfo.xy);
    float4 currentClip = mul(float4(pixel.worldPos, 1.0f), _cameras[_constants.cameraIndex].worldToClip);
    float4 previousClip = mul(float4(pixel.worldPos, 1.0f), _constants.previousWorldToClip);
    if (currentClip.w <= 0.0f || previousClip.w <= 0.0f)
        return;

    _motionVectors[pixelPos] = PackMotion(currentClip.xy / currentClip.w - previousClip.xy / previousClip.w);
}
