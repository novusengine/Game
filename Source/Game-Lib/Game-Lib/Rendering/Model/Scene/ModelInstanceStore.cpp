#include "ModelInstanceStore.h"

#include <Renderer/Renderer.h>

#include <algorithm>

#include <tracy/Tracy.hpp>

namespace ModelScene
{
    ModelInstanceStore::ModelInstanceStore(bool validateTransfers) : _records(validateTransfers), _shadowCasters(validateTransfers)
    {
        _records.SetDebugName("Scene Model Instances");
        _records.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
        _shadowCasters.SetDebugName("Scene Model Shadow Casters");
        _shadowCasters.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
    }

    void ModelInstanceStore::Reserve(u32 instanceCount)
    {
        const u32 reusableSlots = static_cast<u32>(_freeSlots.size());
        const u32 additionalSlots = instanceCount > reusableSlots ? instanceCount - reusableSlots : 0;
        _records.Reserve(additionalSlots);
        _shadowCasters.Reserve(additionalSlots);
        _slots.reserve(_slots.size() + additionalSlots);
        _pendingSlotClears.reserve(_pendingSlotClears.size() + instanceCount);
        _membershipChanges.reserve(_membershipChanges.size() + instanceCount);
        _routingChanges.reserve(_routingChanges.size() + instanceCount);
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
        slot.meshletCount = info.meshletCount;
        _activeMeshlets += info.meshletCount;

        ModelInstanceGPURecord& record = _records[slotIndex];
        u32 replayRevision = reusedSlot ? record.replayRevision + 1u : 1u;
        if (replayRevision == 0u)
            replayRevision = 1u;
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
        record.flags = ModelInstanceFlagPendingPublication | ModelInstanceFlagCastsShadows;
        record.packedHighlightColor = 0xFFFFFFFFu;
        record.highlightIntensity = 1.0f;
        record.opacity = 1.0f;
        record.replayRevision = replayRevision;
        if (info.privateMaterials)
            record.flags |= ModelInstanceFlagPrivateMaterials;
        if (reusedSlot)
            _records.SetDirtyElement(slotIndex);

        QueueSlotClear(slotIndex);
        QueueMembershipChange(slotIndex);
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
        RemoveShadowCaster(slotIndex);
        if (_records[slotIndex].highlightIntensity != 1.0f)
        {
            --_highlightedInstances;
            std::erase(_highlightedSlots, slotIndex);
        }
        if (slot->state == SlotState::Live)
            _liveInstances--;
        else
            _pendingInstances--;

        slot->state = SlotState::Free;
        slot->desiredVisible = false;
        slot->frameAdvanceQueued = false;
        slot->resources = {};
        _activeMeshlets -= slot->meshletCount;
        slot->meshletCount = 0;
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
        QueueMembershipChange(slotIndex);
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
            QueueSlotClear(RenderScenes::GetModelInstanceSlot(handle));
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

    bool ModelInstanceStore::SetTransformHistory(RenderScenes::ModelInstanceHandle handle, const mat4x4& currentTransform, const mat4x4& previousTransform, bool motionValid)
    {
        Slot* slot = GetSlot(handle);
        if (!slot)
        {
            RecordStaleHandle();
            return false;
        }

        ModelInstanceGPURecord& record = _records[RenderScenes::GetModelInstanceSlot(handle)];
        record.currentWorld = currentTransform;
        record.previousWorld = motionValid ? previousTransform : currentTransform;
        record.flags &= ~ModelInstanceFlagTeleported;
        if (motionValid && slot->state == SlotState::Live && slot->desiredVisible)
            record.flags |= ModelInstanceFlagMotionValid;
        else
            record.flags &= ~ModelInstanceFlagMotionValid;
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
            QueueSlotClear(RenderScenes::GetModelInstanceSlot(handle));
        }
        _records.SetDirtyElement(RenderScenes::GetModelInstanceSlot(handle));
        RefreshShadowCaster(RenderScenes::GetModelInstanceSlot(handle));
        return true;
    }

    bool ModelInstanceStore::SetHighlight(RenderScenes::ModelInstanceHandle handle, f32 intensity, u32 packedColor)
    {
        Slot* slot = GetSlot(handle);
        if (!slot)
        {
            RecordStaleHandle();
            return false;
        }

        ModelInstanceGPURecord& record = _records[RenderScenes::GetModelInstanceSlot(handle)];
        const bool wasHighlighted = record.highlightIntensity != 1.0f;
        const bool isHighlighted = intensity != 1.0f;
        if (!wasHighlighted && isHighlighted)
        {
            ++_highlightedInstances;
            _highlightedSlots.push_back(RenderScenes::GetModelInstanceSlot(handle));
        }
        else if (wasHighlighted && !isHighlighted)
        {
            --_highlightedInstances;
            std::erase(_highlightedSlots, RenderScenes::GetModelInstanceSlot(handle));
        }
        record.highlightIntensity = intensity;
        record.packedHighlightColor = packedColor;
        _records.SetDirtyElement(RenderScenes::GetModelInstanceSlot(handle));
        return true;
    }

