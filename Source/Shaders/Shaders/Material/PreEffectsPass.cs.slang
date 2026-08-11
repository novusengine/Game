
#include "DescriptorSet/Global.inc.slang"

#include "Include/Common.inc.slang"
#include "Include/TypedVisibility.inc.slang"
#include "Include/VisibilityBuffers.inc.slang"

struct Constants
{
    float4 renderInfo; // x = Render Width, y = Render Height, z = 1/Width, w = 1/Height
};

[[vk::push_constant]] Constants _constants;

[[vk::binding(0, PER_PASS)]] SamplerState _sampler;
[[vk::binding(3, PER_PASS)]] RWTexture2D<uint> _packedNormals;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 pixelPos = dispatchThreadId.xy;

    if (any(pixelPos >= uint2(_constants.renderInfo.xy)))
    {
        return;
    }

    uint vBufferData = LoadVisibilityBuffer(pixelPos);
    // Meshlet-model pixels are reconstructed by the typed-visibility pre-effects pass.
    if (IsModelVisibility(vBufferData))
    {
        return;
    }
    const VisibilityBuffer vBuffer = UnpackVisibilityBuffer(vBufferData);
    if (vBuffer.typeID != ObjectType::Terrain && vBuffer.typeID != ObjectType::ModelOpaque)
    {
        return;
    }

    PixelVertexData pixelVertexData = GetPixelVertexData(pixelPos, vBuffer, 0, _constants.renderInfo.xy);

    float2 oct = OctNormalEncode(normalize(pixelVertexData.worldNormal));
    _packedNormals[pixelPos] = f32tof16(oct.x) | (f32tof16(oct.y) << 16u);
}
