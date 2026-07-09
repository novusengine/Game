#include "DescriptorSet/Global.inc.slang"

#include "Include/Common.inc.slang"

// Reduces the scene depth buffer to global min/max depth for SDSM cascade fitting.
// Reversed Z: max depth bits = nearest sample, min depth bits = farthest sample.
// The buffer is reset to the sentinel { 0xFFFFFFFF, 0 } before dispatch, uint ordering
// matches float ordering for depth in [0, 1].

[[vk::binding(0, PER_PASS)]] Texture2D<float> _depth;
[[vk::binding(1, PER_PASS)]] RWStructuredBuffer<uint> _depthMinMax; // [0] = min depth bits (farthest), [1] = max depth bits (nearest)

groupshared uint gMinDepth;
groupshared uint gMaxDepth;

struct CSInput
{
    uint3 dispatchThreadID : SV_DispatchThreadID;
    uint groupIndex : SV_GroupIndex;
};

[shader("compute")]
[numthreads(16, 16, 1)]
void main(CSInput input)
{
    if (input.groupIndex == 0)
    {
        gMinDepth = 0xFFFFFFFFu;
        gMaxDepth = 0u;
    }
    GroupMemoryBarrierWithGroupSync();

    uint2 dim;
    _depth.GetDimensions(dim.x, dim.y);

    uint2 pixel = min(input.dispatchThreadID.xy, dim - 1);
    float depth = _depth[pixel];

    // Cleared depth is exactly 0.0 (reversed Z far plane), those pixels are sky and must not drive the bounds
    bool isValid = depth != 0.0f;
    float minCandidate = isValid ? depth : 1.0f;
    float maxCandidate = isValid ? depth : 0.0f;

    float waveMin = WaveActiveMin(minCandidate);
    float waveMax = WaveActiveMax(maxCandidate);

    if (WaveIsFirstLane())
    {
        InterlockedMin(gMinDepth, asuint(waveMin));
        InterlockedMax(gMaxDepth, asuint(waveMax));
    }

    GroupMemoryBarrierWithGroupSync();

    if (input.groupIndex == 0)
    {
        InterlockedMin(_depthMinMax[0], gMinDepth);
        InterlockedMax(_depthMinMax[1], gMaxDepth);
    }
}
