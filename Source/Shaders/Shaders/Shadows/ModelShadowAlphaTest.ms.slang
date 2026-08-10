#include "Shadows/ModelShadowRaster.inc.slang"

[[vk::binding(27, PER_PASS)]] StructuredBuffer<uint4> _shadowRasterVertexAttributes;
[[vk::binding(28, PER_PASS)]] StructuredBuffer<uint> _shadowRasterMaterialTable;

struct MeshVertex
{
    float4 position : SV_Position;
    nointerpolation uint clipmapIndex : TEXCOORD0;
    nointerpolation uint materialInstanceIndex : TEXCOORD1;
    float2 uv0 : TEXCOORD2;
};

[shader("mesh")]
[numthreads(64, 1, 1)]
[outputtopology("triangle")]
void main(uint3 groupID : SV_GroupID, uint3 threadID : SV_GroupThreadID,
          out vertices MeshVertex vertices[64], out indices uint3 triangles[124])
{
    uint queueOffset = groupID.x + groupID.y * MODEL_SHADOW_MESH_DISPATCH_MAX_GROUPS_X;
    ModelShadowRecord shadowRecord = LoadModelShadowRecord(LoadModelShadowRecordIndex(queueOffset));
    ModelVisibilityRecord packed = {shadowRecord.packedInstanceAndMesh, shadowRecord.packedLODSubmeshAndMeshlet};
    DecodedModelVisibilityRecord record = DecodeModelVisibilityRecord(packed);
    ModelInstanceRecord instance = _shadowRasterInstances[record.instanceIndex];
    ModelRecord model = _shadowRasterModels[instance.modelIndex];
    ModelMesh mesh = _shadowRasterMeshes[model.meshBase + record.meshIndex];
    ModelLOD lod = _shadowRasterLODs[model.meshLODBase + mesh.lodOffset + record.lodIndex];
    ModelSubmesh submesh = _shadowRasterSubmeshes[model.submeshBase + lod.submeshOffset + record.submeshIndex];
    uint materialInstanceIndex = _shadowRasterMaterialTable[instance.materialTableOffset +
        mesh.materialSlotOffset + submesh.materialSlotIndex];
    ModelMeshlet meshlet = _shadowRasterMeshlets[model.meshletBase + record.meshletIndex];
    uint vertexCount = ModelMeshletVertexCount(meshlet);
    uint triangleCount = ModelMeshletTriangleCount(meshlet);
    SetMeshOutputCounts(vertexCount, triangleCount);

    if (threadID.x < vertexCount)
    {
        uint vertexIndex = _shadowRasterVertexIndices[model.meshletVertexIndexBase + meshlet.vertexOffset + threadID.x];
        float3 localPosition = DecodePosition(_shadowRasterPositions[model.positionBase + lod.vertexOffset + vertexIndex],
                                              mesh.positionDecodeOffset, mesh.positionDecodeExtent);
        float4 worldPosition = mul(float4(localPosition, 1.0f), instance.currentWorld);
        vertices[threadID.x].position = mul(worldPosition, _cameras[shadowRecord.clipmapIndex + 1u].worldToClip);
        vertices[threadID.x].clipmapIndex = shadowRecord.clipmapIndex;
        vertices[threadID.x].materialInstanceIndex = materialInstanceIndex;
        vertices[threadID.x].uv0 = DecodeHalf2(
            _shadowRasterVertexAttributes[model.vertexAttributeBase + lod.vertexAttributeOffset + vertexIndex].z);
    }
    for (uint triangleIndex = threadID.x; triangleIndex < triangleCount; triangleIndex += 64u)
    {
        uint packedTriangle = _shadowRasterTriangles[model.meshletTriangleBase + meshlet.triangleOffset + triangleIndex];
        triangles[triangleIndex] = DecodeModelMeshletTriangle(packedTriangle);
    }
}
