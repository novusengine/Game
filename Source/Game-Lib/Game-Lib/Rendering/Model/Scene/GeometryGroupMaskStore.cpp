#include "GeometryGroupMaskStore.h"

#include <Renderer/Renderer.h>

#include <algorithm>

#include <tracy/Tracy.hpp>

namespace ModelScene
{
    GeometryGroupMaskStore::GeometryGroupMaskStore(bool validateTransfers)
        : _masks(validateTransfers)
    {
        _masks.SetDebugName("Scene Model Geometry Groups");
        _masks.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
    }

    void GeometryGroupMaskStore::Reserve(u32 maskCount, u32 wordCount)
    {
        if (wordCount > _masks.Count())
            _masks.Reserve(wordCount - _masks.Count());
        _records.reserve(std::max(_records.size(), static_cast<size_t>(maskCount)));
    }

    RenderScenes::GeometryGroupMaskHandle GeometryGroupMaskStore::Create(u32 groupCount, bool enabledByDefault)
    {
        ZoneScopedN("GeometryGroupMaskStore::Create");

        const u32 wordCount = (groupCount + 31u) / 32u;
        const RenderScenes::StableRange range = _wordAllocator.Allocate(wordCount);
        const u32 previousWordCount = _masks.Count();
        if (range.offset + range.count > previousWordCount)
            _masks.Reserve(range.offset + range.count - previousWordCount);
        EnsureWordCapacity(range.offset + range.count);

        u32 recordIndex = 0;
        if (_freeRecordIndices.empty())
        {
            recordIndex = static_cast<u32>(_records.size());
            _records.emplace_back();
        }
        else
        {
            recordIndex = _freeRecordIndices.back();
            _freeRecordIndices.pop_back();
        }

        _records[recordIndex] = { range, groupCount, true };
        _liveMasks++;
        _liveWords += wordCount;

        const RenderScenes::GeometryGroupMaskHandle handle(recordIndex);
        for (u32 index = 0; index < range.count; ++index)
            _masks[range.offset + index] = enabledByDefault ? 0xFFFFFFFFu : 0u;
        if (enabledByDefault && range.count != 0 && (groupCount % 32u) != 0)
            _masks[range.offset + range.count - 1] = (1u << (groupCount % 32u)) - 1u;
        if (range.count != 0 && range.offset < previousWordCount)
            _masks.SetDirtyElements(range.offset, range.count);
        return handle;
    }

    void GeometryGroupMaskStore::Release(RenderScenes::GeometryGroupMaskHandle handle)
    {
        ZoneScopedN("GeometryGroupMaskStore::Release");

        Mask* mask = GetMask(handle);
        if (!mask)
            return;

        _wordAllocator.Free(mask->range);
        _liveMasks--;
        _liveWords -= mask->range.count;
        mask->alive = false;
        mask->range = {};
        mask->groupCount = 0;
        _freeRecordIndices.push_back(static_cast<u32>(static_cast<RenderScenes::GeometryGroupMaskHandle::type>(handle)));
    }

    bool GeometryGroupMaskStore::SetEnabled(RenderScenes::GeometryGroupMaskHandle handle, u32 groupID, bool enabled)
    {
        bool changed = false;
        return SetRangeEnabled(handle, groupID, groupID, enabled, changed);
    }

