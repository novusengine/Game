#include "DescriptorSet/Global.inc.slang"
#include "DescriptorSet/Material.inc.slang"
#include "Model/ModelVisibility.inc.slang"

struct Constants
{
    uint queueIndex;
    uint viewIndex;
    uint resourceIndex;
};
[[vk::push_constant]] Constants _constants;

[[vk::binding(0, PER_PASS)]] StructuredBuffer<uint> _modelRasterQueueSolidOneSided0;
[[vk::binding(1, PER_PASS)]] StructuredBuffer<uint> _modelRasterQueueSolidTwoSided0;
[[vk::binding(2, PER_PASS)]] StructuredBuffer<uint> _modelRasterQueueAlphaTestOneSided0;
[[vk::binding(3, PER_PASS)]] StructuredBuffer<uint> _modelRasterQueueAlphaTestTwoSided0;
[[vk::binding(4, PER_PASS)]] StructuredBuffer<ModelVisibilityRecord> _modelVisibilityRecords0;
[[vk::binding(5, PER_PASS)]] StructuredBuffer<uint> _modelVisibilityStats0;
[[vk::binding(6, PER_PASS)]] StructuredBuffer<uint> _modelRasterQueueSolidOneSided1;
[[vk::binding(7, PER_PASS)]] StructuredBuffer<uint> _modelRasterQueueSolidTwoSided1;
[[vk::binding(8, PER_PASS)]] StructuredBuffer<uint> _modelRasterQueueAlphaTestOneSided1;
[[vk::binding(9, PER_PASS)]] StructuredBuffer<uint> _modelRasterQueueAlphaTestTwoSided1;
[[vk::binding(10, PER_PASS)]] StructuredBuffer<ModelVisibilityRecord> _modelVisibilityRecords1;
[[vk::binding(11, PER_PASS)]] StructuredBuffer<uint> _modelVisibilityStats1;
[[vk::binding(12, PER_PASS)]] StructuredBuffer<ModelInstanceRecord> _modelVisibilityInstances;
[[vk::binding(13, PER_PASS)]] StructuredBuffer<ModelRecord> _modelVisibilityModels;
[[vk::binding(14, PER_PASS)]] StructuredBuffer<ModelMesh> _modelVisibilityMeshes;
[[vk::binding(15, PER_PASS)]] StructuredBuffer<ModelLOD> _modelVisibilityLODs;
[[vk::binding(16, PER_PASS)]] StructuredBuffer<ModelSubmesh> _modelVisibilitySubmeshes;
[[vk::binding(17, PER_PASS)]] StructuredBuffer<ModelMeshlet> _modelVisibilityMeshlets;
[[vk::binding(18, PER_PASS)]] StructuredBuffer<uint2> _modelVisibilityPositions;
[[vk::binding(19, PER_PASS)]] StructuredBuffer<uint4> _modelVisibilityVertexAttributes;
[[vk::binding(20, PER_PASS)]] StructuredBuffer<uint> _modelVisibilityVertexIndices;
[[vk::binding(21, PER_PASS)]] StructuredBuffer<uint> _modelVisibilityTriangles;
[[vk::binding(22, PER_PASS)]] StructuredBuffer<uint> _modelVisibilityMaterialTable;
struct MeshVertex
{
    float4 position : SV_Position;
    nointerpolation uint recordID : TEXCOORD0;
    nointerpolation uint materialInstanceIndex : TEXCOORD1;
    float2 uv0 : TEXCOORD2;
};

struct MeshPrimitive
{
    uint triangleID : SV_PrimitiveID;
};

uint LoadRecordID(uint queueIndex)
{
    if (_constants.resourceIndex == 0u)
    {
        if (_constants.queueIndex == 0u) return _modelRasterQueueSolidOneSided0[queueIndex];
        if (_constants.queueIndex == 1u) return _modelRasterQueueSolidTwoSided0[queueIndex];
        if (_constants.queueIndex == 2u) return _modelRasterQueueAlphaTestOneSided0[queueIndex];
        return _modelRasterQueueAlphaTestTwoSided0[queueIndex];
    }
    if (_constants.queueIndex == 0u) return _modelRasterQueueSolidOneSided1[queueIndex];
    if (_constants.queueIndex == 1u) return _modelRasterQueueSolidTwoSided1[queueIndex];
    if (_constants.queueIndex == 2u) return _modelRasterQueueAlphaTestOneSided1[queueIndex];
    return _modelRasterQueueAlphaTestTwoSided1[queueIndex];
}

