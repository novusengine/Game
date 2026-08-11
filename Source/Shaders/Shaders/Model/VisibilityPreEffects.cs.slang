#include "Include/VisibilityBuffers.inc.slang"
#include "Include/TypedVisibility.inc.slang"
#define MODEL_VISIBILITY_NO_MATERIAL
#include "Model/ModelVisibilityReconstruct.inc.slang"

struct Constants
{
    float4 renderInfo;
    uint resourceIndex;
    uint viewIndex;
};
[[vk::push_constant]] Constants _constants;

[[vk::binding(3, PER_PASS)]] RWTexture2D<uint> _modelPackedNormals;

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

    ReconstructedModelPixel pixel;
    if (!ReconstructModelPixel(UnpackTypedVisibility(targetValue), float2(pixelPos) + 0.5f,
                               _constants.renderInfo.xy, _constants.viewIndex, _constants.resourceIndex, pixel))
        return;
    float2 oct = OctNormalEncode(normalize(pixel.worldNormal));
    _modelPackedNormals[pixelPos] = f32tof16(oct.x) | (f32tof16(oct.y) << 16u);
}