    bool ModelInstanceStore::SetOpacity(RenderScenes::ModelInstanceHandle handle, f32 opacity, bool forceTransparent)
    {
        Slot* slot = GetSlot(handle);
        if (!slot)
        {
            RecordStaleHandle();
            return false;
        }

        const u32 slotIndex = RenderScenes::GetModelInstanceSlot(handle);
        ModelInstanceGPURecord& record = _records[slotIndex];
        record.opacity = std::clamp(opacity, 0.0f, 1.0f);
        if (forceTransparent || record.opacity < 1.0f)
            record.flags |= ModelInstanceFlagForceTransparent;
        else
            record.flags &= ~ModelInstanceFlagForceTransparent;
        _records.SetDirtyElement(slotIndex);
        RefreshShadowCaster(slotIndex);
        return true;
    }

    bool ModelInstanceStore::SetShadowDynamic(RenderScenes::ModelInstanceHandle handle, bool dynamic)
    {
        Slot* slot = GetSlot(handle);
        if (!slot)
            return false;

        const u32 slotIndex = RenderScenes::GetModelInstanceSlot(handle);
        ModelInstanceGPURecord& record = _records[slotIndex];
        const u32 oldFlags = record.flags;
        if (dynamic)
            record.flags |= ModelInstanceFlagDynamicShadowCaster;
        else
            record.flags &= ~ModelInstanceFlagDynamicShadowCaster;
        if (record.flags != oldFlags)
            _records.SetDirtyElement(slotIndex);
        return true;
    }

    bool ModelInstanceStore::SetCastsShadows(RenderScenes::ModelInstanceHandle handle, bool castsShadows)
    {
        Slot* slot = GetSlot(handle);
        if (!slot)
        {
            RecordStaleHandle();
            return false;
        }

        const u32 slotIndex = RenderScenes::GetModelInstanceSlot(handle);
        ModelInstanceGPURecord& record = _records[slotIndex];
        if (castsShadows)
            record.flags |= ModelInstanceFlagCastsShadows;
        else
            record.flags &= ~ModelInstanceFlagCastsShadows;
        _records.SetDirtyElement(slotIndex);
        RefreshShadowCaster(slotIndex);
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

    bool ModelInstanceStore::InvalidateReplay(RenderScenes::ModelInstanceHandle handle)
    {
        Slot* slot = GetSlot(handle);
        if (!slot)
        {
            RecordStaleHandle();
            return false;
        }

        const u32 slotIndex = RenderScenes::GetModelInstanceSlot(handle);
        ModelInstanceGPURecord& record = _records[slotIndex];
        ++record.replayRevision;
        if (record.replayRevision == 0)
            record.replayRevision = 1;
        _records.SetDirtyElement(slotIndex);
        return true;
    }

    bool ModelInstanceStore::SetPoseSource(RenderScenes::ModelInstanceHandle handle, Animation::PoseSourceHandle poseSource)
    {
        Slot* slot = GetSlot(handle);
        if (!slot)
        {
            RecordStaleHandle();
            return false;
        }
        if (slot->resources.poseSource == poseSource)
            return true;
        slot->resources.poseSource = poseSource;
        slot->resources.lastDeformationFrame = 0;
        const u32 index = RenderScenes::GetModelInstanceSlot(handle);
        _records[index].flags &= ~ModelInstanceFlagMotionValid;
        _records.SetDirtyElement(index);
        QueueFrameAdvance(index);
        return SetDeformationHandle(handle, RenderScenes::INVALID_SCENE_INDEX);
    }

    bool ModelInstanceStore::MarkDeformationFrame(RenderScenes::ModelInstanceHandle handle, u64 frame, bool succeeded)
    {
        Slot* slot = GetSlot(handle);
        if (!slot)
            return false;
        const u32 index = RenderScenes::GetModelInstanceSlot(handle);
        ModelInstanceGPURecord& record = _records[index];
        const bool valid = succeeded && slot->resources.lastDeformationFrame != 0 && slot->resources.lastDeformationFrame + 1 == frame;
        record.flags = valid ? record.flags | ModelInstanceFlagDeformationMotionValid : record.flags & ~ModelInstanceFlagDeformationMotionValid;
        if (succeeded)
            slot->resources.lastDeformationFrame = frame;
        _records.SetDirtyElement(index);
        return true;
    }

    bool ModelInstanceStore::SetDeformationHandle(RenderScenes::ModelInstanceHandle handle, u32 deformationHandle)
    {
        Slot* slot = GetSlot(handle);
        if (!slot)
        {
            RecordStaleHandle();
            return false;
        }
        const u32 slotIndex = RenderScenes::GetModelInstanceSlot(handle);
        ModelInstanceGPURecord& record = _records[slotIndex];
        if (record.deformationHandle == deformationHandle)
            return true;
        record.deformationHandle = deformationHandle;
        if (deformationHandle == RenderScenes::INVALID_SCENE_INDEX)
        {
            record.flags &= ~ModelInstanceFlagDeformationMotionValid;
        }
        _records.SetDirtyElement(slotIndex);
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
            RefreshShadowCaster(entry.slotIndex);
        }
        if (dirtyRangeCount != 0)
            _records.SetDirtyElements(dirtyRangeStart, dirtyRangeCount);
        _pendingPublications.clear();
    }

