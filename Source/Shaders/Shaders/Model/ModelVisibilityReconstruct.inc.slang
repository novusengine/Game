#ifndef MODEL_VISIBILITY_RECONSTRUCT_INCLUDED
#define MODEL_VISIBILITY_RECONSTRUCT_INCLUDED

#include "Model/ModelVisibility.inc.slang"
#ifndef MODEL_VISIBILITY_NO_MATERIAL
#include "DescriptorSet/Material.inc.slang"
#endif

[[vk::binding(4, PER_PASS)]] StructuredBuffer<ModelVisibilityRecord> _resolvedModelVisibilityRecords0;
[[vk::binding(5, PER_PASS)]] StructuredBuffer<uint> _resolvedModelVisibilityStats0;
[[vk::binding(6, PER_PASS)]] StructuredBuffer<ModelVisibilityRecord> _resolvedModelVisibilityRecords1;
[[vk::binding(7, PER_PASS)]] StructuredBuffer<uint> _resolvedModelVisibilityStats1;
[[vk::binding(8, PER_PASS)]] StructuredBuffer<ModelInstanceRecord> _resolvedModelInstances;
[[vk::binding(9, PER_PASS)]] StructuredBuffer<ModelRecord> _resolvedModels;
[[vk::binding(10, PER_PASS)]] StructuredBuffer<ModelMesh> _resolvedModelMeshes;
[[vk::binding(11, PER_PASS)]] StructuredBuffer<ModelLOD> _resolvedModelLODs;
[[vk::binding(12, PER_PASS)]] StructuredBuffer<ModelSubmesh> _resolvedModelSubmeshes;
[[vk::binding(13, PER_PASS)]] StructuredBuffer<ModelMeshlet> _resolvedModelMeshlets;
[[vk::binding(14, PER_PASS)]] StructuredBuffer<uint2> _resolvedModelPositions;
[[vk::binding(15, PER_PASS)]] StructuredBuffer<uint4> _resolvedModelVertexAttributes;
[[vk::binding(16, PER_PASS)]] StructuredBuffer<uint> _resolvedModelVertexIndices;
[[vk::binding(17, PER_PASS)]] StructuredBuffer<uint> _resolvedModelTriangles;
#ifndef MODEL_VISIBILITY_NO_MATERIAL
[[vk::binding(18, PER_PASS)]] StructuredBuffer<uint> _resolvedModelMaterialTable;
#endif

struct ReconstructedModelPixel
{
    DecodedModelVisibilityRecord record;
    ModelInstanceRecord instance;
    ModelRecord model;
    ModelMesh mesh;
    ModelLOD lod;
    ModelSubmesh submesh;
    ModelMeshlet meshlet;
    uint materialInstanceIndex;
    uint materialIndex;
    uint triangleID;
    float3 worldPosition;
#ifdef MODEL_VISIBILITY_RECONSTRUCT_PREVIOUS_POSITION
    float3 previousWorldPosition;
#endif
    float3 worldNormal;
    float3 faceNormal;
    float2 uv0;
    float2 uv0DDX;
    float2 uv0DDY;
    bool frontFacing;
};

bool LoadResolvedModelRecord(uint recordID, uint resourceIndex, out ModelVisibilityRecord record)
{
    uint count = resourceIndex == 0u ? ModelVisibilityRecordCount(_resolvedModelVisibilityStats0)
                                     : ModelVisibilityRecordCount(_resolvedModelVisibilityStats1);
    if (recordID >= count || recordID > TYPED_VISIBILITY_MAX_RECORD_ID)
        return false;
    record = resourceIndex == 0u ? _resolvedModelVisibilityRecords0[recordID]
                                 : _resolvedModelVisibilityRecords1[recordID];
    return true;
}

