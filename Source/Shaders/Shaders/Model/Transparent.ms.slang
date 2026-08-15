#include "DescriptorSet/Global.inc.slang"
#include "Model/ModelTransparentWork.inc.slang"
#include "Model/ModelVisibility.inc.slang"

struct Constants
{
    float4 shadowSettings;
    float4 fogColor;
    float4 fogSettings;
    uint viewIndex;
    uint numDirectionalLights;
    uint shadowsReady;
    uint resourceIndex;
    uint binIndex;
    uint binCapacity;
};
[[vk::push_constant]] Constants _constants;

[[vk::binding(0, PER_PASS)]] StructuredBuffer<ModelVisibilityRecord> _transparentRecords0;
[[vk::binding(1, PER_PASS)]] StructuredBuffer<ModelVisibilityRecord> _transparentRecords1;
[[vk::binding(2, PER_PASS)]] StructuredBuffer<uint> _transparentStats0;
[[vk::binding(3, PER_PASS)]] StructuredBuffer<uint> _transparentStats1;
[[vk::binding(4, PER_PASS)]] StructuredBuffer<ModelInstanceRecord> _transparentRasterInstances;
[[vk::binding(5, PER_PASS)]] StructuredBuffer<ModelRecord> _transparentRasterModels;
[[vk::binding(6, PER_PASS)]] StructuredBuffer<ModelMesh> _transparentRasterMeshes;
[[vk::binding(7, PER_PASS)]] StructuredBuffer<ModelLOD> _transparentRasterLODs;
[[vk::binding(8, PER_PASS)]] StructuredBuffer<ModelSubmesh> _transparentRasterSubmeshes;
[[vk::binding(9, PER_PASS)]] StructuredBuffer<ModelMeshlet> _transparentRasterMeshlets;
[[vk::binding(10, PER_PASS)]] StructuredBuffer<uint2> _transparentRasterPositions;
[[vk::binding(11, PER_PASS)]] StructuredBuffer<uint4> _transparentRasterVertexAttributes;
[[vk::binding(12, PER_PASS)]] StructuredBuffer<uint> _transparentRasterVertexIndices;
[[vk::binding(13, PER_PASS)]] StructuredBuffer<uint> _transparentRasterTriangles;
[[vk::binding(14, PER_PASS)]] StructuredBuffer<uint> _transparentRasterMaterialTable;

struct MeshVertex
{
    float4 position : SV_Position;
    float3 worldPosition : TEXCOORD0;
    float3 geometricNormal : TEXCOORD1;
    float2 uv0 : TEXCOORD2;
    nointerpolation uint materialInstanceIndex : TEXCOORD3;
    nointerpolation float opacity : TEXCOORD4;
    nointerpolation uint instanceIndex : TEXCOORD5;
};

[shader("mesh")]
[numthreads(64, 1, 1)]
[outputtopology("triangle")]
void main(uint3 groupID : SV_GroupID, uint3 threadID : SV_GroupThreadID,
          out vertices MeshVertex vertices[64], out indices uint3 triangles[124])
{
    uint queueIndex = groupID.x + groupID.y * MODEL_MESH_DISPATCH_MAX_GROUPS_X;
    uint queueCount = _constants.resourceIndex == 0u
        ? _transparentStats0[MODEL_TRANSPARENT_STAT_QUEUE_COUNTS + _constants.binIndex]
        : _transparentStats1[MODEL_TRANSPARENT_STAT_QUEUE_COUNTS + _constants.binIndex];
    if (queueIndex >= min(queueCount, _constants.binCapacity))
    {
        SetMeshOutputCounts(0, 0);
        return;
    }

    uint recordIndex = _constants.binIndex * _constants.binCapacity + queueIndex;
    ModelVisibilityRecord packedRecord = _constants.resourceIndex == 0u ? _transparentRecords0[recordIndex]
                                                                         : _transparentRecords1[recordIndex];
    DecodedModelVisibilityRecord record = DecodeModelVisibilityRecord(packedRecord);
    ModelInstanceRecord instance = _transparentRasterInstances[record.instanceIndex];
    ModelRecord model = _transparentRasterModels[instance.modelIndex];
    ModelMesh mesh = _transparentRasterMeshes[model.meshBase + record.meshIndex];
    ModelLOD lod = _transparentRasterLODs[model.meshLODBase + mesh.lodOffset + record.lodIndex];
    ModelSubmesh submesh = _transparentRasterSubmeshes[model.submeshBase + lod.submeshOffset + record.submeshIndex];
    ModelMeshlet meshlet = _transparentRasterMeshlets[model.meshletBase + record.meshletIndex];
    uint materialInstanceIndex = _transparentRasterMaterialTable[instance.materialTableOffset + mesh.materialSlotOffset + submesh.materialSlotIndex];
    uint vertexCount = ModelMeshletVertexCount(meshlet);
    uint triangleCount = ModelMeshletTriangleCount(meshlet);
    SetMeshOutputCounts(vertexCount, triangleCount);

    if (threadID.x < vertexCount)
    {
        uint vertexIndex = _transparentRasterVertexIndices[model.meshletVertexIndexBase + meshlet.vertexOffset + threadID.x];
        float3 localPosition = DecodePosition(_transparentRasterPositions[model.positionBase + lod.vertexOffset + vertexIndex],
                                              mesh.positionDecodeOffset, mesh.positionDecodeExtent);
        uint4 attributes = _transparentRasterVertexAttributes[model.vertexAttributeBase + lod.vertexAttributeOffset + vertexIndex];
        float4 worldPosition = mul(float4(localPosition, 1.0f), instance.currentWorld);
        vertices[threadID.x].position = mul(worldPosition, _cameras[_constants.viewIndex].worldToClip);
        vertices[threadID.x].worldPosition = worldPosition.xyz;
        vertices[threadID.x].geometricNormal = normalize(mul(float4(DecodeOctahedralSnorm(attributes.x), 0.0f), instance.currentWorld).xyz);
        vertices[threadID.x].uv0 = DecodeHalf2(attributes.z);
        vertices[threadID.x].materialInstanceIndex = materialInstanceIndex;
        vertices[threadID.x].opacity = instance.opacity;
        vertices[threadID.x].instanceIndex = record.instanceIndex;
    }

    for (uint triangleIndex = threadID.x; triangleIndex < triangleCount; triangleIndex += 64u)
    {
        uint packedTriangle = _transparentRasterTriangles[model.meshletTriangleBase + meshlet.triangleOffset + triangleIndex];
        triangles[triangleIndex] = DecodeModelMeshletTriangle(packedTriangle);
    }
}