[shader("mesh")]
[numthreads(64, 1, 1)]
[outputtopology("triangle")]
void main(uint3 groupID : SV_GroupID, uint3 threadID : SV_GroupThreadID,
          out vertices MeshVertex vertices[64], out indices uint3 triangles[124],
          out primitives MeshPrimitive primitives[124])
{
    uint queueIndex = groupID.x + groupID.y * MODEL_MESH_DISPATCH_MAX_GROUPS_X;
    uint countIndex = MODEL_WORK_STAT_COMMITTED_RASTER_MESHLETS_BASE + _constants.queueIndex;
    uint queueCount = _constants.resourceIndex == 0u ? _modelVisibilityStats0[countIndex]
                                                      : _modelVisibilityStats1[countIndex];
    if (queueIndex >= queueCount)
    {
        SetMeshOutputCounts(0, 0);
        return;
    }

    uint recordID = LoadRecordID(queueIndex);
    uint recordCount = _constants.resourceIndex == 0u ? ModelVisibilityRecordCount(_modelVisibilityStats0)
                                                       : ModelVisibilityRecordCount(_modelVisibilityStats1);
    if (recordID >= recordCount)
    {
        SetMeshOutputCounts(0, 0);
        return;
    }
    ModelVisibilityRecord packedRecord = _constants.resourceIndex == 0u ? _modelVisibilityRecords0[recordID]
                                                                         : _modelVisibilityRecords1[recordID];
    DecodedModelVisibilityRecord record = DecodeModelVisibilityRecord(packedRecord);
    ModelInstanceRecord instance = _modelVisibilityInstances[record.instanceIndex];
    ModelRecord model = _modelVisibilityModels[instance.modelIndex];
    ModelMesh mesh = _modelVisibilityMeshes[model.meshBase + record.meshIndex];
    ModelLOD lod = _modelVisibilityLODs[model.meshLODBase + mesh.lodOffset + record.lodIndex];
    ModelSubmesh submesh = _modelVisibilitySubmeshes[model.submeshBase + lod.submeshOffset + record.submeshIndex];
    uint materialInstanceIndex = _modelVisibilityMaterialTable[instance.materialTableOffset +
        mesh.materialSlotOffset + submesh.materialSlotIndex];
    ModelMeshlet meshlet = _modelVisibilityMeshlets[model.meshletBase + record.meshletIndex];
    uint vertexCount = ModelMeshletVertexCount(meshlet);
    uint triangleCount = ModelMeshletTriangleCount(meshlet);
    SetMeshOutputCounts(vertexCount, triangleCount);

    if (threadID.x < vertexCount)
    {
        uint vertexIndex = _modelVisibilityVertexIndices[model.meshletVertexIndexBase + meshlet.vertexOffset + threadID.x];
        float3 localPosition = DecodePosition(
            _modelVisibilityPositions[model.positionBase + lod.vertexOffset + vertexIndex],
            mesh.positionDecodeOffset, mesh.positionDecodeExtent);
        float4 worldPosition = mul(float4(localPosition, 1.0f), instance.currentWorld);
        vertices[threadID.x].position = mul(worldPosition, _cameras[_constants.viewIndex].worldToClip);
        vertices[threadID.x].recordID = recordID;
        vertices[threadID.x].materialInstanceIndex = materialInstanceIndex;
        vertices[threadID.x].uv0 = DecodeHalf2(
            _modelVisibilityVertexAttributes[model.vertexAttributeBase + lod.vertexAttributeOffset + vertexIndex].z);
    }

    for (uint triangleIndex = threadID.x; triangleIndex < triangleCount; triangleIndex += 64u)
    {
        uint packedTriangle = _modelVisibilityTriangles[model.meshletTriangleBase + meshlet.triangleOffset + triangleIndex];
        triangles[triangleIndex] = DecodeModelMeshletTriangle(packedTriangle);
        primitives[triangleIndex].triangleID = triangleIndex;
    }
}
