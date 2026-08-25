#ifndef MODEL_VISIBILITY_RECORD_INCLUDED
#define MODEL_VISIBILITY_RECORD_INCLUDED

#define MODEL_VISIBILITY_INSTANCE_BITS 22u
#define MODEL_VISIBILITY_MESH_BITS 10u
#define MODEL_VISIBILITY_LOD_BITS 3u
#define MODEL_VISIBILITY_SUBMESH_BITS 12u
#define MODEL_VISIBILITY_MESHLET_BITS 17u

struct ModelVisibilityRecord
{
    uint packedInstanceAndMesh;
    uint packedLODSubmeshAndMeshlet;
    uint replayRevision;
};

struct DecodedModelVisibilityRecord
{
    uint instanceIndex;
    uint meshIndex;
    uint lodIndex;
    uint submeshIndex;
    uint meshletIndex;
};

uint PackModelInstanceAndMesh(uint instanceIndex, uint meshIndex)
{
    return instanceIndex | (meshIndex << MODEL_VISIBILITY_INSTANCE_BITS);
}

uint ModelVisibilityInstanceIndex(uint packedInstanceAndMesh)
{
    return packedInstanceAndMesh & ((1u << MODEL_VISIBILITY_INSTANCE_BITS) - 1u);
}

uint ModelVisibilityMeshIndex(uint packedInstanceAndMesh)
{
    return packedInstanceAndMesh >> MODEL_VISIBILITY_INSTANCE_BITS;
}

DecodedModelVisibilityRecord DecodeModelVisibilityRecord(ModelVisibilityRecord record)
{
    DecodedModelVisibilityRecord result;
    result.instanceIndex = ModelVisibilityInstanceIndex(record.packedInstanceAndMesh);
    result.meshIndex = ModelVisibilityMeshIndex(record.packedInstanceAndMesh);
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
    result.packedInstanceAndMesh = PackModelInstanceAndMesh(instanceIndex, meshIndex);
    result.packedLODSubmeshAndMeshlet = lodIndex | (submeshIndex << MODEL_VISIBILITY_LOD_BITS) |
        (meshletIndex << (MODEL_VISIBILITY_LOD_BITS + MODEL_VISIBILITY_SUBMESH_BITS));
    result.replayRevision = 0u;
    return result;
}

#endif
