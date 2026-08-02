#pragma once

#include <Base/Types.h>

namespace ModelView
{
    inline constexpr u32 MODEL_RASTER_CLASS_COUNT = 2;
    inline constexpr u32 MODEL_MESHLET_CHUNK_SIZE = 32;
    inline constexpr u32 INVALID_LOD_HISTORY = 0xFFFFFFFFu;

    struct ViewInstanceInput
    {
        u32 instanceIndex = 0;
        u32 lodHistoryOffset = 0;
    };

    struct MeshletWork
    {
        u32 instanceIndex = 0;
        u32 meshletIndex = 0;
        u32 positionBase = 0;
        u32 vertexAttributeBase = 0;

        u32 meshletVertexIndexBase = 0;
        u32 meshletTriangleBase = 0;
        u32 colorSeed = 0;
        u32 lodIndex = 0;

        vec4 positionDecodeOffset = {};
        vec4 positionDecodeExtent = {};
    };
    static_assert(sizeof(MeshletWork) == 64);

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
        u32 queueOverflows = 0;
        u32 skippedSkinnedLODs = 0;
        u32 lodSelections[8] = {};
    };
} // namespace ModelView
