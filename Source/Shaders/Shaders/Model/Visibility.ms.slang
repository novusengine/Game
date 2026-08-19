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

uint RasterQueueCapacity()
{
    uint count;
    uint stride;
    if (_constants.resourceIndex == 0u)
    {
        if (_constants.queueIndex == 0u) _modelRasterQueueSolidOneSided0.GetDimensions(count, stride);
        else if (_constants.queueIndex == 1u) _modelRasterQueueSolidTwoSided0.GetDimensions(count, stride);
        else if (_constants.queueIndex == 2u) _modelRasterQueueAlphaTestOneSided0.GetDimensions(count, stride);
        else _modelRasterQueueAlphaTestTwoSided0.GetDimensions(count, stride);
    }
    else
    {
        if (_constants.queueIndex == 0u) _modelRasterQueueSolidOneSided1.GetDimensions(count, stride);
        else if (_constants.queueIndex == 1u) _modelRasterQueueSolidTwoSided1.GetDimensions(count, stride);
        else if (_constants.queueIndex == 2u) _modelRasterQueueAlphaTestOneSided1.GetDimensions(count, stride);
        else _modelRasterQueueAlphaTestTwoSided1.GetDimensions(count, stride);
    }
    return count;
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
    if (queueIndex >= queueCount || queueIndex >= RasterQueueCapacity())
    {
        SetMeshOutputCounts(0, 0);
        return;
    }

    uint recordID = LoadRecordID(queueIndex);
    uint recordCount = _constants.resourceIndex == 0u ? ModelVisibilityRecordCount(_modelVisibilityStats0)
                                                       : ModelVisibilityRecordCount(_modelVisibilityStats1);
    uint boundRecordCount;
    uint boundRecordStride;
    if (_constants.resourceIndex == 0u) _modelVisibilityRecords0.GetDimensions(boundRecordCount, boundRecordStride);
    else _modelVisibilityRecords1.GetDimensions(boundRecordCount, boundRecordStride);
    if (recordID >= recordCount || recordID >= boundRecordCount)
    {
        SetMeshOutputCounts(0, 0);
        return;
    }
    ModelVisibilityRecord packedRecord = _constants.resourceIndex == 0u ? _modelVisibilityRecords0[recordID]
                                                                         : _modelVisibilityRecords1[recordID];
    DecodedModelVisibilityRecord record = DecodeModelVisibilityRecord(packedRecord);
    uint instanceCount, instanceStride;
    uint modelCount, modelStride;
    uint meshCount, meshStride;
    uint lodCount, lodStride;
    uint submeshCount, submeshStride;
    uint meshletCount, meshletStride;
    uint materialTableCount, materialTableStride;
    _modelVisibilityInstances.GetDimensions(instanceCount, instanceStride);
    _modelVisibilityModels.GetDimensions(modelCount, modelStride);
    _modelVisibilityMeshes.GetDimensions(meshCount, meshStride);
    _modelVisibilityLODs.GetDimensions(lodCount, lodStride);
    _modelVisibilitySubmeshes.GetDimensions(submeshCount, submeshStride);
    _modelVisibilityMeshlets.GetDimensions(meshletCount, meshletStride);
    _modelVisibilityMaterialTable.GetDimensions(materialTableCount, materialTableStride);
    if (record.instanceIndex >= instanceCount)
    {
        SetMeshOutputCounts(0, 0);
        return;
    }
    ModelInstanceRecord instance = _modelVisibilityInstances[record.instanceIndex];
    if (instance.modelIndex >= modelCount)
    {
        SetMeshOutputCounts(0, 0);
        return;
    }
    ModelRecord model = _modelVisibilityModels[instance.modelIndex];
    uint meshIndex = model.meshBase + record.meshIndex;
    if (meshIndex < model.meshBase || meshIndex >= meshCount)
    {
        SetMeshOutputCounts(0, 0);
        return;
    }
    ModelMesh mesh = _modelVisibilityMeshes[meshIndex];
    uint lodIndex = model.meshLODBase + mesh.lodOffset + record.lodIndex;
    if (lodIndex < model.meshLODBase || lodIndex >= lodCount)
    {
        SetMeshOutputCounts(0, 0);
        return;
    }
    ModelLOD lod = _modelVisibilityLODs[lodIndex];
    uint submeshIndex = model.submeshBase + lod.submeshOffset + record.submeshIndex;
    if (submeshIndex < model.submeshBase || submeshIndex >= submeshCount)
    {
        SetMeshOutputCounts(0, 0);
        return;
    }
    ModelSubmesh submesh = _modelVisibilitySubmeshes[submeshIndex];
    uint materialTableIndex = instance.materialTableOffset + mesh.materialSlotOffset + submesh.materialSlotIndex;
    uint meshletIndex = model.meshletBase + record.meshletIndex;
    if (materialTableIndex < instance.materialTableOffset || materialTableIndex >= materialTableCount ||
        meshletIndex < model.meshletBase || meshletIndex >= meshletCount)
    {
        SetMeshOutputCounts(0, 0);
        return;
    }
    uint materialInstanceIndex = _modelVisibilityMaterialTable[materialTableIndex];
    ModelMeshlet meshlet = _modelVisibilityMeshlets[meshletIndex];
    uint vertexCount = ModelMeshletVertexCount(meshlet);
    uint triangleCount = ModelMeshletTriangleCount(meshlet);
    uint vertexIndexCount, vertexIndexStride;
    uint positionCount, positionStride;
    uint attributeCount, attributeStride;
    uint triangleBufferCount, triangleStride;
    _modelVisibilityVertexIndices.GetDimensions(vertexIndexCount, vertexIndexStride);
    _modelVisibilityPositions.GetDimensions(positionCount, positionStride);
    _modelVisibilityVertexAttributes.GetDimensions(attributeCount, attributeStride);
    _modelVisibilityTriangles.GetDimensions(triangleBufferCount, triangleStride);
    uint vertexBase = model.meshletVertexIndexBase + meshlet.vertexOffset;
    uint triangleBase = model.meshletTriangleBase + meshlet.triangleOffset;
    if (vertexCount > 64u || triangleCount > 124u || vertexBase < model.meshletVertexIndexBase ||
        vertexBase + vertexCount < vertexBase || vertexBase + vertexCount > vertexIndexCount ||
        triangleBase < model.meshletTriangleBase || triangleBase + triangleCount < triangleBase ||
        triangleBase + triangleCount > triangleBufferCount)
    {
        SetMeshOutputCounts(0, 0);
        return;
    }
    SetMeshOutputCounts(vertexCount, triangleCount);

    if (threadID.x < vertexCount)
    {
        uint vertexIndex = _modelVisibilityVertexIndices[vertexBase + threadID.x];
        uint positionIndex = model.positionBase + lod.vertexOffset + vertexIndex;
        uint attributeIndex = model.vertexAttributeBase + lod.vertexAttributeOffset + vertexIndex;
        bool validVertex = positionIndex >= model.positionBase && positionIndex < positionCount &&
                           attributeIndex >= model.vertexAttributeBase && attributeIndex < attributeCount;
        float3 localPosition = validVertex ? DecodePosition(_modelVisibilityPositions[positionIndex], mesh.positionDecodeOffset, mesh.positionDecodeExtent) : float3(0.0f, 0.0f, 0.0f);
        float4 worldPosition = mul(float4(localPosition, 1.0f), instance.currentWorld);
        vertices[threadID.x].position = mul(worldPosition, _cameras[_constants.viewIndex].worldToClip);
        vertices[threadID.x].recordID = recordID;
        vertices[threadID.x].materialInstanceIndex = materialInstanceIndex;
        vertices[threadID.x].uv0 = validVertex ? DecodeHalf2(_modelVisibilityVertexAttributes[attributeIndex].z) :
                                                 float2(0.0f, 0.0f);
    }

    for (uint triangleIndex = threadID.x; triangleIndex < triangleCount; triangleIndex += 64u)
    {
        uint packedTriangle = _modelVisibilityTriangles[triangleBase + triangleIndex];
        triangles[triangleIndex] = DecodeModelMeshletTriangle(packedTriangle);
        primitives[triangleIndex].triangleID = triangleIndex;
    }
}
