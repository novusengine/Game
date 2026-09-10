#include "PoseSourceStore.h"

#include <Base/Math/Math.h>

#include <algorithm>

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
        slot.source.modelTransforms = evaluated.modelTransforms;
        slot.source.previousModelTransforms = evaluated.modelTransforms;
        slot.source.poseGeneration = 1;
        slot.source.sampleTimeMicroseconds = 0;
        slot.source.motionValid = false;
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
        slot->source.modelTransforms = std::move(evaluated.modelTransforms);
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

    bool PoseSourceStore::RebindSkeleton(PoseSourceHandle handle, AnimationLoading::SkeletonHandle skeleton)
    {
        if (!handle || !skeleton || handle.GetSlot() >= _slots.size()) return false;
        Slot& slot = _slots[handle.GetSlot()];
        if (!slot.live || slot.generation != handle.GetGeneration()) return false;
        slot.source.skeleton = skeleton;
        return Invalidate(handle);
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
        slot->source.motionValid = false;
        ++slot->source.motionGeneration;
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
            slot.source.participatedPreviousFrame = slot.source.participatedCurrentFrame && slot.source.lastDemandFrame + 1 == frame;
            slot.source.participatedCurrentFrame = false;
            slot.source.motionValid = false;
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
        slot->source.motionValid = slot->source.participatedPreviousFrame && slot->source.previousModelTransforms.size() == slot->source.modelTransforms.size();
        return true;
    }

    void PoseSourceStore::CommitDemandFrame()
    {
        // Preserve the final rendered pose, not an intermediate simulation sample.
        for (Slot& slot : _slots)
            if (slot.live && slot.source.participatedCurrentFrame)
                slot.source.previousModelTransforms = slot.source.modelTransforms;
    }

    bool PoseSourceStore::SetConservativeBoundsExpansion(PoseSourceHandle handle, const vec3& expansion)
    {
        Slot* slot = GetSlot(handle);
        if (!slot || !std::isfinite(expansion.x) || !std::isfinite(expansion.y) || !std::isfinite(expansion.z) || expansion.x < 0.0f || expansion.y < 0.0f || expansion.z < 0.0f)
            return false;
        slot->source.conservativeBoundsExpansion = expansion;
        return true;
    }

    bool PoseSourceStore::SetDebugJointWeights(PoseSourceHandle handle, std::span<const f32> weights)
    {
        Slot* slot = GetSlot(handle);
        if (!slot || (!weights.empty() && weights.size() != slot->source.localPose.size()))
            return false;
        slot->source.debugJointWeights.assign(weights.begin(), weights.end());
        return true;
    }

    bool PoseSourceStore::SetDebugIKOperations(PoseSourceHandle handle, std::span<const PoseIKDebugOperation> operations)
    {
        Slot* slot = GetSlot(handle);
        if (!slot || operations.size() > slot->source.debugIKOperations.size())
            return false;
        slot->source.debugIKOperations = {};
        std::copy(operations.begin(), operations.end(), slot->source.debugIKOperations.begin());
        slot->source.debugIKOperationCount = static_cast<u32>(operations.size());
        return true;
    }

    PoseAttachmentHandle PoseSourceStore::CreateAttachment(PoseSourceHandle parent, const AnimationLoading::SkeletonAssetView& skeletonView, u64 semanticID, bool visualOnly, const vec3& childBoundsExtent)
    {
        if (!GetSlot(parent) || semanticID == 0 || glm::any(glm::lessThan(childBoundsExtent, vec3(0.0f))))
            return {};
        const auto attachment = std::find_if(skeletonView.attachments.begin(), skeletonView.attachments.end(), [semanticID](const FileFormat::Animation::SkeletonAttachment& value) { return value.semanticID == semanticID; });
        if (attachment == skeletonView.attachments.end() || attachment->jointIndex >= skeletonView.joints.size())
            return {};
        u32 slotIndex = 0;
        if (_freeAttachmentSlots.empty())
        {
            slotIndex = static_cast<u32>(_attachmentSlots.size());
            _attachmentSlots.emplace_back();
        }
        else
        {
            slotIndex = _freeAttachmentSlots.back();
            _freeAttachmentSlots.pop_back();
        }
        AttachmentSlot& slot = _attachmentSlots[slotIndex];
        slot.live = true;
        slot.localTransform = attachment->localTransform;
        const vec3 attachmentExpansion = glm::abs(attachment->localTransform.translation) + glm::abs(attachment->localTransform.scale) * childBoundsExtent;
        slot.attachment = {parent, semanticID, attachment->jointIndex, mat4a(1.0f), mat4a(1.0f), 0, 0, attachmentExpansion, visualOnly, false, false};
        RecomputeAttachmentBounds(parent);
        return PoseAttachmentHandle::Create(slotIndex, slot.generation);
    }

    bool PoseSourceStore::DestroyAttachment(PoseAttachmentHandle handle)
    {
        AttachmentSlot* slot = GetAttachmentSlot(handle);
        if (!slot)
            return false;
        const PoseSourceHandle parent = slot->attachment.parent;
        slot->live = false;
        slot->attachment = {};
        slot->localTransform = {};
        ++slot->generation;
        if (slot->generation == 0)
            slot->generation = 1;
        _freeAttachmentSlots.push_back(handle.GetSlot());
        RecomputeAttachmentBounds(parent);
        return true;
    }

    void PoseSourceStore::ResolveAttachments(u64 frame)
    {
        for (AttachmentSlot& attachmentSlot : _attachmentSlots)
        {
            if (!attachmentSlot.live)
                continue;
            PoseAttachment& attachment = attachmentSlot.attachment;
            const Slot* parentSlot = GetSlot(attachment.parent);
            if (!parentSlot || attachment.jointIndex >= parentSlot->source.modelTransforms.size())
            {
                attachment.currentTransform = mat4a(1.0f);
                attachment.previousTransform = mat4a(1.0f);
                attachment.parentPoseGeneration = 0;
                attachment.lastResolvedFrame = 0;
                attachment.valid = false;
                attachment.motionValid = false;
                continue;
            }
            if (attachment.visualOnly && !parentSlot->source.participatedCurrentFrame)
            {
                attachment.valid = false;
                attachment.motionValid = false;
                continue;
            }
            const mat4a local = ComposeLocalTransform(attachmentSlot.localTransform);
            attachment.currentTransform = Math::AffineMatrix::MatrixMul(parentSlot->source.modelTransforms[attachment.jointIndex], local);
            const bool consecutive = attachment.valid && (attachment.lastResolvedFrame + 1 == frame || (attachment.lastResolvedFrame == frame && attachment.motionValid));
            attachment.motionValid = consecutive && parentSlot->source.motionValid && attachment.jointIndex < parentSlot->source.previousModelTransforms.size();
            attachment.previousTransform = attachment.motionValid ? Math::AffineMatrix::MatrixMul(parentSlot->source.previousModelTransforms[attachment.jointIndex], local) : attachment.currentTransform;
            attachment.parentPoseGeneration = parentSlot->source.poseGeneration;
            attachment.lastResolvedFrame = frame;
            attachment.valid = true;
            ++_attachmentResolves;
        }
    }

    const PoseAttachment* PoseSourceStore::GetAttachment(PoseAttachmentHandle handle) const
    {
        const AttachmentSlot* slot = GetAttachmentSlot(handle);
        return slot ? &slot->attachment : nullptr;
    }

    void PoseSourceStore::BeginPoseQueryFrame(u64 frame)
    {
        if (_queryFrame == frame)
            return;
        _queryFrame = frame;
        _queries.clear();
    }

    PoseQueryHandle PoseSourceStore::RequestJoint(PoseSourceHandle poseSource, const AnimationLoading::SkeletonAssetView& skeletonView, u64 jointSemanticID)
    {
        if (_queryFrame == 0 || !GetSlot(poseSource) || jointSemanticID == 0)
            return {};
        for (u32 index = 0; index < _queries.size(); ++index)
        {
            const PoseQueryResult& existing = _queries[index].result;
            if (existing.poseSource == poseSource && existing.jointSemanticID == jointSemanticID)
            {
                ++_deduplicatedQueries;
                return {_queryFrame, index};
            }
        }
        const auto joint = std::find_if(skeletonView.joints.begin(), skeletonView.joints.end(), [jointSemanticID](const FileFormat::Animation::SkeletonJoint& value) { return value.semanticID == jointSemanticID; });
        if (joint == skeletonView.joints.end())
            return {};
        const u32 jointIndex = static_cast<u32>(std::distance(skeletonView.joints.begin(), joint));
        u32 closureCount = 0;
        i32 dependency = static_cast<i32>(jointIndex);
        while (dependency >= 0)
        {
            ++closureCount;
            dependency = skeletonView.joints[dependency].parentJointIndex;
        }
        _sparseQueryJoints += closureCount;
        if (closureCount * 2 >= skeletonView.joints.size())
            ++_fullPoseQueryPromotions;
        MarkDemand(poseSource, _queryFrame);
        PendingQuery query;
        query.result.poseSource = poseSource;
        query.result.jointSemanticID = jointSemanticID;
        query.result.jointIndex = jointIndex;
        _queries.push_back(query);
        return {_queryFrame, static_cast<u32>(_queries.size() - 1)};
    }

    void PoseSourceStore::ResolvePoseQueries()
    {
        for (PendingQuery& query : _queries)
        {
            const Slot* slot = GetSlot(query.result.poseSource);
            if (!slot || query.result.jointIndex >= slot->source.modelTransforms.size())
                continue;
            query.result.modelTransform = slot->source.modelTransforms[query.result.jointIndex];
            query.result.valid = true;
        }
    }

    const PoseQueryResult* PoseSourceStore::GetPoseQuery(PoseQueryHandle handle) const
    {
        return handle.frame == _queryFrame && handle.index < _queries.size() ? &_queries[handle.index].result : nullptr;
    }

    const PoseSource* PoseSourceStore::Get(PoseSourceHandle handle) const
    {
        const Slot* slot = GetSlot(handle);
        return slot ? &slot->source : nullptr;
    }

    void PoseSourceStore::RecomputeAttachmentBounds(PoseSourceHandle parent)
    {
        Slot* parentSlot = GetSlot(parent);
        if (!parentSlot)
            return;
        parentSlot->source.attachmentBoundsExpansion = {};
        for (const AttachmentSlot& attachmentSlot : _attachmentSlots)
        {
            if (attachmentSlot.live && attachmentSlot.attachment.parent == parent)
                parentSlot->source.attachmentBoundsExpansion = glm::max(parentSlot->source.attachmentBoundsExpansion, attachmentSlot.attachment.conservativeBoundsExpansion);
        }
    }

    PoseSourceStoreStats PoseSourceStore::GetStats() const
    {
        return {.liveSources = static_cast<u32>(_slots.size() - _freeSlots.size()), .freeSlots = static_cast<u32>(_freeSlots.size()), .staleHandleRejects = _staleHandleRejects, .poseEvaluations = _poseEvaluations, .invalidations = _invalidations, .liveAttachments = static_cast<u32>(_attachmentSlots.size() - _freeAttachmentSlots.size()), .attachmentResolves = _attachmentResolves, .sparseQueryJoints = _sparseQueryJoints, .deduplicatedQueries = _deduplicatedQueries, .fullPoseQueryPromotions = _fullPoseQueryPromotions};
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

    PoseSourceStore::AttachmentSlot* PoseSourceStore::GetAttachmentSlot(PoseAttachmentHandle handle)
    {
        if (!handle || handle.GetSlot() >= _attachmentSlots.size())
            return nullptr;
        AttachmentSlot& slot = _attachmentSlots[handle.GetSlot()];
        return slot.live && slot.generation == handle.GetGeneration() ? &slot : nullptr;
    }

    const PoseSourceStore::AttachmentSlot* PoseSourceStore::GetAttachmentSlot(PoseAttachmentHandle handle) const
    {
        if (!handle || handle.GetSlot() >= _attachmentSlots.size())
            return nullptr;
        const AttachmentSlot& slot = _attachmentSlots[handle.GetSlot()];
        return slot.live && slot.generation == handle.GetGeneration() ? &slot : nullptr;
    }
} // namespace Animation
