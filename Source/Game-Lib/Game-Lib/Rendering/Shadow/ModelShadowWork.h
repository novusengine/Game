#pragma once

#include <Base/Types.h>

namespace ShadowRendering
{
    inline constexpr u32 MODEL_SHADOW_FRAME_COUNT = 2;
    inline constexpr u32 MODEL_SHADOW_MAX_CLIPMAPS = 8;
    inline constexpr u32 MODEL_SHADOW_RASTER_CLASS_COUNT = 4;
    inline constexpr u32 MODEL_SHADOW_CASTER_CLASS_COUNT = 2;
    inline constexpr u32 MODEL_SHADOW_QUEUE_COUNT = MODEL_SHADOW_RASTER_CLASS_COUNT * MODEL_SHADOW_CASTER_CLASS_COUNT;
    inline constexpr u32 MODEL_SHADOW_DISPATCH_ARGUMENT_COUNT = 3;
    inline constexpr u32 MODEL_SHADOW_COMPUTE_THREAD_COUNT = 64;

    enum class ModelShadowRasterClass : u32
    {
        SolidOneSided,
        SolidTwoSided,
        AlphaTestOneSided,
        AlphaTestTwoSided
    };

    inline constexpr u32 ModelShadowQueueIndex(ModelShadowRasterClass rasterClass, bool dynamicCaster)
    {
        return static_cast<u32>(rasterClass) + (dynamicCaster ? MODEL_SHADOW_RASTER_CLASS_COUNT : 0u);
    }

    struct ModelShadowChunk
    {
        u32 instanceIndex = 0;
        u32 meshIndex = 0;
        u32 meshletBase = 0;
        u32 packedCountRasterLODAndClipmap = 0;
        u32 submeshIndex = 0;
        u32 dynamicCaster = 0;
        u32 reserved[2] = {};
    };
    static_assert(sizeof(ModelShadowChunk) == 32);

    struct ModelShadowRecord
    {
        u32 packedInstanceAndMesh = 0;
        u32 packedLODSubmeshAndMeshlet = 0;
        u32 clipmapIndex = 0;
        u32 reserved = 0;
    };
    static_assert(sizeof(ModelShadowRecord) == 16);

    struct ModelShadowStats
    {
        u32 testedInstances = 0;
        u32 rejectedInstances = 0;
        u32 expandedChunks = 0;
        u32 testedMeshlets = 0;
        u32 rejectedFrustumMeshlets = 0;
        u32 rejectedPageMeshlets = 0;
        u32 rejectedConeMeshlets = 0;
        u32 rejectedGeometryGroupMeshlets = 0;
        u32 rejectedMaterialMeshlets = 0;
        u32 rejectedTransparentMeshlets = 0;
        u32 queueOverflows = 0;
        u32 recordOverflows = 0;
        u32 chunkQueueOverflows = 0;
        u32 testedTriangles = 0;
        u32 committedTriangles = 0;
        u32 visibilityRecords = 0;
        u32 queueCounts[MODEL_SHADOW_QUEUE_COUNT] = {};
        u32 survivingMeshlets[MODEL_SHADOW_MAX_CLIPMAPS * MODEL_SHADOW_QUEUE_COUNT] = {};
    };
} // namespace ShadowRendering
