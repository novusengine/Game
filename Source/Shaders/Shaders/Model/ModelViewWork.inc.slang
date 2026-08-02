#ifndef MODEL_VIEW_WORK_INCLUDED
#define MODEL_VIEW_WORK_INCLUDED

#define MODEL_INSTANCE_VISIBLE (1u << 0u)
#define MODEL_LOD_HAS_SKINNING (1u << 0u)
#define MODEL_LOD_STATIC_FALLBACK_IS_BIND_POSE (1u << 1u)
#define MATERIAL_TWO_SIDED (1u << 0u)
#define INVALID_LOD_HISTORY 0xFFFFFFFFu

#define MODEL_RASTER_ONE_SIDED 0u
#define MODEL_RASTER_TWO_SIDED 1u

#define MODEL_WORK_STAT_SELECTED_INSTANCES 0u
#define MODEL_WORK_STAT_TESTED_INSTANCES 1u
#define MODEL_WORK_STAT_REJECTED_INSTANCES 2u
#define MODEL_WORK_STAT_EXPANDED_CHUNKS 3u
#define MODEL_WORK_STAT_TESTED_CHUNKS 4u
#define MODEL_WORK_STAT_TESTED_MESHLETS 5u
#define MODEL_WORK_STAT_REJECTED_FRUSTUM_MESHLETS 6u
#define MODEL_WORK_STAT_REJECTED_CONE_MESHLETS 7u
#define MODEL_WORK_STAT_REJECTED_GEOMETRY_GROUP_MESHLETS 8u
#define MODEL_WORK_STAT_ONE_SIDED_MESHLETS 9u
#define MODEL_WORK_STAT_TWO_SIDED_MESHLETS 10u
#define MODEL_WORK_STAT_QUEUE_OVERFLOWS 11u
#define MODEL_WORK_STAT_SKIPPED_SKINNED_LODS 12u
#define MODEL_WORK_STAT_LOD_SELECTION_BASE 13u
#define MODEL_WORK_STAT_LOD_SELECTION_COUNT 8u
#define MODEL_WORK_STAT_VISIBILITY_RECORDS 21u
#define MODEL_WORK_STAT_VISIBILITY_RECORD_OVERFLOWS 22u

#define MODEL_CHUNK_MESHLET_COUNT 32u
#define MODEL_CHUNK_MESHLET_COUNT_BITS 6u
#define MODEL_CHUNK_MESHLET_COUNT_MASK ((1u << MODEL_CHUNK_MESHLET_COUNT_BITS) - 1u)
#define MODEL_CHUNK_RASTER_CLASS_SHIFT MODEL_CHUNK_MESHLET_COUNT_BITS
#define MODEL_CHUNK_RASTER_CLASS_MASK 0x1u
#define MODEL_CHUNK_LOD_SHIFT 8u
#define MODEL_CHUNK_LOD_MASK 0x7u
#define MODEL_CHUNK_SUBMESH_SHIFT 11u
#define MODEL_CHUNK_SUBMESH_MASK 0x3FFu

#define MODEL_MESH_DISPATCH_MAX_GROUPS_X 1024u
#define MODEL_MESH_DISPATCH_ARGUMENT_COUNT 3u
#define MODEL_ONE_SIDED_ARGUMENT_OFFSET 0u
#define MODEL_TWO_SIDED_ARGUMENT_OFFSET MODEL_MESH_DISPATCH_ARGUMENT_COUNT
#define MODEL_DISPATCH_ARGUMENT_X 0u
#define MODEL_DISPATCH_ARGUMENT_Y 1u
#define MODEL_DISPATCH_ARGUMENT_Z 2u

struct ViewInstanceInput
{
    uint instanceIndex;
    uint lodHistoryOffset;
};

struct ModelMeshletChunk
{
    uint instanceIndex;
    uint meshIndex;
    uint meshletBase;
    uint packedCountRasterAndLOD;
    uint positionBase;
    uint vertexAttributeBase;
    uint meshletVertexIndexBase;
    uint meshletTriangleBase;
};

struct ModelBounds
{
    float3 center;
    float sphereRadius;
    float3 extents;
    uint reserved;
};

