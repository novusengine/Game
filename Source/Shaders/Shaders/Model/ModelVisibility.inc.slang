#ifndef MODEL_VISIBILITY_INCLUDED
#define MODEL_VISIBILITY_INCLUDED

#include "Include/TypedVisibility.inc.slang"
#include "Model/ModelGeometry.inc.slang"
#include "Model/ModelViewWork.inc.slang"

#define MODEL_VISIBILITY_INSTANCE_BITS 22u
#define MODEL_VISIBILITY_MESH_BITS 10u
#define MODEL_VISIBILITY_LOD_BITS 3u
#define MODEL_VISIBILITY_SUBMESH_BITS 10u
#define MODEL_VISIBILITY_MESHLET_BITS 19u

struct ModelVisibilityRecord
{
    uint packedInstanceAndMesh;
    uint packedLODSubmeshAndMeshlet;
};

struct DecodedModelVisibilityRecord
{
    uint instanceIndex;
    uint meshIndex;
    uint lodIndex;
    uint submeshIndex;
    uint meshletIndex;
};

DecodedModelVisibilityRecord DecodeModelVisibilityRecord(ModelVisibilityRecord record)
{
    DecodedModelVisibilityRecord result;
    result.instanceIndex = record.packedInstanceAndMesh & ((1u << MODEL_VISIBILITY_INSTANCE_BITS) - 1u);
    result.meshIndex = record.packedInstanceAndMesh >> MODEL_VISIBILITY_INSTANCE_BITS;
    result.lodIndex = record.packedLODSubmeshAndMeshlet & ((1u << MODEL_VISIBILITY_LOD_BITS) - 1u);
    result.submeshIndex = (record.packedLODSubmeshAndMeshlet >> MODEL_VISIBILITY_LOD_BITS) &
                          ((1u << MODEL_VISIBILITY_SUBMESH_BITS) - 1u);
    result.meshletIndex = record.packedLODSubmeshAndMeshlet >>
                          (MODEL_VISIBILITY_LOD_BITS + MODEL_VISIBILITY_SUBMESH_BITS);
    return result;
}

ModelVisibilityRecord PackModelVisibilityRecord(uint instanceIndex, uint meshIndex, uint lodIndex,
                                                uint submeshIndex, uint meshletIndex)
{
    ModelVisibilityRecord result;
    result.packedInstanceAndMesh = instanceIndex | (meshIndex << MODEL_VISIBILITY_INSTANCE_BITS);
    result.packedLODSubmeshAndMeshlet = lodIndex | (submeshIndex << MODEL_VISIBILITY_LOD_BITS) |
        (meshletIndex << (MODEL_VISIBILITY_LOD_BITS + MODEL_VISIBILITY_SUBMESH_BITS));
    return result;
}

uint ModelVisibilityRecordCount(StructuredBuffer<uint> stats)
{
    return stats[MODEL_WORK_STAT_VISIBILITY_RECORDS];
}

#endif
