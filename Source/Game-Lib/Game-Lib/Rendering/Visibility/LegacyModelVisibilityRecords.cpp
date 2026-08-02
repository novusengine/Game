#include "LegacyModelVisibilityRecords.h"

#include <Base/Util/DebugHandler.h>

#include <Renderer/DescriptorSet.h>
#include <Renderer/Renderer.h>

#include <algorithm>
#include <limits>

namespace Visibility
{
    LegacyModelVisibilityRecords::LegacyModelVisibilityRecords()
    {
        _records.SetDebugName("LegacyModelVisibilityRecords");
        _records.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
        Clear();
    }

    void LegacyModelVisibilityRecords::Clear()
    {
        _records.Clear();
        _records.Add(); // Keep a valid buffer bound even when no legacy models are loaded.
        _overflowReported = false;
    }

    u32 LegacyModelVisibilityRecords::AddInstance(u32 instanceRefID, u32 triangleCount)
    {
        constexpr u32 TRIANGLES_PER_RECORD = MAX_TRIANGLE_ID + 1u;
        constexpr u32 INVALID_RECORD_BASE = std::numeric_limits<u32>().max();

        const u32 recordCount = (triangleCount + TRIANGLES_PER_RECORD - 1u) / TRIANGLES_PER_RECORD;
        const u32 recordBase = static_cast<u32>(_records.Count());
        if (recordBase > MAX_RECORD_ID + 1u ||
            recordCount > MAX_RECORD_ID + 1u - std::min(recordBase, MAX_RECORD_ID + 1u))
        {
            if (!_overflowReported)
            {
                NC_LOG_ERROR("Legacy model visibility record capacity exceeded; opaque legacy geometry will be incomplete");
                _overflowReported = true;
            }
            return INVALID_RECORD_BASE;
        }

        _records.AddCount(recordCount);
        for (u32 recordOffset = 0; recordOffset < recordCount; ++recordOffset)
        {
            LegacyModelRecord& record = _records[recordBase + recordOffset];
            record.instanceRefID = instanceRefID;
            record.triangleBase = recordOffset * TRIANGLES_PER_RECORD;
        }
        return recordBase;
    }

    void LegacyModelVisibilityRecords::Upload(Renderer::Renderer* renderer, Renderer::DescriptorSet& modelSet)
    {
        if (_records.SyncToGPU(renderer))
            modelSet.Bind("_legacyModelVisibilityRecords"_h, _records.GetBuffer());
    }

    void LegacyModelVisibilityRecords::RegisterUsage(Renderer::RenderGraphBuilder& builder,
                                                       Renderer::BufferPassUsage usage) const
    {
        builder.Read(_records.GetBuffer(), usage);
    }
} // namespace Visibility