bool ReconstructModelPixel(TypedVisibilityPayload payload, float2 samplePosition, float2 renderSize,
                           uint cameraIndex, uint resourceIndex, out ReconstructedModelPixel result)
{
    ModelVisibilityRecord packedRecord;
    if (payload.geometryTypeID != TYPED_VISIBILITY_MODEL || payload.triangleID > TYPED_VISIBILITY_MAX_TRIANGLE_ID ||
        !LoadResolvedModelRecord(payload.recordID, resourceIndex, packedRecord))
        return false;

    result.record = DecodeModelVisibilityRecord(packedRecord);
    result.instance = _resolvedModelInstances[result.record.instanceIndex];
    result.model = _resolvedModels[result.instance.modelIndex];
    if (result.record.meshIndex >= result.model.numMeshes)
        return false;
    result.mesh = _resolvedModelMeshes[result.model.meshBase + result.record.meshIndex];
    if (result.record.lodIndex >= result.mesh.numLODs)
        return false;
    result.lod = _resolvedModelLODs[result.model.meshLODBase + result.mesh.lodOffset + result.record.lodIndex];
    if (result.record.submeshIndex >= result.lod.numSubmeshes || result.record.meshletIndex >= result.model.numMeshlets)
        return false;
    result.submesh = _resolvedModelSubmeshes[result.model.submeshBase + result.lod.submeshOffset + result.record.submeshIndex];
    result.meshlet = _resolvedModelMeshlets[result.model.meshletBase + result.record.meshletIndex];
    uint triangleCount = ModelMeshletTriangleCount(result.meshlet);
    if (payload.triangleID >= triangleCount)
        return false;

    uint packedTriangle = _resolvedModelTriangles[result.model.meshletTriangleBase + result.meshlet.triangleOffset + payload.triangleID];
    uint3 localVertexIndices = DecodeModelMeshletTriangle(packedTriangle);
    float3 worldPositions[3];
#ifdef MODEL_VISIBILITY_RECONSTRUCT_PREVIOUS_POSITION
    float3 previousWorldPositions[3];
#endif
    float3 worldNormals[3];
    float2 uvs[3];
    for (uint i = 0u; i < 3u; ++i)
    {
        uint vertexIndex = _resolvedModelVertexIndices[result.model.meshletVertexIndexBase + result.meshlet.vertexOffset + localVertexIndices[i]];
        float3 localPosition = DecodePosition(
            _resolvedModelPositions[result.model.positionBase + result.lod.vertexOffset + vertexIndex],
            result.mesh.positionDecodeOffset, result.mesh.positionDecodeExtent);
        uint4 attributes = _resolvedModelVertexAttributes[result.model.vertexAttributeBase + result.lod.vertexAttributeOffset + vertexIndex];
        worldPositions[i] = mul(float4(localPosition, 1.0f), result.instance.currentWorld).xyz;
#ifdef MODEL_VISIBILITY_RECONSTRUCT_PREVIOUS_POSITION
        previousWorldPositions[i] = mul(float4(localPosition, 1.0f), result.instance.previousWorld).xyz;
#endif
        worldNormals[i] = normalize(mul(float4(DecodeOctahedralSnorm(attributes.x), 0.0f), result.instance.currentWorld).xyz);
        uvs[i] = DecodeHalf2(attributes.z);
    }

    Barycentrics bary = ReconstructBarycentricsAtSample(samplePosition, renderSize, worldPositions[0],
                                                         worldPositions[1], worldPositions[2], cameraIndex);
    result.worldPosition = InterpolateVertexAttribute(bary, worldPositions[0], worldPositions[1], worldPositions[2]);
#ifdef MODEL_VISIBILITY_RECONSTRUCT_PREVIOUS_POSITION
    result.previousWorldPosition = InterpolateVertexAttribute(bary, previousWorldPositions[0], previousWorldPositions[1], previousWorldPositions[2]);
#endif
    result.worldNormal = normalize(InterpolateVertexAttribute(bary, worldNormals[0], worldNormals[1], worldNormals[2]));
    result.faceNormal = normalize(cross(worldPositions[1] - worldPositions[0], worldPositions[2] - worldPositions[0]));
    FullBary2 fullUV = CalcFullBary2(bary, uvs[0], uvs[1], uvs[2]);
    result.uv0 = fullUV.value;
    result.uv0DDX = fullUV.ddx;
    result.uv0DDY = fullUV.ddy;
    float2 screenPositions[3];
    for (uint screenVertex = 0u; screenVertex < 3u; ++screenVertex)
    {
        float4 clip = mul(float4(worldPositions[screenVertex], 1.0f), _cameras[cameraIndex].worldToClip);
        screenPositions[screenVertex] = clip.xy / clip.w;
    }
    float2 edge0 = screenPositions[1] - screenPositions[0];
    float2 edge1 = screenPositions[2] - screenPositions[0];
    result.frontFacing = edge0.x * edge1.y - edge0.y * edge1.x >= 0.0f;
#ifndef MODEL_VISIBILITY_NO_MATERIAL
    result.materialInstanceIndex = _resolvedModelMaterialTable[result.instance.materialTableOffset +
        result.mesh.materialSlotOffset + result.submesh.materialSlotIndex];
    result.materialIndex = _materialInstances[result.materialInstanceIndex].materialIndex;
#else
    result.materialInstanceIndex = 0u;
    result.materialIndex = 0u;
#endif
    result.triangleID = payload.triangleID;
    return true;
}

#endif
