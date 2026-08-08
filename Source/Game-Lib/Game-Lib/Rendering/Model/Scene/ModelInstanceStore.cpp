#include "ModelInstanceStore.h"

#include <Renderer/Renderer.h>

#include <algorithm>

#include <tracy/Tracy.hpp>

namespace ModelScene
{
    ModelInstanceStore::ModelInstanceStore(bool validateTransfers) : _records(validateTransfers)
    {
        _records.SetDebugName("Scene Model Instances");
        _records.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
    }

    void ModelInstanceStore::Reserve(u32 instanceCount)
    {
        const u32 reusableSlots = static_cast<u32>(_freeSlots.size());
        const u32 additionalSlots = instanceCount > reusableSlots ? instanceCount - reusableSlots : 0;
        _records.Reserve(additionalSlots);
        _slots.reserve(_slots.size() + additionalSlots);
        _pendingSlotClears.reserve(_pendingSlotClears.size() + instanceCount);
        _pendingPublications.reserve(_pendingPublications.size() + instanceCount);
        _frameAdvanceEntries.reserve(_frameAdvanceEntries.size() + instanceCount);
    }

    RenderScenes::ModelInstanceHandle ModelInstanceStore::Create(const ModelInstanceCreateInfo& info)
    {
        ZoneScopedN("ModelInstanceStore::Create");

        u32 slotIndex = 0;
        const bool reusedSlot = !_freeSlots.empty();
        if (_freeSlots.empty())
        {
            slotIndex = static_cast<u32>(_slots.size());
            _slots.emplace_back();
            {
                ZoneScopedN("Grow Instance GPUVector");
                _records.Add();
            }
        }
        else
        {
            slotIndex = _freeSlots.back();
            _freeSlots.pop_back();
        }

        Slot& slot = _slots[slotIndex];
        slot.resources = {info.model, info.materialTable, info.geometryGroupMask, info.meshletHistory};
        slot.state = SlotState::Pending;
        slot.desiredVisible = info.visible;
        slot.frameAdvanceQueued = false;

        ModelInstanceGPURecord& record = _records[slotIndex];
        record = {};
        record.currentWorld = info.worldTransform;
        record.previousWorld = info.worldTransform;
        record.modelIndex = static_cast<RenderAssets::ModelHandle::type>(info.model);
        record.materialTableOffset = info.materialTableOffset;
        record.materialTableCount = info.materialTableCount;
        record.geometryGroupWordOffset = info.geometryGroupWordOffset;
        record.geometryGroupWordCount = info.geometryGroupWordCount;
        record.meshletHistoryWordOffset = info.meshletHistory.wordOffset;
        record.meshletHistoryWordCount = info.meshletHistory.wordCount;
        record.generation = slot.generation;
        record.flags = ModelInstanceFlagPendingPublication;
        if (info.privateMaterials)
            record.flags |= ModelInstanceFlagPrivateMaterials;
        if (reusedSlot)
            _records.SetDirtyElement(slotIndex);

        _pendingSlotClears.push_back(slotIndex);
        _pendingPublications.push_back({slotIndex, slot.generation});
        _pendingInstances++;
        ++_membershipRevision;
        return RenderScenes::MakeModelInstanceHandle(slotIndex, slot.generation);
    }

    bool ModelInstanceStore::Destroy(RenderScenes::ModelInstanceHandle handle, ModelInstanceResources& outResources)
    {
        Slot* slot = GetSlot(handle);
        if (!slot)
        {
            RecordStaleHandle();
            return false;
        }

        const u32 slotIndex = RenderScenes::GetModelInstanceSlot(handle);
        outResources = slot->resources;
        if (slot->state == SlotState::Live)
            _liveInstances--;
        else
            _pendingInstances--;

        slot->state = SlotState::Free;
        slot->desiredVisible = false;
        slot->frameAdvanceQueued = false;
        slot->resources = {};
        slot->generation++;
        if (slot->generation == 0)
            slot->generation = 1;

        ModelInstanceGPURecord& record = _records[slotIndex];
        record.flags = 0;
        record.generation = slot->generation;
        {
            ZoneScopedN("Mark Instance Record Dirty");
            _records.SetDirtyElement(slotIndex);
        }
        _freeSlots.push_back(slotIndex);
        ++_membershipRevision;
        return true;
    }

