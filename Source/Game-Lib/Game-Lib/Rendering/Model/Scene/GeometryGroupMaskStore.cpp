#include "GeometryGroupMaskStore.h"

#include <Renderer/Renderer.h>

#include <algorithm>

namespace ModelScene
{
    GeometryGroupMaskStore::GeometryGroupMaskStore(bool validateTransfers)
        : _masks(validateTransfers)
    {
        _masks.SetDebugName("Scene Model Geometry Groups");
        _masks.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
    }

    RenderScenes::GeometryGroupMaskHandle GeometryGroupMaskStore::Create(u32 groupCount, bool enabledByDefault)
    {
        const u32 wordCount = (groupCount + 31u) / 32u;
        const RenderScenes::StableRange range = _wordAllocator.Allocate(wordCount);
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
        SetAll(handle, enabledByDefault);
        return handle;
    }

    void GeometryGroupMaskStore::Release(RenderScenes::GeometryGroupMaskHandle handle)
    {
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
        Mask* mask = GetMask(handle);
        if (!mask || groupID >= mask->groupCount)
            return false;

        const u32 wordIndex = mask->range.offset + (groupID / 32u);
        const u32 bit = 1u << (groupID % 32u);
        if (enabled)
            _masks[wordIndex] |= bit;
        else
            _masks[wordIndex] &= ~bit;
        _masks.SetDirtyElement(wordIndex);
        return true;
    }

    bool GeometryGroupMaskStore::SetAll(RenderScenes::GeometryGroupMaskHandle handle, bool enabled)
    {
        Mask* mask = GetMask(handle);
        if (!mask)
            return false;

        for (u32 index = 0; index < mask->range.count; ++index)
            _masks[mask->range.offset + index] = enabled ? 0xFFFFFFFFu : 0u;

        if (enabled && mask->range.count != 0 && (mask->groupCount % 32u) != 0)
            _masks[mask->range.offset + mask->range.count - 1] = (1u << (mask->groupCount % 32u)) - 1u;

        if (mask->range.count != 0)
            _masks.SetDirtyElements(mask->range.offset, mask->range.count);
        return true;
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
