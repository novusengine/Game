
#include "DescriptorSet/Global.inc.slang"

struct VertexInput
{
    uint vertexID : SV_VulkanVertexID;
};

struct VertexOutput
{
    float4 position : SV_POSITION;
};

[shader("vertex")]
VertexOutput main(VertexInput input)
{
    const float3 GRID_PLANE[6] = {
        float3(1, 1, 0), float3(-1, -1, 0), float3(-1, 1, 0),
        float3(-1, -1, 0), float3(1, 1, 0), float3(1, -1, 0)
    };

    float3 position = GRID_PLANE[input.vertexID];

    VertexOutput output;
    output.position = float4(position, 1.0f); // Use position directly

    return output;
}