    bool GeometryGroupMaskStore::SetRangeEnabled(RenderScenes::GeometryGroupMaskHandle handle, u32 firstGroupID, u32 lastGroupID, bool enabled, bool& outChanged)
    {
        Mask* mask = GetMask(handle);
        outChanged = false;
        if (!mask || firstGroupID > lastGroupID || lastGroupID >= mask->groupCount)
            return false;

        const u32 firstWord = firstGroupID / 32u;
        const u32 lastWord = lastGroupID / 32u;
        for (u32 word = firstWord; word <= lastWord; ++word)
        {
            const u32 firstBit = word == firstWord ? firstGroupID % 32u : 0u;
            const u32 lastBit = word == lastWord ? lastGroupID % 32u : 31u;
            const u32 lowMask = 0xFFFFFFFFu << firstBit;
            const u32 highMask = lastBit == 31u ? 0xFFFFFFFFu : (1u << (lastBit + 1u)) - 1u;
            const u32 rangeMask = lowMask & highMask;
            const u32 index = mask->range.offset + word;
            const u32 previous = _masks[index];
            _masks[index] = enabled ? previous | rangeMask : previous & ~rangeMask;
            if (_masks[index] != previous)
            {
                _masks.SetDirtyElement(index);
                outChanged = true;
            }
        }
        return true;
    }

    bool GeometryGroupMaskStore::SetAll(RenderScenes::GeometryGroupMaskHandle handle, bool enabled, bool& outChanged)
    {
        const Mask* mask = GetMask(handle);
        if (!mask)
            return false;
        if (mask->groupCount == 0)
        {
            outChanged = false;
            return true;
        }
        return SetRangeEnabled(handle, 0, mask->groupCount - 1u, enabled, outChanged);
    }

    bool GeometryGroupMaskStore::IsEnabled(RenderScenes::GeometryGroupMaskHandle handle, u32 groupID) const
    {
        const Mask* mask = GetMask(handle);
        if (!mask || groupID >= mask->groupCount)
            return false;

        const u32 word = _masks[mask->range.offset + (groupID / 32u)];
        return (word & (1u << (groupID % 32u))) != 0;
    }

    void GeometryGroupMaskStore::SyncToGPU(Renderer::Renderer* renderer)
    {
        _masks.SyncToGPU(renderer);
    }

    bool GeometryGroupMaskStore::IsValid(RenderScenes::GeometryGroupMaskHandle handle) const
    {
        return GetMask(handle) != nullptr;
    }

    u32 GeometryGroupMaskStore::GetOffset(RenderScenes::GeometryGroupMaskHandle handle) const
    {
        const Mask* mask = GetMask(handle);
        return mask ? mask->range.offset : 0;
    }

    u32 GeometryGroupMaskStore::GetWordCount(RenderScenes::GeometryGroupMaskHandle handle) const
    {
        const Mask* mask = GetMask(handle);
        return mask ? mask->range.count : 0;
    }

    u32 GeometryGroupMaskStore::GetGroupCount(RenderScenes::GeometryGroupMaskHandle handle) const
    {
        const Mask* mask = GetMask(handle);
        return mask ? mask->groupCount : 0;
    }

    GeometryGroupMaskStoreStats GeometryGroupMaskStore::GetStats() const
    {
        GeometryGroupMaskStoreStats stats;
        stats.liveMasks = _liveMasks;
        stats.liveWords = _liveWords;
        stats.allocatedWords = _wordAllocator.GetAddressSpaceSize();
        stats.freeWords = _wordAllocator.GetFreeCount();
        return stats;
    }

    GeometryGroupMaskStore::Mask* GeometryGroupMaskStore::GetMask(RenderScenes::GeometryGroupMaskHandle handle)
    {
        const u32 index = static_cast<RenderScenes::GeometryGroupMaskHandle::type>(handle);
        return index < _records.size() && _records[index].alive ? &_records[index] : nullptr;
    }

    const GeometryGroupMaskStore::Mask* GeometryGroupMaskStore::GetMask(RenderScenes::GeometryGroupMaskHandle handle) const
    {
        const u32 index = static_cast<RenderScenes::GeometryGroupMaskHandle::type>(handle);
        return index < _records.size() && _records[index].alive ? &_records[index] : nullptr;
    }

    void GeometryGroupMaskStore::EnsureWordCapacity(u32 count)
    {
        if (_masks.Count() < count)
            _masks.AddCount(count - _masks.Count());
    }
} // namespace ModelScene
