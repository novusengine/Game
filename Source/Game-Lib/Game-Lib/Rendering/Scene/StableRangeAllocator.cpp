#include "StableRangeAllocator.h"

#include <tracy/Tracy.hpp>

#include <algorithm>

namespace RenderScenes
{
    StableRange StableRangeAllocator::Allocate(u32 count)
    {
        if (count == 0)
            return {};

        FlushFrees();

        for (size_t index = 0; index < _freeRanges.size(); ++index)
        {
            StableRange& freeRange = _freeRanges[index];
            if (freeRange.count < count)
                continue;

            StableRange allocation = { freeRange.offset, count };
            freeRange.offset += count;
            freeRange.count -= count;
            if (freeRange.count == 0)
                _freeRanges.erase(_freeRanges.begin() + index);
            return allocation;
        }

        StableRange allocation = { _addressSpaceSize, count };
        _addressSpaceSize += count;
        return allocation;
    }

    void StableRangeAllocator::Free(StableRange range)
    {
        ZoneScopedN("StableRangeAllocator::Free");

        if (!range)
            return;

        _freeRanges.push_back(range);
        _freesDirty = true;
    }

    void StableRangeAllocator::Free(std::span<const StableRange> ranges)
    {
        ZoneScopedN("StableRangeAllocator::FreeBatch");

        for (const StableRange range : ranges)
        {
            if (range)
                _freeRanges.push_back(range);
        }
        _freesDirty |= !ranges.empty();
    }

    void StableRangeAllocator::FlushFrees()
    {
        if (!_freesDirty)
            return;

        Coalesce();
        TrimTail();
        _freesDirty = false;
    }

    u32 StableRangeAllocator::GetFreeCount() const
    {
        u32 count = 0;
        for (const StableRange& range : _freeRanges)
            count += range.count;
        return count;
    }

    void StableRangeAllocator::Coalesce()
    {
        ZoneScopedN("StableRangeAllocator::Coalesce");

        std::sort(_freeRanges.begin(), _freeRanges.end(), [](const StableRange& left, const StableRange& right) {
            return left.offset < right.offset;
        });

        size_t writeIndex = 0;
        for (const StableRange& range : _freeRanges)
        {
            if (writeIndex != 0)
            {
                StableRange& previous = _freeRanges[writeIndex - 1];
                if (previous.offset + previous.count == range.offset)
                {
                    previous.count += range.count;
                    continue;
                }
            }

            _freeRanges[writeIndex++] = range;
        }
        _freeRanges.resize(writeIndex);
    }

    void StableRangeAllocator::TrimTail()
    {
        while (!_freeRanges.empty())
        {
            const StableRange& range = _freeRanges.back();
            if (range.offset + range.count != _addressSpaceSize)
                break;

            _addressSpaceSize = range.offset;
            _freeRanges.pop_back();
        }
    }
} // namespace RenderScenes
