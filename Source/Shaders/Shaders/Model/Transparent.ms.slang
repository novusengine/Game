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
    uint recordCount, recordStride;
    if (_constants.resourceIndex == 0u) _transparentRecords0.GetDimensions(recordCount, recordStride);
    else _transparentRecords1.GetDimensions(recordCount, recordStride);
    if (recordIndex < queueIndex || recordIndex >= recordCount)
    {
        SetMeshOutputCounts(0, 0);
        return;
    }
    ModelVisibilityRecord packedRecord = _constants.resourceIndex == 0u ? _transparentRecords0[recordIndex]
                                                                         : _transparentRecords1[recordIndex];
    DecodedModelVisibilityRecord record = DecodeModelVisibilityRecord(packedRecord);
    uint instanceCount, instanceStride;
    uint modelCount, modelStride;
    uint meshCount, meshStride;
    uint lodCount, lodStride;
    uint submeshCount, submeshStride;
    uint meshletCount, meshletStride;
    uint materialCount, materialStride;
    _transparentRasterInstances.GetDimensions(instanceCount, instanceStride);
    _transparentRasterModels.GetDimensions(modelCount, modelStride);
    _transparentRasterMeshes.GetDimensions(meshCount, meshStride);
    _transparentRasterLODs.GetDimensions(lodCount, lodStride);
    _transparentRasterSubmeshes.GetDimensions(submeshCount, submeshStride);
    _transparentRasterMeshlets.GetDimensions(meshletCount, meshletStride);
    _transparentRasterMaterialTable.GetDimensions(materialCount, materialStride);
    if (record.instanceIndex >= instanceCount)
    {
        SetMeshOutputCounts(0, 0);
        return;
    }
    ModelInstanceRecord instance = _transparentRasterInstances[record.instanceIndex];
    if (instance.modelIndex >= modelCount)
    {
        SetMeshOutputCounts(0, 0);
        return;
    }
    ModelRecord model = _transparentRasterModels[instance.modelIndex];
    uint meshIndex = model.meshBase + record.meshIndex;
    if (meshIndex < model.meshBase || meshIndex >= meshCount)
    {
        SetMeshOutputCounts(0, 0);
        return;
    }
    ModelMesh mesh = _transparentRasterMeshes[meshIndex];
    uint lodIndex = model.meshLODBase + mesh.lodOffset + record.lodIndex;
    if (lodIndex < model.meshLODBase || lodIndex >= lodCount)
    {
        SetMeshOutputCounts(0, 0);
        return;
    }
    ModelLOD lod = _transparentRasterLODs[lodIndex];
    uint submeshIndex = model.submeshBase + lod.submeshOffset + record.submeshIndex;
    uint meshletIndex = model.meshletBase + record.meshletIndex;
    if (submeshIndex < model.submeshBase || submeshIndex >= submeshCount ||
        meshletIndex < model.meshletBase || meshletIndex >= meshletCount)
    {
        SetMeshOutputCounts(0, 0);
        return;
    }
    ModelSubmesh submesh = _transparentRasterSubmeshes[submeshIndex];
    ModelMeshlet meshlet = _transparentRasterMeshlets[meshletIndex];
    uint materialIndex = instance.materialTableOffset + mesh.materialSlotOffset + submesh.materialSlotIndex;
    if (materialIndex < instance.materialTableOffset || materialIndex >= materialCount)
    {
        SetMeshOutputCounts(0, 0);
        return;
    }
    uint materialInstanceIndex = _transparentRasterMaterialTable[materialIndex];
    uint vertexCount = ModelMeshletVertexCount(meshlet);
    uint triangleCount = ModelMeshletTriangleCount(meshlet);
    uint vertexIndexCount, vertexIndexStride;
    uint positionCount, positionStride;
    uint attributeCount, attributeStride;
    uint triangleBufferCount, triangleStride;
    _transparentRasterVertexIndices.GetDimensions(vertexIndexCount, vertexIndexStride);
    _transparentRasterPositions.GetDimensions(positionCount, positionStride);
    _transparentRasterVertexAttributes.GetDimensions(attributeCount, attributeStride);
    _transparentRasterTriangles.GetDimensions(triangleBufferCount, triangleStride);
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
        uint vertexIndex = _transparentRasterVertexIndices[vertexBase + threadID.x];
        uint positionIndex = model.positionBase + lod.vertexOffset + vertexIndex;
        uint attributeIndex = model.vertexAttributeBase + lod.vertexAttributeOffset + vertexIndex;
        bool validVertex = positionIndex >= model.positionBase && positionIndex < positionCount &&
                           attributeIndex >= model.vertexAttributeBase && attributeIndex < attributeCount;
        float3 localPosition = validVertex ? DecodePosition(_transparentRasterPositions[positionIndex], mesh.positionDecodeOffset, mesh.positionDecodeExtent) : float3(0.0f, 0.0f, 0.0f);
        uint4 attributes = validVertex ? _transparentRasterVertexAttributes[attributeIndex] : uint4(0u);
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
        uint packedTriangle = _transparentRasterTriangles[triangleBase + triangleIndex];
        triangles[triangleIndex] = DecodeModelMeshletTriangle(packedTriangle);
    }
}
