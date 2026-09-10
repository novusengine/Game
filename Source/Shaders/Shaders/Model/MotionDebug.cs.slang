#include "Include/Common.inc.slang"
#include "Include/TypedVisibility.inc.slang"
#include "Include/Motion.inc.slang"

struct Constants { uint2 dimensions; uint mode; uint padding; };
[[vk::push_constant]] Constants _constants;
[[vk::binding(0, PER_PASS)]] Texture2D<uint> _debugMotion;
[[vk::binding(1, PER_PASS)]] Texture2D<uint> _debugVisibility;
[[vk::binding(2, PER_PASS)]] RWTexture2D<float4> _debugColor;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 thread : SV_DispatchThreadID)
{
    uint2 pixel = thread.xy;
    if (any(pixel >= _constants.dimensions) || !IsModelVisibility(_debugVisibility[pixel]))
        return;
    float2 motion;
    bool valid = UnpackValidMotion(_debugMotion[pixel], motion);
    float3 color = float3(1.0f, 0.0f, 0.65f);
    if (valid)
        color = _constants.mode == 2u ? float3(0.1f, 0.9f, 0.2f) : float3(saturate(0.5f + motion * 20.0f), saturate(length(motion) * 30.0f));
    _debugColor[pixel] = float4(color, 1.0f);
}
