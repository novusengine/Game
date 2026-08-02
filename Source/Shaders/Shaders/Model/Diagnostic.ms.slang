#include "DescriptorSet/Global.inc.slang"
#include "Model/ModelDiagnostic.inc.slang"

struct Constants
{
    uint workOffset;
    uint viewIndex;
    uint debugMode;
    uint reserved;
};

[[vk::push_constant]] Constants _constants;

struct MeshVertex
{
    float4 position : SV_Position;
    float3 color : COLOR;
};

[shader("mesh")]
[numthreads(64, 1, 1)]
[outputtopology("triangle")]
void main(uint3 groupID : SV_GroupID, uint3 threadID : SV_GroupThreadID,
          out vertices MeshVertex vertices[64], out indices uint3 triangles[124])
{
    DiagnosticMeshletWork work = _diagnosticWork[_constants.workOffset + groupID.x];
    ModelMeshlet meshlet = _modelMeshlets[work.meshletIndex];
    uint vertexCount = meshlet.packedCounts & 0xFFFFu;
    uint triangleCount = meshlet.packedCounts >> 16u;
    SetMeshOutputCounts(vertexCount, triangleCount);

    if (threadID.x < vertexCount)
    {
        uint vertexIndex = _modelMeshletVertexIndices[work.meshletVertexIndexBase + meshlet.vertexOffset + threadID.x];
        float3 localPosition = DecodePosition(_modelPositions[work.positionBase + vertexIndex], work);
        uint4 attributes = _modelVertexAttributes[work.vertexAttributeBase + vertexIndex];
        ModelInstanceRecord instance = _modelInstances[work.instanceIndex];
        float4 worldPosition = mul(float4(localPosition, 1.0f), instance.currentWorld);

        float3 color = SeedColor(work.colorSeed);
        if (_constants.debugMode == 1u)
        {
            float3 localNormal = DecodeOctahedralSnorm(attributes.x);
            float3 worldNormal = normalize(mul(float4(localNormal, 0.0f), instance.currentWorld).xyz);
            color = worldNormal * 0.5f + 0.5f;
        }
        else if (_constants.debugMode == 2u)
        {
            float2 uv = frac(DecodeHalf2(attributes.z));
            color = float3(uv, frac(uv.x + uv.y));
        }
        else if (_constants.debugMode == 3u)
        {
            float radius = max(meshlet.boundsRadius, 0.000001f);
            color = normalize(localPosition - meshlet.boundsCenter) * 0.5f + 0.5f;
            color *= saturate(length(localPosition - meshlet.boundsCenter) / radius);
        }
        else if (_constants.debugMode == 4u)
        {
            float4 cone = DecodeNormalCone(meshlet.packedNormalCone);
            color = (cone.xyz * 0.5f + 0.5f) * (0.5f + 0.5f * saturate(cone.w));
        }

        vertices[threadID.x].position = mul(worldPosition, _cameras[_constants.viewIndex].worldToClip);
        vertices[threadID.x].color = color;
    }

    for (uint triangleIndex = threadID.x; triangleIndex < triangleCount; triangleIndex += 64u)
    {
        uint packedTriangle = _modelMeshletTriangles[work.meshletTriangleBase + meshlet.triangleOffset + triangleIndex];
        triangles[triangleIndex] = uint3(packedTriangle & 0xFFu, (packedTriangle >> 8u) & 0xFFu,
                                         (packedTriangle >> 16u) & 0xFFu);
    }
}
