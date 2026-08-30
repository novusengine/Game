#include "PoseSourceStore.h"

namespace Animation
{
    PoseSourceHandle PoseSourceStore::CreateRestPose(AnimationLoading::SkeletonHandle skeleton, const AnimationLoading::SkeletonAssetView& skeletonView)
    {
        const PoseEvaluationResult evaluated = EvaluateRestPose(skeletonView);
        if (!evaluated)
            return {};

        u32 slotIndex = 0;
        if (_freeSlots.empty())
        {
            slotIndex = static_cast<u32>(_slots.size());
            _slots.emplace_back();
        }
        else
        {
            slotIndex = _freeSlots.back();
            _freeSlots.pop_back();
        }

        Slot& slot = _slots[slotIndex];
        slot.live = true;
        slot.source.skeleton = skeleton;
        slot.source.localPose.clear();
        slot.source.localPose.reserve(skeletonView.joints.size());
        for (const FileFormat::Animation::SkeletonJoint& joint : skeletonView.joints)
            slot.source.localPose.push_back(joint.restTransform);
        slot.source.palette = evaluated.palette;
        slot.source.poseGeneration = 1;
        slot.source.sampleTimeMicroseconds = 0;
        ++_poseEvaluations;
        return PoseSourceHandle::Create(slotIndex, slot.generation);
    }

    bool PoseSourceStore::Destroy(PoseSourceHandle handle)
    {
        Slot* slot = GetSlot(handle);
        if (!slot)
            return false;
        slot->live = false;
        slot->source = {};
        ++slot->generation;
        if (slot->generation == 0)
            slot->generation = 1;
        _freeSlots.push_back(handle.GetSlot());
        return true;
    }

    bool PoseSourceStore::SetLocalPose(PoseSourceHandle handle, std::span<const FileFormat::Animation::LocalTransform> localPose, const AnimationLoading::SkeletonAssetView& skeletonView, u64 sampleTimeMicroseconds)
    {
        Slot* slot = GetSlot(handle);
        if (!slot || localPose.size() != skeletonView.joints.size())
            return false;
        PoseEvaluationResult evaluated = EvaluatePose(skeletonView.joints, localPose);
        if (!evaluated)
            return false;
        slot->source.localPose.assign(localPose.begin(), localPose.end());
        slot->source.palette = std::move(evaluated.palette);
        slot->source.sampleTimeMicroseconds = sampleTimeMicroseconds;
        ++slot->source.poseGeneration;
        if (slot->source.poseGeneration == 0)
            slot->source.poseGeneration = 1;
        ++_poseEvaluations;
        return true;
    }

    bool PoseSourceStore::SetBoundsMetadata(PoseSourceHandle handle, FileFormat::AssetID clipAssetID, u64 sampleTimeMicroseconds, bool supportsTightBounds)
    {
        Slot* slot = GetSlot(handle);
        if (!slot)
            return false;
        slot->source.boundsClipAssetID = clipAssetID;
        slot->source.sampleTimeMicroseconds = sampleTimeMicroseconds;
        slot->source.supportsTightAnimationBounds = supportsTightBounds;
        return true;
    }

    bool PoseSourceStore::Invalidate(PoseSourceHandle handle)
    {
        Slot* slot = GetSlot(handle);
        if (!slot)
            return false;
        ++slot->source.poseGeneration;
        if (slot->source.poseGeneration == 0)
            slot->source.poseGeneration = 1;
        slot->source.participatedPreviousFrame = false;
        slot->source.participatedCurrentFrame = false;
        ++_invalidations;
        return true;
    }

    void PoseSourceStore::BeginDemandFrame(u64 frame)
    {
        if (_demandFrame == frame)
            return;
        _demandFrame = frame;
        for (Slot& slot : _slots)
        {
            if (!slot.live)
                continue;
            slot.source.participatedPreviousFrame = slot.source.participatedCurrentFrame;
            slot.source.participatedCurrentFrame = false;
        }
    }

    bool PoseSourceStore::MarkDemand(PoseSourceHandle handle, u64 frame)
    {
        BeginDemandFrame(frame);
        Slot* slot = GetSlot(handle);
        if (!slot)
            return false;
        slot->source.lastDemandFrame = frame;
        slot->source.participatedCurrentFrame = true;
        return true;
    }

    const PoseSource* PoseSourceStore::Get(PoseSourceHandle handle) const
    {
        const Slot* slot = GetSlot(handle);
        return slot ? &slot->source : nullptr;
    }

    PoseSourceStoreStats PoseSourceStore::GetStats() const
    {
        return {.liveSources = static_cast<u32>(_slots.size() - _freeSlots.size()), .freeSlots = static_cast<u32>(_freeSlots.size()), .staleHandleRejects = _staleHandleRejects, .poseEvaluations = _poseEvaluations, .invalidations = _invalidations};
    }

    PoseSourceStore::Slot* PoseSourceStore::GetSlot(PoseSourceHandle handle)
    {
        if (!handle || handle.GetSlot() >= _slots.size())
        {
            ++_staleHandleRejects;
            return nullptr;
        }
        Slot& slot = _slots[handle.GetSlot()];
        if (!slot.live || slot.generation != handle.GetGeneration())
        {
            ++_staleHandleRejects;
            return nullptr;
        }
        return &slot;
    }

    const PoseSourceStore::Slot* PoseSourceStore::GetSlot(PoseSourceHandle handle) const
    {
        if (!handle || handle.GetSlot() >= _slots.size())
        {
            ++_staleHandleRejects;
            return nullptr;
        }
        const Slot& slot = _slots[handle.GetSlot()];
        if (!slot.live || slot.generation != handle.GetGeneration())
        {
            ++_staleHandleRejects;
            return nullptr;
        }
        return &slot;
    }
} // namespace Animation
