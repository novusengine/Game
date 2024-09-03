
struct PackedCharData
{
    int4 data; // x = textureID, y = textColor, z = outlineColor, w = (float)outlineWidth
};

[[vk::binding(1, PER_PASS)]] SamplerState _sampler;

[[vk::binding(2, PER_PASS)]] StructuredBuffer<PackedCharData> _charData;
[[vk::binding(3, PER_PASS)]] Texture2D<float4> _textures[4096];

struct CharData
{
    int textureID;
    float4 textColor;
    float4 outlineColor;
    float outlineWidth;
};

CharData GetCharData(uint charIndex)
{
    PackedCharData packedCharData = _charData[charIndex];
    CharData charData;
    charData.textureID = packedCharData.data.x;
    charData.textColor = float4((packedCharData.data.y >> 24) & 0xFF, (packedCharData.data.y >> 16) & 0xFF, (packedCharData.data.y >> 8) & 0xFF, (packedCharData.data.y >> 0) & 0xFF) / 255.0;
    charData.outlineColor = float4((packedCharData.data.z >> 24) & 0xFF, (packedCharData.data.z >> 16) & 0xFF, (packedCharData.data.z >> 8) & 0xFF, (packedCharData.data.z >> 0) & 0xFF) / 255.0;
    charData.outlineWidth = asfloat(packedCharData.data.w);
    return charData;
}

struct VertexOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    uint charIndex : TEXCOORD1;
};

float4 main(VertexOutput input) : SV_Target
{
    CharData charData = GetCharData(input.charIndex);

    float distance = _textures[charData.textureID].SampleLevel(_sampler, input.uv, 0).r;
    float smoothWidth = fwidth(distance);
    float alpha = smoothstep(0.5 - smoothWidth, 0.5 + smoothWidth, distance);
    float3 rgb = float3(alpha, alpha, alpha) * charData.textColor.rgb;

    if (charData.outlineWidth > 0.0)
    {
        float w = 1.0 - charData.outlineWidth;
        alpha = smoothstep(w - smoothWidth, w + smoothWidth, distance);
        rgb += lerp(float3(alpha, alpha, alpha), charData.outlineColor.rgb, alpha);
    }

    return float4(rgb, alpha);
}