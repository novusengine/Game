#include "Include/VisibilityBuffers.inc.slang"
#include "Include/TypedVisibility.inc.slang"
#include "Include/Motion.inc.slang"
#define MODEL_VISIBILITY_NO_MATERIAL
#define MODEL_VISIBILITY_RECONSTRUCT_PREVIOUS_POSITION
#include "Model/ModelVisibilityReconstruct.inc.slang"

struct Constants
{
    float4 renderInfo;
    float4x4 previousWorldToClip;
    uint resourceIndex;
    uint viewIndex;
    uint cameraValid;
};
[[vk::push_constant]] Constants _constants;

[[vk::binding(3, PER_PASS)]] RWTexture2D<uint> _modelMotionVectors;

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

    _modelMotionVectors[pixelPos] = INVALID_MOTION;
    if (_constants.cameraValid == 0u)
        return;

    ReconstructedModelPixel pixel;
    if (!ReconstructModelPixel(UnpackTypedVisibility(targetValue), float2(pixelPos) + 0.5f,
                               _constants.renderInfo.xy, _constants.viewIndex, _constants.resourceIndex, pixel) ||
        (pixel.instance.flags & MODEL_INSTANCE_MOTION_VALID) == 0u || !pixel.motionValid)
        return;

    float4 currentClip = mul(float4(pixel.worldPosition, 1.0f), _cameras[_constants.viewIndex].worldToClip);
    float4 previousClip = mul(float4(pixel.previousWorldPosition, 1.0f), _constants.previousWorldToClip);
    if (currentClip.w <= 0.0f || previousClip.w <= 0.0f)
        return;

    _modelMotionVectors[pixelPos] = PackMotion(currentClip.xy / currentClip.w - previousClip.xy / previousClip.w);
}
