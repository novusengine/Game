struct Constants
{
    uint2 renderSize;
};
[[vk::push_constant]] Constants _constants;

[[vk::binding(0, PER_PASS)]] Texture2D<float4> _transparencyAccumulation;
[[vk::binding(1, PER_PASS)]] Texture2D<float> _transparencyRevealage;
[[vk::binding(2, PER_PASS)]] RWTexture2D<float4> _transparencySceneColor;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadID.xy;
    if (any(pixel >= _constants.renderSize))
        return;

    float4 accumulation = _transparencyAccumulation.Load(uint3(pixel, 0));
    float revealage = _transparencyRevealage.Load(uint3(pixel, 0));
    float3 transparentColor = accumulation.rgb / max(accumulation.a, 1e-5f);
    float4 sceneColor = _transparencySceneColor[pixel];
    sceneColor.rgb = transparentColor * (1.0f - revealage) + sceneColor.rgb * revealage;
    _transparencySceneColor[pixel] = sceneColor;
}
