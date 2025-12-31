
#include "DescriptorSet/Global.inc.slang"

#include "Include/Common.inc.slang"
#include "Include/OIT.inc.slang"

struct Constants
{
    float4 shallowOceanColor;
    float4 deepOceanColor;
    float4 shallowRiverColor;
    float4 deepRiverColor;
    float liquidVisibilityRange;
    float currentTime;
};

[[vk::push_constant]] Constants _constants;

struct PackedDrawCallData
{
    uint packed0; // u16 chunkID, u16 cellID
    uint packed1; // u16 textureStartIndex, u8 textureCount, u8 hasDepth
    uint packed2; // u16 liquidType, u16 padding
    uint packed3; // f16 uvAnim scrolling, f16 uvAnim rotation
};

struct DrawCallData
{
    uint chunkID;
    uint cellID;
    uint textureStartIndex;
    uint textureCount;
    uint hasDepth;
    uint liquidType;
    float2 uvAnim;
};

[[vk::binding(0, PER_PASS)]] StructuredBuffer<PackedDrawCallData> _drawCallDatas;

DrawCallData LoadDrawCallData(uint drawCallID)
{
    PackedDrawCallData packedDrawCallData = _drawCallDatas[drawCallID];

    DrawCallData drawCallData;
    drawCallData.chunkID = ((packedDrawCallData.packed0 >> 0) & 0xFFFF);
    drawCallData.cellID = ((packedDrawCallData.packed0 >> 16) & 0xFFFF);
    drawCallData.textureStartIndex = ((packedDrawCallData.packed1 >> 0) & 0xFFFF);
    drawCallData.textureCount = ((packedDrawCallData.packed1 >> 16) & 0xFF);
    drawCallData.hasDepth = ((packedDrawCallData.packed1 >> 24) & 0xFF);
    drawCallData.liquidType = ((packedDrawCallData.packed2 >> 0) & 0xFFFF);
    drawCallData.uvAnim.x = f16tof32(packedDrawCallData.packed3);
    drawCallData.uvAnim.y = f16tof32(packedDrawCallData.packed3 >> 16);

    return drawCallData;
}

[[vk::binding(2, PER_PASS)]] Texture2D<float> _depthRT;
[[vk::binding(3, PER_PASS)]] SamplerState _sampler;
[[vk::binding(4, PER_PASS)]] Texture2D<float4> _textures[512];

struct PSInput
{
    float4 pixelPos : SV_Position;
    float2 textureUV : TEXCOORD0;
    uint drawCallID : TEXCOORD1;
    float3 posViewSpace : TEXCOORD2;
};

struct PSOutput
{
    float4 transparency : SV_Target0;
    float4 transparencyWeight : SV_Target1;
};

float2 Rot2(float2 p, float degree)
{
    float a = radians(degree);
    return mul(p, float2x2(cos(a), -sin(a), sin(a), cos(a)));
}

[shader("fragment")]
PSOutput main(PSInput input)
{
    DrawCallData drawCallData = LoadDrawCallData(input.drawCallID);

    uint textureAnimationOffset = fmod(ceil(_constants.currentTime * drawCallData.textureCount), drawCallData.textureCount);

    // We need to get the depth of the opaque pixel "under" this liquid pixel
    float2 dimensions;
    _depthRT.GetDimensions(dimensions.x, dimensions.y);

    float2 pixelUV = input.pixelPos.xy / dimensions;

    float4 color = float4(1, 0, 0, 1);
    float liquidDepth = input.pixelPos.z / input.pixelPos.w;

    if (drawCallData.liquidType == 2 || drawCallData.liquidType == 3) // Lava or Slime
    {
        float2 textureUV = float2(input.textureUV.x, input.textureUV.y);

        // Add slow scrolling effect
        textureUV += /*drawCallData.uvAnim*/ float2(0.0f, 1.0f) * (_constants.currentTime) * 0.05f;

        color = _textures[drawCallData.textureStartIndex + textureAnimationOffset].Sample(_sampler, textureUV);
    }
    else
    {
        // Get the depths
        float opaqueDepth = _depthRT.Load(int3(input.pixelPos.xy, 0)); // 0.0 .. 1.0

        float linearDepthDifference = LinearizeDepth((1.0f - opaqueDepth), 0.1f, 100000.0f) - LinearizeDepth((1.0f - (liquidDepth)), 0.1f, 100000.0f);
        float blendFactor = clamp(linearDepthDifference, 0.0f, _constants.liquidVisibilityRange) / _constants.liquidVisibilityRange;

        // Blend color
        float4 shallowColor = lerp(_constants.shallowRiverColor, _constants.shallowOceanColor, drawCallData.liquidType);
        float4 deepColor = lerp(_constants.deepRiverColor, _constants.deepOceanColor, drawCallData.liquidType);

        color = lerp(shallowColor, deepColor, blendFactor);

        // Animate the texture UV
        float2 textureUV = input.textureUV;//Rot2(input.textureUV * drawCallData.uvAnim.x, drawCallData.uvAnim.y);

        float3 texture0 = _textures[drawCallData.textureStartIndex + textureAnimationOffset].Sample(_sampler, textureUV).rgb;

        color.rgb = saturate(color.rgb + texture0);
    }

    // Calculate OIT weight and output
    float clipSpaceDepth = liquidDepth;
    float viewSpaceDepth = input.posViewSpace.z;
    float oitWeight = CalculateOITWeight(color, clipSpaceDepth, viewSpaceDepth);

    PSOutput output;
    output.transparency = float4(color.rgb * color.a, color.a) * oitWeight;
    output.transparencyWeight.a = color.a;

    return output;
}