
#include "DescriptorSet/Global.inc.slang"

struct Constants
{
    float fadeStart;
    float fadeEnd;
};

[[vk::push_constant]] Constants _constants;

struct VertexOutput
{
    float4 position : SV_POSITION;
    float3 nearPoint : TEXCOORD0;
    float3 farPoint : TEXCOORD1;
};

struct PSOutput
{
    float4 color : SV_Target0;

};

float4 Grid(float3 pos, float scale, bool drawAxis)
{
    float2 coord = pos.xz * scale; // Use the scale variable to set the distance between the lines
    float2 derivative = fwidth(coord);
    float2 grid = abs(frac(coord - 0.5f) - 0.5f) / derivative;
    float minGrid = min(grid.x, grid.y);
    float minimumZ = min(derivative.y, 1.0f);
    float minimumX = min(derivative.x, 1.0f);

    float4 color = float4(0.2f, 0.2f, 0.2f, 1.0f - min(minGrid, 1.0f));

    // Z Axis
    if (pos.x > -0.1f * minimumX && pos.x < 0.1f * minimumX && drawAxis)
    {
        color.z = 1.0f;
    }

    // X Axis
    if (pos.z > -0.1f * minimumZ && pos.z < 0.1f * minimumZ && drawAxis)
    {
        color.x = 1.0f;
    }

    return color;
}

[shader("fragment")]
PSOutput main(VertexOutput input) : SV_Target
{
    float t = -input.nearPoint.y / (input.farPoint.y - input.nearPoint.y);

    clip(t);

    float3 worldPos = input.nearPoint + t * (input.farPoint - input.nearPoint);

    const float fadeStart = _constants.fadeStart;
    const float fadeEnd = _constants.fadeEnd;

    float distX = distance(worldPos.x, _cameras[0].eyePosition.x);
    float fadingX = clamp((fadeEnd - distX) / (fadeEnd - fadeStart), 0.0f, 0.3f);

    float distZ = distance(worldPos.z, _cameras[0].eyePosition.z);
    float fadingZ = clamp((fadeEnd - distZ) / (fadeEnd - fadeStart), 0.0f, 0.3f);

    float fading = min(fadingX, fadingZ);

    PSOutput output;
    output.color = Grid(worldPos, 1.0f, true) + Grid(worldPos, 10.0f, true);
    output.color.a *= fading;
    return output;
}