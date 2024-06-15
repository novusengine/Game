
#include "common.inc.hlsl"
#include "globalData.inc.hlsl"
#include "Include/VisibilityBuffers.inc.hlsl"

[[vk::binding(0, PER_PASS)]] SamplerState _sampler;
[[vk::binding(3, PER_PASS)]] RWTexture2D<float4> _packedNormals;

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 pixelPos = dispatchThreadId.xy;

    float2 dimensions;
    _packedNormals.GetDimensions(dimensions.x, dimensions.y);

    if (any(pixelPos > dimensions))
    {
        return;
    }

    uint4 vBufferData = LoadVisibilityBuffer(pixelPos);
    const VisibilityBuffer vBuffer = UnpackVisibilityBuffer(vBufferData);

    PixelVertexData pixelVertexData = GetPixelVertexData(pixelPos, vBuffer);

    float3 normal = pixelVertexData.worldNormal;

    // Our normal buffer is R11G11B10_UFLOAT, it can't store negative values so we remap it to [0, 1] where 0.5 is 0
    float3 packedNormals = float3(0.5f, 0.5f, 0.5f) + (normal * float3(0.5f, 0.5f, 0.5f));
    _packedNormals[pixelPos] = float4(packedNormals, 0.0f);
}