    void ModelInstanceStore::SyncToGPU(Renderer::Renderer* renderer)
    {
        _records.SyncToGPU(renderer);
        _shadowCasters.SyncToGPU(renderer);
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

    bool ModelInstanceStore::IsVisible(RenderScenes::ModelInstanceHandle handle) const
    {
        const Slot* slot = GetSlot(handle);
        return slot && slot->desiredVisible;
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

    RenderScenes::ModelInstanceHandle ModelInstanceStore::GetHandleAtSlot(u32 slotIndex) const
    {
        if (slotIndex >= _slots.size() || _slots[slotIndex].state == SlotState::Free)
            return RenderScenes::InvalidModelInstanceHandle();
        return RenderScenes::MakeModelInstanceHandle(slotIndex, _slots[slotIndex].generation);
    }

    void ModelInstanceStore::AcknowledgePendingSlotClears()
    {
        for (const u32 slotIndex : _pendingSlotClears)
            if (slotIndex < _slots.size())
                _slots[slotIndex].clearQueued = false;
        _pendingSlotClears.clear();
    }

    void ModelInstanceStore::AcknowledgeMembershipChanges()
    {
        for (const u32 slotIndex : _membershipChanges)
            if (slotIndex < _slots.size())
                _slots[slotIndex].membershipChangeQueued = false;
        _membershipChanges.clear();
    }

    void ModelInstanceStore::QueueRoutingChange(RenderScenes::ModelInstanceHandle handle)
    {
        Slot* slot = GetSlot(handle);
        if (!slot || slot->routingChangeQueued)
            return;
        slot->routingChangeQueued = true;
        _routingChanges.push_back(RenderScenes::GetModelInstanceSlot(handle));
    }

    void ModelInstanceStore::AcknowledgeRoutingChanges()
    {
        for (const u32 slotIndex : _routingChanges)
            if (slotIndex < _slots.size())
                _slots[slotIndex].routingChangeQueued = false;
        _routingChanges.clear();
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

    void ModelInstanceStore::CollectHighlightedHandles(std::vector<RenderScenes::ModelInstanceHandle>& outHandles) const
    {
        outHandles.clear();
        outHandles.reserve(_highlightedSlots.size());
        for (const u32 slotIndex : _highlightedSlots)
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
                .staleHandleRejects = _staleHandleRejects,
                .activeMeshlets = _activeMeshlets};
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

    void ModelInstanceStore::QueueSlotClear(u32 slotIndex)
    {
        Slot& slot = _slots[slotIndex];
        if (slot.clearQueued)
            return;
        slot.clearQueued = true;
        _pendingSlotClears.push_back(slotIndex);
    }

    void ModelInstanceStore::QueueMembershipChange(u32 slotIndex)
    {
        Slot& slot = _slots[slotIndex];
        if (slot.membershipChangeQueued)
            return;
        slot.membershipChangeQueued = true;
        _membershipChanges.push_back(slotIndex);
    }

    void ModelInstanceStore::RefreshShadowCaster(u32 slotIndex)
    {
        Slot& slot = _slots[slotIndex];
        const ModelInstanceGPURecord& record = _records[slotIndex];
        const bool eligible = slot.state == SlotState::Live &&
                              (record.flags & (ModelInstanceFlagVisible | ModelInstanceFlagCastsShadows)) ==
                                  (ModelInstanceFlagVisible | ModelInstanceFlagCastsShadows) &&
                              record.opacity > 0.0f;
        if (!eligible)
        {
            RemoveShadowCaster(slotIndex);
            return;
        }
        if (slot.shadowCasterIndex != RenderScenes::INVALID_SCENE_INDEX)
            return;

        slot.shadowCasterIndex = _shadowCasters.Add(slotIndex);
    }

    void ModelInstanceStore::RemoveShadowCaster(u32 slotIndex)
    {
        Slot& slot = _slots[slotIndex];
        const u32 casterIndex = slot.shadowCasterIndex;
        if (casterIndex == RenderScenes::INVALID_SCENE_INDEX)
            return;

        const u32 lastIndex = _shadowCasters.Count() - 1u;
        if (casterIndex != lastIndex)
        {
            const u32 movedSlotIndex = _shadowCasters[lastIndex];
            _shadowCasters[casterIndex] = movedSlotIndex;
            _shadowCasters.SetDirtyElement(casterIndex);
            _slots[movedSlotIndex].shadowCasterIndex = casterIndex;
        }
        _shadowCasters.Remove(lastIndex);
        _shadowCasters.Compress();
        slot.shadowCasterIndex = RenderScenes::INVALID_SCENE_INDEX;
    }
} // namespace ModelScene
