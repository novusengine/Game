#pragma once

#include <Base/Types.h>

namespace ModelView
{
    inline constexpr u32 MODEL_VIEW_FRAME_COUNT = 2;
    inline constexpr u32 MODEL_RASTER_SOLID_ONE_SIDED = 0;
    inline constexpr u32 MODEL_RASTER_SOLID_TWO_SIDED = 1;
    inline constexpr u32 MODEL_RASTER_ALPHA_TEST_ONE_SIDED = 2;
    inline constexpr u32 MODEL_RASTER_ALPHA_TEST_TWO_SIDED = 3;
    inline constexpr u32 MODEL_RASTER_CLASS_COUNT = 4;
    inline constexpr u32 MODEL_DISPATCH_ARGUMENT_COUNT = 3;
    inline constexpr u32 MODEL_MESHLET_CHUNK_SIZE = 32;
    inline constexpr u32 MODEL_LOD_SELECTION_COUNT = 8;
    inline constexpr u32 INVALID_LOD_HISTORY = 0xFFFFFFFFu;

    inline constexpr u32 MODEL_VISIBILITY_INSTANCE_BITS = 22;
    inline constexpr u32 MODEL_VISIBILITY_MESH_BITS = 10;
    inline constexpr u32 MODEL_VISIBILITY_LOD_BITS = 3;
    inline constexpr u32 MODEL_VISIBILITY_SUBMESH_BITS = 10;
    inline constexpr u32 MODEL_VISIBILITY_MESHLET_BITS = 19;

    struct VisibilityRecord
    {
        u32 packedInstanceAndMesh = 0;
        u32 packedLODSubmeshAndMeshlet = 0;
    };
    static_assert(sizeof(VisibilityRecord) == 8);

    struct DecodedVisibilityRecord
    {
        u32 instanceIndex = 0;
        u32 meshIndex = 0;
        u32 lodIndex = 0;
        u32 submeshIndex = 0;
        u32 meshletIndex = 0;
    };

    inline bool PackVisibilityRecord(u32 instanceIndex, u32 meshIndex, u32 lodIndex, u32 submeshIndex,
                                     u32 meshletIndex, VisibilityRecord& outRecord)
    {
        if (instanceIndex >= (1u << MODEL_VISIBILITY_INSTANCE_BITS) ||
            meshIndex >= (1u << MODEL_VISIBILITY_MESH_BITS) ||
            lodIndex >= (1u << MODEL_VISIBILITY_LOD_BITS) ||
            submeshIndex >= (1u << MODEL_VISIBILITY_SUBMESH_BITS) ||
            meshletIndex >= (1u << MODEL_VISIBILITY_MESHLET_BITS))
            return false;

        outRecord.packedInstanceAndMesh = instanceIndex | (meshIndex << MODEL_VISIBILITY_INSTANCE_BITS);
        outRecord.packedLODSubmeshAndMeshlet = lodIndex |
            (submeshIndex << MODEL_VISIBILITY_LOD_BITS) |
            (meshletIndex << (MODEL_VISIBILITY_LOD_BITS + MODEL_VISIBILITY_SUBMESH_BITS));
        return true;
    }

    inline DecodedVisibilityRecord UnpackVisibilityRecord(const VisibilityRecord& record)
    {
        DecodedVisibilityRecord result;
        result.instanceIndex = record.packedInstanceAndMesh & ((1u << MODEL_VISIBILITY_INSTANCE_BITS) - 1u);
        result.meshIndex = record.packedInstanceAndMesh >> MODEL_VISIBILITY_INSTANCE_BITS;
        result.lodIndex = record.packedLODSubmeshAndMeshlet & ((1u << MODEL_VISIBILITY_LOD_BITS) - 1u);
        result.submeshIndex = (record.packedLODSubmeshAndMeshlet >> MODEL_VISIBILITY_LOD_BITS) &
                              ((1u << MODEL_VISIBILITY_SUBMESH_BITS) - 1u);
        result.meshletIndex = record.packedLODSubmeshAndMeshlet >>
                              (MODEL_VISIBILITY_LOD_BITS + MODEL_VISIBILITY_SUBMESH_BITS);
        return result;
    }

    struct ViewInstanceInput
    {
        u32 instanceIndex = 0;
        u32 lodHistoryOffset = 0;
    };

    struct MeshletChunk
    {
        u32 instanceIndex = 0;
        u32 meshIndex = 0;
        u32 meshletBase = 0;
        u32 packedCountRasterAndLOD = 0;
        u32 positionBase = 0;
        u32 vertexAttributeBase = 0;
        u32 meshletVertexIndexBase = 0;
        u32 meshletTriangleBase = 0;
    };
    static_assert(sizeof(MeshletChunk) == 32);

    struct WorkStats
    {
        u32 selectedInstances = 0;
        u32 testedInstances = 0;
        u32 rejectedInstances = 0;
        u32 expandedChunks = 0;
        u32 testedChunks = 0;
        u32 testedMeshlets = 0;
        u32 rejectedFrustumMeshlets = 0;
        u32 rejectedConeMeshlets = 0;
        u32 rejectedGeometryGroupMeshlets = 0;
        u32 oneSidedMeshlets = 0;
        u32 twoSidedMeshlets = 0;
        u32 alphaTestOneSidedMeshlets = 0;
        u32 alphaTestTwoSidedMeshlets = 0;
        u32 queueOverflows = 0;
        u32 skippedSkinnedLODs = 0;
        u32 lodSelections[MODEL_LOD_SELECTION_COUNT] = {};
        u32 visibilityRecords = 0;
        u32 visibilityRecordOverflows = 0;
    };
} // namespace ModelView
