#include "DescriptorSet/Global.inc.slang"
#include "Model/ModelVisibility.inc.slang"

struct Constants
{
    uint queueIndex;
    uint viewIndex;
    uint resourceIndex;
};
[[vk::push_constant]] Constants _constants;

[[vk::binding(0, PER_PASS)]] StructuredBuffer<uint> _modelRasterQueueOneSided0;
[[vk::binding(1, PER_PASS)]] StructuredBuffer<uint> _modelRasterQueueTwoSided0;
[[vk::binding(2, PER_PASS)]] StructuredBuffer<ModelVisibilityRecord> _modelVisibilityRecords0;
[[vk::binding(3, PER_PASS)]] StructuredBuffer<uint> _modelVisibilityStats0;
[[vk::binding(4, PER_PASS)]] StructuredBuffer<uint> _modelRasterQueueOneSided1;
[[vk::binding(5, PER_PASS)]] StructuredBuffer<uint> _modelRasterQueueTwoSided1;
[[vk::binding(6, PER_PASS)]] StructuredBuffer<ModelVisibilityRecord> _modelVisibilityRecords1;
[[vk::binding(7, PER_PASS)]] StructuredBuffer<uint> _modelVisibilityStats1;
[[vk::binding(8, PER_PASS)]] StructuredBuffer<ModelInstanceRecord> _modelVisibilityInstances;
[[vk::binding(9, PER_PASS)]] StructuredBuffer<ModelRecord> _modelVisibilityModels;
[[vk::binding(10, PER_PASS)]] StructuredBuffer<ModelMesh> _modelVisibilityMeshes;
[[vk::binding(11, PER_PASS)]] StructuredBuffer<ModelLOD> _modelVisibilityLODs;
[[vk::binding(12, PER_PASS)]] StructuredBuffer<ModelSubmesh> _modelVisibilitySubmeshes;
[[vk::binding(13, PER_PASS)]] StructuredBuffer<ModelMeshlet> _modelVisibilityMeshlets;
[[vk::binding(14, PER_PASS)]] StructuredBuffer<uint2> _modelVisibilityPositions;
[[vk::binding(15, PER_PASS)]] StructuredBuffer<uint4> _modelVisibilityVertexAttributes;
[[vk::binding(16, PER_PASS)]] StructuredBuffer<uint> _modelVisibilityVertexIndices;
[[vk::binding(17, PER_PASS)]] StructuredBuffer<uint> _modelVisibilityTriangles;

struct MeshVertex
{
    float4 position : SV_Position;
    nointerpolation uint recordID : TEXCOORD0;
};

struct MeshPrimitive
{
    uint triangleID : SV_PrimitiveID;
};

uint LoadRecordID(uint queueIndex)
{
    if (_constants.resourceIndex == 0u)
        return _constants.queueIndex == 0u ? _modelRasterQueueOneSided0[queueIndex]
                                           : _modelRasterQueueTwoSided0[queueIndex];
    return _constants.queueIndex == 0u ? _modelRasterQueueOneSided1[queueIndex]
                                       : _modelRasterQueueTwoSided1[queueIndex];
}

[shader("mesh")]
[numthreads(64, 1, 1)]
[outputtopology("triangle")]
void main(uint3 groupID : SV_GroupID, uint3 threadID : SV_GroupThreadID,
          out vertices MeshVertex vertices[64], out indices uint3 triangles[124],
          out primitives MeshPrimitive primitives[124])
{
    uint queueIndex = groupID.x + groupID.y * MODEL_MESH_DISPATCH_MAX_GROUPS_X;
    uint countIndex = _constants.queueIndex == MODEL_RASTER_ONE_SIDED
                          ? MODEL_WORK_STAT_ONE_SIDED_MESHLETS
                          : MODEL_WORK_STAT_TWO_SIDED_MESHLETS;
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
    }

    for (uint triangleIndex = threadID.x; triangleIndex < triangleCount; triangleIndex += 64u)
    {
        uint packedTriangle = _modelVisibilityTriangles[model.meshletTriangleBase + meshlet.triangleOffset + triangleIndex];
        triangles[triangleIndex] = DecodeModelMeshletTriangle(packedTriangle);
        primitives[triangleIndex].triangleID = triangleIndex;
    }
}
