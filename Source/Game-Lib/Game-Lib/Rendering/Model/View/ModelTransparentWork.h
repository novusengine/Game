#pragma once

#include <Base/Types.h>
#include <FileFormat/Novus/Model/MaterialABI.h>

namespace ModelView
{
    inline constexpr u32 MODEL_TRANSPARENT_FRAME_COUNT = 2;
    inline constexpr u32 MODEL_TRANSPARENT_GROUP_CLASS_COUNT = 2;
    inline constexpr u32 MODEL_TRANSPARENT_SIDEDNESS_COUNT = 2;
    inline constexpr u32 MODEL_TRANSPARENT_BIN_COUNT = FileFormat::Material::ABI::MAX_PROGRAM_FAMILIES *
        MODEL_TRANSPARENT_GROUP_CLASS_COUNT * MODEL_TRANSPARENT_SIDEDNESS_COUNT;
    inline constexpr u32 MODEL_TRANSPARENT_DISPATCH_ARGUMENT_COUNT = 3;

    constexpr u32 TransparentBinIndex(u16 executionGroupID, bool twoSided)
    {
        const u32 family = FileFormat::Material::ABI::GetProgramFamily(executionGroupID);
        const auto groupClass = FileFormat::Material::ABI::GetExecutionGroupClass(executionGroupID);
        const u32 layered = groupClass == FileFormat::Material::ABI::ExecutionGroup::TransparentLayered ? 1u : 0u;
        return family * 4u + layered * 2u + static_cast<u32>(twoSided);
    }

    constexpr u16 TransparentExecutionGroup(u32 binIndex)
    {
        const u16 family = static_cast<u16>(binIndex / 4u);
        const auto groupClass = ((binIndex / 2u) & 1u) != 0u
            ? FileFormat::Material::ABI::ExecutionGroup::TransparentLayered
            : FileFormat::Material::ABI::ExecutionGroup::TransparentSimple;
        return FileFormat::Material::ABI::MakeExecutionGroup(family, groupClass);
    }

    struct TransparentMeshletChunk
    {
        u32 instanceIndex = 0;
        u32 meshIndex = 0;
        u32 meshletBase = 0;
        u32 packedCountAndLOD = 0;
        u32 submeshIndex = 0;
        u32 binIndex = 0;
        u32 reserved[2] = {};
    };
    static_assert(sizeof(TransparentMeshletChunk) == 32);

    struct TransparentWorkStats
    {
        u32 testedInstances = 0;
        u32 rejectedInstances = 0;
        u32 expandedChunks = 0;
        u32 testedMeshlets = 0;
        u32 rejectedFrustumMeshlets = 0;
        u32 rejectedConeMeshlets = 0;
        u32 rejectedOcclusionMeshlets = 0;
        u32 rejectedGeometryGroupMeshlets = 0;
        u32 chunkQueueOverflows = 0;
        u32 queueOverflows = 0;
        u32 visibilityRecordPackingFailures = 0;
        u32 testedTriangles = 0;
        u32 committedTriangles = 0;
        u32 queueCounts[MODEL_TRANSPARENT_BIN_COUNT] = {};
    };
} // namespace ModelView
