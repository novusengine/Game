
#include "DescriptorSet/Global.inc.slang"

struct VertexInput
{
    uint vertexID : SV_VulkanVertexID;
};

struct VertexOutput
{
    float4 position : SV_POSITION;
    float3 nearPoint : TEXCOORD0;
    float3 farPoint : TEXCOORD1;
};

float3 UnprojectPoint(float2 xy, float z, Camera camera)
{
    float4 unprojectedPoint = mul(float4(xy, z, 1.0f), camera.clipToWorld);
    return unprojectedPoint.xyz / unprojectedPoint.w;
}

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
    output.nearPoint = UnprojectPoint(position.xy, 1.0f, _cameras[0]);
    output.farPoint = UnprojectPoint(position.xy, 0.0f, _cameras[0]);

    return output;
}