    bool ModelInstanceStore::SetTransform(RenderScenes::ModelInstanceHandle handle, const mat4x4& transform,
                                          bool teleported, bool& outNeedsHistoryClear)
    {
        Slot* slot = GetSlot(handle);
        if (!slot)
        {
            RecordStaleHandle();
            return false;
        }

        ModelInstanceGPURecord& record = _records[RenderScenes::GetModelInstanceSlot(handle)];
        record.currentWorld = transform;
        outNeedsHistoryClear = teleported;
        if (teleported)
        {
            record.previousWorld = transform;
            record.flags |= ModelInstanceFlagTeleported;
            record.flags &= ~ModelInstanceFlagMotionValid;
            if (std::find(_pendingSlotClears.begin(), _pendingSlotClears.end(),
                          RenderScenes::GetModelInstanceSlot(handle)) == _pendingSlotClears.end())
            {
                _pendingSlotClears.push_back(RenderScenes::GetModelInstanceSlot(handle));
            }
        }
        else if (slot->state == SlotState::Pending)
        {
            record.previousWorld = transform;
        }
        if (slot->state == SlotState::Live)
            QueueFrameAdvance(RenderScenes::GetModelInstanceSlot(handle));
        _records.SetDirtyElement(RenderScenes::GetModelInstanceSlot(handle));
        return true;
    }

    bool ModelInstanceStore::SetVisible(RenderScenes::ModelInstanceHandle handle, bool visible,
                                        bool& outNeedsHistoryClear)
    {
        Slot* slot = GetSlot(handle);
        if (!slot)
        {
            RecordStaleHandle();
            return false;
        }

        outNeedsHistoryClear = false;
        if (slot->desiredVisible == visible)
            return true;

        slot->desiredVisible = visible;
        ModelInstanceGPURecord& record = _records[RenderScenes::GetModelInstanceSlot(handle)];
        if (!visible)
        {
            record.flags &= ~(ModelInstanceFlagVisible | ModelInstanceFlagMotionValid);
        }
        else if (slot->state == SlotState::Live)
        {
            record.previousWorld = record.currentWorld;
            record.flags |= ModelInstanceFlagVisible | ModelInstanceFlagNew;
            record.flags &= ~ModelInstanceFlagMotionValid;
            outNeedsHistoryClear = true;
            QueueFrameAdvance(RenderScenes::GetModelInstanceSlot(handle));
            if (std::find(_pendingSlotClears.begin(), _pendingSlotClears.end(),
                          RenderScenes::GetModelInstanceSlot(handle)) == _pendingSlotClears.end())
            {
                _pendingSlotClears.push_back(RenderScenes::GetModelInstanceSlot(handle));
            }
        }
        _records.SetDirtyElement(RenderScenes::GetModelInstanceSlot(handle));
        return true;
    }

    bool ModelInstanceStore::SetMaterialTable(RenderScenes::ModelInstanceHandle handle,
                                              RenderScenes::ModelMaterialTableHandle table, u32 offset, u32 count,
                                              bool isPrivate)
    {
        Slot* slot = GetSlot(handle);
        if (!slot)
        {
            RecordStaleHandle();
            return false;
        }

        slot->resources.materialTable = table;
        ModelInstanceGPURecord& record = _records[RenderScenes::GetModelInstanceSlot(handle)];
        record.materialTableOffset = offset;
        record.materialTableCount = count;
        if (isPrivate)
            record.flags |= ModelInstanceFlagPrivateMaterials;
        else
            record.flags &= ~ModelInstanceFlagPrivateMaterials;
        _records.SetDirtyElement(RenderScenes::GetModelInstanceSlot(handle));
        return true;
    }

    void ModelInstanceStore::AdvanceFrame()
    {
        std::sort(_frameAdvanceEntries.begin(), _frameAdvanceEntries.end(),
                  [](const SlotGenerationEntry& left, const SlotGenerationEntry& right)
                  { return left.slotIndex < right.slotIndex; });

        u32 dirtyRangeStart = 0;
        u32 dirtyRangeCount = 0;
        for (const SlotGenerationEntry& entry : _frameAdvanceEntries)
        {
            if (entry.slotIndex >= _slots.size())
                continue;

            Slot& slot = _slots[entry.slotIndex];
            if (slot.generation != entry.generation || !slot.frameAdvanceQueued)
                continue;
            slot.frameAdvanceQueued = false;
            if (slot.state != SlotState::Live)
                continue;

            if (dirtyRangeCount != 0 && entry.slotIndex != dirtyRangeStart + dirtyRangeCount)
            {
                _records.SetDirtyElements(dirtyRangeStart, dirtyRangeCount);
                dirtyRangeCount = 0;
            }

            ModelInstanceGPURecord& record = _records[entry.slotIndex];
            record.previousWorld = record.currentWorld;
            record.flags &= ~(ModelInstanceFlagNew | ModelInstanceFlagTeleported);
            if (slot.desiredVisible)
                record.flags |= ModelInstanceFlagMotionValid;
            if (dirtyRangeCount == 0)
                dirtyRangeStart = entry.slotIndex;
            dirtyRangeCount++;
        }
        if (dirtyRangeCount != 0)
            _records.SetDirtyElements(dirtyRangeStart, dirtyRangeCount);
        _frameAdvanceEntries.clear();
    }