struct ModelRecord
{
    ModelBounds bounds;
    uint2 collisionAssetID;
    uint meshBase;
    uint numMeshes;
    uint meshLODBase;
    uint numMeshLODs;
    uint submeshBase;
    uint numSubmeshes;
    uint meshletBase;
    uint numMeshlets;
    uint positionBase;
    uint numPositions;
    uint vertexAttributeBase;
    uint numVertexAttributes;
    uint skinningDataBase;
    uint numSkinningData;
    uint meshletVertexIndexBase;
    uint numMeshletVertexIndices;
    uint meshletTriangleBase;
    uint numMeshletTriangles;
    uint jointPaletteRemapBase;
    uint numJointPaletteRemaps;
    uint materialSlotBase;
    uint numMaterialSlots;
    uint embeddedInstanceSetBase;
    uint numEmbeddedInstanceSets;
    uint embeddedInstanceBase;
    uint numEmbeddedInstances;
    uint defaultMaterialTableOffset;
    uint defaultMaterialTableCount;
    uint flags;
    uint geometryGroupCount;
};

struct ModelMesh
{
    uint lodOffset;
    uint numLODs;
    uint materialSlotOffset;
    uint numMaterialSlots;
    uint2 skeletonAssetID;
    uint2 animationBoundsAssetID;
    ModelBounds bounds;
    float3 positionDecodeOffset;
    float reserved0;
    float3 positionDecodeExtent;
    uint flags;
};

struct ModelLOD
{
    uint vertexOffset;
    uint numVertices;
    uint vertexAttributeOffset;
    uint numVertexAttributes;
    uint skinningDataOffset;
    uint numSkinningData;
    uint submeshOffset;
    uint numSubmeshes;
    uint meshletOffset;
    uint numMeshlets;
    uint jointPaletteRemapOffset;
    uint numJointPaletteRemaps;
    ModelBounds bounds;
    float geometricError;
    uint flags;
};

struct ModelSubmesh
{
    uint meshletOffset;
    uint numMeshlets;
    uint materialSlotIndex;
    uint geometryGroupID;
    uint semanticPartID;
    uint flags;
};

struct MaterialInstanceRecord { uint parameterOffset; uint materialIndex; };
struct MaterialRecord
{
    uint defaultParameterOffset;
    uint parameterBlockSize;
    uint programID;
    uint flags;
    uint packedClassification;
    uint reserved;
};

float MaxWorldScale(float4x4 transform)
{
    return max(length(transform[0].xyz), max(length(transform[1].xyz), length(transform[2].xyz)));
}

float4 TransformSphere(float3 center, float radius, float4x4 transform)
{
    return float4(mul(float4(center, 1.0f), transform).xyz, radius * MaxWorldScale(transform));
}

bool ModelSphereInsideFrustum(Camera camera, float4 sphere)
{
    if (dot(camera.frustum[0].xyz, sphere.xyz) - camera.frustum[0].w <= -sphere.w) return false;
    if (dot(camera.frustum[1].xyz, sphere.xyz) - camera.frustum[1].w <= -sphere.w) return false;
    if (dot(camera.frustum[2].xyz, sphere.xyz) - camera.frustum[2].w <= -sphere.w) return false;
    if (dot(camera.frustum[3].xyz, sphere.xyz) - camera.frustum[3].w <= -sphere.w) return false;
    if (dot(camera.frustum[4].xyz, sphere.xyz) - camera.frustum[4].w <= -sphere.w) return false;
    if (dot(camera.frustum[5].xyz, sphere.xyz) - camera.frustum[5].w <= -sphere.w) return false;
    return true;
}

int SignExtendConeByte(uint value, uint shift)
{
    return int(value << (24u - shift)) >> 24u;
}

bool ModelConeBackfacing(uint packedCone, float3 center, float radius, float4x4 transform, float3 eye)
{
    float3 localAxis = float3(SignExtendConeByte(packedCone, 0u), SignExtendConeByte(packedCone, 8u),
                              SignExtendConeByte(packedCone, 16u)) / 127.0f;
    float cutoff = float(SignExtendConeByte(packedCone, 24u)) / 127.0f;
    if (cutoff >= 1.0f)
        return false;

    float3 worldCenter = mul(float4(center, 1.0f), transform).xyz;
    float3 worldAxis = normalize(mul(float4(localAxis, 0.0f), transform).xyz);
    float3 view = worldCenter - eye;
    return dot(view, worldAxis) >= cutoff * length(view) + radius * MaxWorldScale(transform);
}

#endif
