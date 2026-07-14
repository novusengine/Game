#include "Shadows/SVSM.inc.slang"

// Draws a downsampled view of the physical page pool straight into the scene color corner:
// grayscale reversed-Z depth (bright = close to the sun), untouched texels stay black.

struct DebugConstants
{
    uint poolSize;    // Pool texture resolution
    uint regionSize;  // On-screen size of the debug view
    uint padding0;
    uint padding1;
    int2 screenOffset;
};

[[vk::push_constant]] DebugConstants _constants;

[[vk::binding(0, PER_PASS)]] Texture2D<uint> _pagePool;
[[vk::binding(1, PER_PASS)]] RWTexture2D<float4> _target;

struct CSInput
{
    uint3 dispatchThreadID : SV_DispatchThreadID;
};

[shader("compute")]
[numthreads(16, 16, 1)]
void main(CSInput input)
{
    uint regionSize = _constants.regionSize;
    if (any(input.dispatchThreadID.xy >= uint2(regionSize, regionSize)))
    {
        return;
    }

    uint2 targetDim;
    _target.GetDimensions(targetDim.x, targetDim.y);
    int2 targetPixel = _constants.screenOffset + int2(input.dispatchThreadID.xy);
    if (any(targetPixel < int2(0, 0)) || any(targetPixel >= int2(targetDim)))
    {
        return;
    }

    uint2 poolTexel = input.dispatchThreadID.xy * (_constants.poolSize / regionSize);
    float depth = asfloat(_pagePool[poolTexel]);

    // sqrt spreads the reversed-Z range so distant depth stays visible
    float value = sqrt(saturate(depth));
    _target[uint2(targetPixel)] = float4(value, value, value, 1.0f);
}