    void ModelInstanceStore::PublishPending()
    {
        std::sort(_pendingPublications.begin(), _pendingPublications.end(),
                  [](const SlotGenerationEntry& left, const SlotGenerationEntry& right)
                  { return left.slotIndex < right.slotIndex; });

        u32 dirtyRangeStart = 0;
        u32 dirtyRangeCount = 0;
        for (const SlotGenerationEntry& entry : _pendingPublications)
        {
            if (entry.slotIndex >= _slots.size())
                continue;

            Slot& slot = _slots[entry.slotIndex];
            if (slot.generation != entry.generation || slot.state != SlotState::Pending)
                continue;

            if (dirtyRangeCount != 0 && entry.slotIndex != dirtyRangeStart + dirtyRangeCount)
            {
                _records.SetDirtyElements(dirtyRangeStart, dirtyRangeCount);
                dirtyRangeCount = 0;
            }

            slot.state = SlotState::Live;
            ModelInstanceGPURecord& record = _records[entry.slotIndex];
            record.flags &= ~ModelInstanceFlagPendingPublication;
            record.flags |= ModelInstanceFlagNew;
            if (slot.desiredVisible)
                record.flags |= ModelInstanceFlagVisible;
            QueueFrameAdvance(entry.slotIndex);
            if (dirtyRangeCount == 0)
                dirtyRangeStart = entry.slotIndex;
            dirtyRangeCount++;
            _pendingInstances--;
            _liveInstances++;
        }
        if (dirtyRangeCount != 0)
            _records.SetDirtyElements(dirtyRangeStart, dirtyRangeCount);
        _pendingPublications.clear();
    }

    void ModelInstanceStore::SyncToGPU(Renderer::Renderer* renderer)
    {
        _records.SyncToGPU(renderer);
    }

    bool ModelInstanceStore::IsAlive(RenderScenes::ModelInstanceHandle handle) const
    {
        const Slot* slot = GetSlot(handle);
        return slot && slot->state == SlotState::Live;
    }

    bool ModelInstanceStore::IsPending(RenderScenes::ModelInstanceHandle handle) const
    {
        const Slot* slot = GetSlot(handle);
        return slot && slot->state == SlotState::Pending;
    }

    const ModelInstanceGPURecord* ModelInstanceStore::GetRecord(RenderScenes::ModelInstanceHandle handle) const
    {
        const Slot* slot = GetSlot(handle);
        return slot ? &_records[RenderScenes::GetModelInstanceSlot(handle)] : nullptr;
    }

    const ModelInstanceResources* ModelInstanceStore::GetResources(RenderScenes::ModelInstanceHandle handle) const
    {
        const Slot* slot = GetSlot(handle);
        return slot ? &slot->resources : nullptr;
    }

    void ModelInstanceStore::CollectActiveHandles(std::vector<RenderScenes::ModelInstanceHandle>& outHandles) const
    {
        outHandles.clear();
        outHandles.reserve(_liveInstances + _pendingInstances);
        for (u32 slotIndex = 0; slotIndex < _slots.size(); ++slotIndex)
        {
            const Slot& slot = _slots[slotIndex];
            if (slot.state != SlotState::Free)
                outHandles.push_back(RenderScenes::MakeModelInstanceHandle(slotIndex, slot.generation));
        }
    }

    ModelInstanceStoreStats ModelInstanceStore::GetStats() const
    {
        return {.liveInstances = _liveInstances,
                .pendingInstances = _pendingInstances,
                .freeSlots = static_cast<u32>(_freeSlots.size()),
                .slotCapacity = static_cast<u32>(_slots.size()),
                .pendingSlotClears = static_cast<u32>(_pendingSlotClears.size()),
                .staleHandleRejects = _staleHandleRejects};
    }

    ModelInstanceStore::Slot* ModelInstanceStore::GetSlot(RenderScenes::ModelInstanceHandle handle)
    {
        const u32 slotIndex = RenderScenes::GetModelInstanceSlot(handle);
        const u32 generation = RenderScenes::GetModelInstanceGeneration(handle);
        if (slotIndex >= _slots.size())
            return nullptr;

        Slot& slot = _slots[slotIndex];
        return slot.state != SlotState::Free && slot.generation == generation ? &slot : nullptr;
    }

    const ModelInstanceStore::Slot* ModelInstanceStore::GetSlot(RenderScenes::ModelInstanceHandle handle) const
    {
        const u32 slotIndex = RenderScenes::GetModelInstanceSlot(handle);
        const u32 generation = RenderScenes::GetModelInstanceGeneration(handle);
        if (slotIndex >= _slots.size())
            return nullptr;

        const Slot& slot = _slots[slotIndex];
        return slot.state != SlotState::Free && slot.generation == generation ? &slot : nullptr;
    }

    void ModelInstanceStore::QueueFrameAdvance(u32 slotIndex)
    {
        Slot& slot = _slots[slotIndex];
        if (slot.frameAdvanceQueued)
            return;

        slot.frameAdvanceQueued = true;
        _frameAdvanceEntries.push_back({slotIndex, slot.generation});
    }
} // namespace ModelScene
