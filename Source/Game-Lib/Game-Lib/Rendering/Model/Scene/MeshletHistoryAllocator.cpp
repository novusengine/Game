#include "MeshletHistoryAllocator.h"

#include <algorithm>

#include <tracy/Tracy.hpp>

namespace ModelScene
{
    void MeshletHistoryAllocator::Reserve(u32 rangeCount)
    {
        _pendingClears.reserve(_pendingClears.size() + rangeCount);
        _retiredRanges.reserve(_retiredRanges.size() + rangeCount);
    }

    MeshletHistoryRange MeshletHistoryAllocator::Allocate(u32 wordCount)
    {
        ZoneScopedN("MeshletHistoryAllocator::Allocate");

        if (wordCount == 0)
            return {};

        for (size_t index = 0; index < _freeRanges.size(); ++index)
        {
            MeshletHistoryRange& freeRange = _freeRanges[index];
            if (freeRange.wordCount < wordCount)
                continue;

            MeshletHistoryRange allocation = { freeRange.wordOffset, wordCount };
            freeRange.wordOffset += wordCount;
            freeRange.wordCount -= wordCount;
            if (freeRange.wordCount == 0)
                _freeRanges.erase(_freeRanges.begin() + index);

            _liveWords += wordCount;
            RequestClear(allocation);
            return allocation;
        }

        MeshletHistoryRange allocation = { _addressSpaceWords, wordCount };
        _addressSpaceWords += wordCount;
        _highWaterWords = std::max(_highWaterWords, _addressSpaceWords);
        _liveWords += wordCount;
        RequestClear(allocation);
        return allocation;
    }

    void MeshletHistoryAllocator::Retire(MeshletHistoryRange range, u64 retireValue)
    {
        if (!range)
            return;

        _liveWords -= range.wordCount;
        _retiredWords += range.wordCount;
        _retiredRanges.push_back({ range, retireValue });
    }

    void MeshletHistoryAllocator::ReleaseRetired(u64 completedValue)
    {
        size_t writeIndex = 0;
        for (const RetiredRange& retired : _retiredRanges)
        {
            if (retired.retireValue > completedValue)
            {
                _retiredRanges[writeIndex++] = retired;
                continue;
            }

            _retiredWords -= retired.range.wordCount;
            AddFreeRange(retired.range);
        }
        _retiredRanges.resize(writeIndex);

        CoalesceFreeRanges();
        TrimFreeTail();
    }

    void MeshletHistoryAllocator::RequestClear(MeshletHistoryRange range)
    {
        if (range)
            _pendingClears.push_back(range);
    }

    MeshletHistoryAllocatorStats MeshletHistoryAllocator::GetStats() const
    {
        MeshletHistoryAllocatorStats stats;
        stats.liveWords = _liveWords;
        stats.retiredWords = _retiredWords;
        stats.addressSpaceWords = _addressSpaceWords;
        stats.highWaterWords = _highWaterWords;
        stats.pendingClearRanges = static_cast<u32>(_pendingClears.size());
        stats.freeRanges = static_cast<u32>(_freeRanges.size());
        for (const MeshletHistoryRange& range : _freeRanges)
            stats.freeWords += range.wordCount;
        return stats;
    }

    void MeshletHistoryAllocator::AddFreeRange(MeshletHistoryRange range)
    {
        if (range)
            _freeRanges.push_back(range);
    }

    void MeshletHistoryAllocator::CoalesceFreeRanges()
    {
        std::sort(_freeRanges.begin(), _freeRanges.end(), [](const MeshletHistoryRange& left, const MeshletHistoryRange& right) {
            return left.wordOffset < right.wordOffset;
        });

        size_t writeIndex = 0;
        for (const MeshletHistoryRange& range : _freeRanges)
        {
            if (writeIndex != 0)
            {
                MeshletHistoryRange& previous = _freeRanges[writeIndex - 1];
                if (previous.wordOffset + previous.wordCount == range.wordOffset)
                {
                    previous.wordCount += range.wordCount;
                    continue;
                }
            }

            _freeRanges[writeIndex++] = range;
        }
        _freeRanges.resize(writeIndex);
    }

    void MeshletHistoryAllocator::TrimFreeTail()
    {
        while (!_freeRanges.empty())
        {
            const MeshletHistoryRange& range = _freeRanges.back();
            if (range.wordOffset + range.wordCount != _addressSpaceWords)
                break;

            _addressSpaceWords = range.wordOffset;
            _freeRanges.pop_back();
        }
    }
} // namespace ModelScene
