#include "ControllerPosePlayer.h"

#include "Game-Lib/Rendering/Animation/Asset/AnimationAssetValidator.h"
#include "Game-Lib/Rendering/Animation/Pose/AnimationPose.h"

#include <Base/Util/StringUtils.h>

#include <algorithm>
#include <cstring>

namespace Animation
{
    ControllerPlaybackHandle ControllerPosePlayer::Create(AnimationLoading::SkeletonHandle skeleton, AnimationLoading::AnimationSetHandle animationSet, const AnimationLoading::SkeletonAssetView& skeletonView, const AnimationLoading::AnimationSetAssetView& setView, std::shared_ptr<const CompiledAnimationGraph> graph, u64 stableSeed)
    {
        if (!skeleton || !animationSet || !graph || setView.root.declaredPoseDomainType != FileFormat::Animation::PoseDomainType::Skeleton || setView.root.declaredPoseDomainAssetID != _assets->GetAssetID(skeleton))
            return {};
        const PoseSourceHandle poseSource = _poseSources->CreateRestPose(skeleton, skeletonView);
        if (!poseSource)
            return {};
        std::vector<ClipRecord> clips;
        std::vector<AnimationClipDomainRecord> domains;
        for (const FileFormat::Animation::AnimationClipVariant& variant : setView.variants)
        {
            if (std::any_of(clips.begin(), clips.end(), [&variant](const ClipRecord& clip) { return clip.assetID == variant.animationClipAssetID; }))
                continue;
            const AnimationLoading::AnimationClipHandle handle = _assets->LoadAnimationClip(variant.animationClipAssetID);
            AssetLoading::Diagnostic diagnostic;
            const AnimationLoading::AnimationClipAssetView* view = _assets->Get(handle, &diagnostic);
            if (!view || AnimationLoading::AnimationAssetValidator::ValidateDomain(*view, FileFormat::Animation::PoseDomainType::Skeleton, setView.root.declaredPoseDomainAssetID))
            {
                _assets->Release(handle);
                for (const ClipRecord& clip : clips)
                    _assets->Release(clip.handle);
                _poseSources->Destroy(poseSource);
                return {};
            }
            clips.push_back({variant.animationClipAssetID, handle});
            domains.push_back({variant.animationClipAssetID, view->root.sourcePoseDomainType, view->root.sourcePoseDomainAssetID, view->root.durationMicroseconds, view->synchronizationMarkers});
        }
        const AnimatorHandle animator = _controllers.Create({_assets->GetAssetID(skeleton), FileFormat::INVALID_ASSET_ID, std::move(graph), stableSeed});
        if (!animator)
        {
            for (const ClipRecord& clip : clips)
                _assets->Release(clip.handle);
            _poseSources->Destroy(poseSource);
            return {};
        }
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
        slot.skeleton = skeleton;
        slot.animationSet = animationSet;
        slot.animator = animator;
        slot.poseSource = poseSource;
        slot.clips = std::move(clips);
        slot.clipDomains = std::move(domains);
        slot.localPose.reserve(skeletonView.joints.size());
        for (const FileFormat::Animation::SkeletonJoint& joint : skeletonView.joints)
            slot.localPose.push_back(joint.restTransform);
        slot.samplePose.resize(skeletonView.joints.size());
        slot.lastRecipeHash = 0;
        slot.live = true;
        _poseToController[poseSource.GetRawValue()] = slotIndex;
        return ControllerPlaybackHandle::Create(slotIndex, slot.generation);
    }

    bool ControllerPosePlayer::Destroy(ControllerPlaybackHandle handle)
    {
        Slot* slot = GetSlot(handle);
        if (!slot)
            return false;
        _poseToController.erase(slot->poseSource.GetRawValue());
        _controllers.Destroy(slot->animator);
        _poseSources->Destroy(slot->poseSource);
        for (const ClipRecord& clip : slot->clips)
            _assets->Release(clip.handle);
        _assets->Release(slot->animationSet);
        _assets->Release(slot->skeleton);
        const u32 slotIndex = handle.GetSlot();
        *slot = {};
        slot->generation = handle.GetGeneration() + 1u;
        if (slot->generation == 0)
            slot->generation = 1;
        _freeSlots.push_back(slotIndex);
        return true;
    }

    void ControllerPosePlayer::Advance(f64 deltaSeconds)
    {
        _controllers.Advance(deltaSeconds);
        for (Slot& slot : _slots)
        {
            if (!slot.live)
                continue;
            const AnimationLoading::AnimationSetAssetView* set = _assets->Get(slot.animationSet);
            if (!set)
                continue;
            FileFormat::AssetID clipAssetID = FileFormat::INVALID_ASSET_ID;
            u64 timeMicroseconds = 0;
            bool supportsTightBounds = false;
            if (_controllers.GetBoundsMetadata(slot.animator, *set, slot.clipDomains, clipAssetID, timeMicroseconds, supportsTightBounds))
                _poseSources->SetBoundsMetadata(slot.poseSource, clipAssetID, timeMicroseconds, supportsTightBounds);
        }
        ++_stats.advances;
    }

    bool ControllerPosePlayer::RequestPose(PoseSourceHandle poseSource)
    {
        const auto found = _poseToController.find(poseSource.GetRawValue());
        if (found == _poseToController.end() || found->second >= _slots.size() || !_slots[found->second].live)
            return false;
        Slot& slot = _slots[found->second];
        ++_stats.poseRequests;
        const AnimationLoading::SkeletonAssetView* skeleton = _assets->Get(slot.skeleton);
        const AnimationLoading::AnimationSetAssetView* animationSet = _assets->Get(slot.animationSet);
        if (!skeleton || !animationSet)
        {
            ++_stats.failures;
            return false;
        }
        const PoseRecipe recipe = _controllers.RequestPose(slot.animator, *animationSet, slot.clipDomains, slot.localPose);
        u64 recipeHash = recipe.sampleCount;
        for (u32 sampleIndex = 0; sampleIndex < recipe.sampleCount; ++sampleIndex)
        {
            const PoseRecipeSample& sample = recipe.samples[sampleIndex];
            recipeHash = XXHash64::hash(&sample.clipAssetID, sizeof(sample.clipAssetID), recipeHash);
            recipeHash = XXHash64::hash(&sample.timeMicroseconds, sizeof(sample.timeMicroseconds), recipeHash);
            recipeHash = XXHash64::hash(&sample.weight, sizeof(sample.weight), recipeHash);
            recipeHash = XXHash64::hash(&sample.stableVariationID, sizeof(sample.stableVariationID), recipeHash);
        }
        recipeHash = XXHash64::hash(&recipe.transitionWeight, sizeof(recipe.transitionWeight), recipeHash);
        recipeHash = XXHash64::hash(&recipe.usesSnapshot, sizeof(recipe.usesSnapshot), recipeHash);
        if (recipeHash == slot.lastRecipeHash)
            return true;

        std::vector<FileFormat::Animation::LocalTransform> composed = slot.localPose;
        bool hasComposedPose = recipe.usesSnapshot;
        f32 accumulatedWeight = recipe.usesSnapshot ? std::max(0.0f, 1.0f - recipe.transitionWeight) : 0.0f;
        for (u32 sampleIndex = 0; sampleIndex < recipe.sampleCount; ++sampleIndex)
        {
            const PoseRecipeSample& sample = recipe.samples[sampleIndex];
            const AnimationLoading::AnimationClipAssetView* clip = FindClip(slot, sample.clipAssetID);
            if (!clip || SampleClipLocalPose(*clip, *skeleton, static_cast<f64>(sample.timeMicroseconds) / 1'000'000.0, (clip->root.flags & FileFormat::Animation::AnimationClipFlags_Looping) != 0, slot.samplePose) != ClipSampleError::None)
            {
                ++_stats.failures;
                return false;
            }
            if (!hasComposedPose)
            {
                composed = slot.samplePose;
                accumulatedWeight = sample.weight;
                hasComposedPose = true;
            }
            else
            {
                const f32 denominator = accumulatedWeight + sample.weight;
                const f32 blendWeight = denominator > 0.0f ? sample.weight / denominator : 0.0f;
                if (!BlendLocalPoses(composed, slot.samplePose, blendWeight, composed))
                    return false;
                accumulatedWeight = denominator;
                ++_stats.blendedSamples;
            }
        }
        if (!hasComposedPose)
            return true;
        if (recipe.usesSnapshot)
            ++_stats.snapshotBlends;
        slot.localPose = std::move(composed);
        slot.lastRecipeHash = recipeHash;
        const u64 sampleTime = recipe.sampleCount ? recipe.samples[recipe.sampleCount - 1].timeMicroseconds : 0;
        if (!_poseSources->SetLocalPose(slot.poseSource, slot.localPose, *skeleton, sampleTime))
        {
            ++_stats.failures;
            return false;
        }
        ++_stats.poseEvaluations;
        return true;
    }

    PoseSourceHandle ControllerPosePlayer::GetPoseSource(ControllerPlaybackHandle handle) const
    {
        const Slot* slot = GetSlot(handle);
        return slot ? slot->poseSource : PoseSourceHandle{};
    }

    AnimationParameterHandle ControllerPosePlayer::ResolveParameter(ControllerPlaybackHandle handle, u64 nameHash) const
    {
        const Slot* slot = GetSlot(handle);
        return slot ? _controllers.ResolveParameter(slot->animator, nameHash) : AnimationParameterHandle{};
    }

    bool ControllerPosePlayer::QueueAction(ControllerPlaybackHandle handle, u64 semanticID, u32 payload)
    {
        Slot* slot = GetSlot(handle);
        return slot && _controllers.QueueAction(slot->animator, semanticID, payload);
    }

    bool ControllerPosePlayer::GetState(ControllerPlaybackHandle handle, AnimationControllerState& state) const
    {
        const Slot* slot = GetSlot(handle);
        return slot && _controllers.GetState(slot->animator, state);
    }

    ControllerPosePlayer::Slot* ControllerPosePlayer::GetSlot(ControllerPlaybackHandle handle)
    {
        if (!handle || handle.GetSlot() >= _slots.size())
            return nullptr;
        Slot& slot = _slots[handle.GetSlot()];
        return slot.live && slot.generation == handle.GetGeneration() ? &slot : nullptr;
    }

    const ControllerPosePlayer::Slot* ControllerPosePlayer::GetSlot(ControllerPlaybackHandle handle) const
    {
        if (!handle || handle.GetSlot() >= _slots.size())
            return nullptr;
        const Slot& slot = _slots[handle.GetSlot()];
        return slot.live && slot.generation == handle.GetGeneration() ? &slot : nullptr;
    }

    const AnimationLoading::AnimationClipAssetView* ControllerPosePlayer::FindClip(const Slot& slot, FileFormat::AssetID assetID) const
    {
        const auto found = std::find_if(slot.clips.begin(), slot.clips.end(), [assetID](const ClipRecord& clip) { return clip.assetID == assetID; });
        return found != slot.clips.end() ? _assets->Get(found->handle) : nullptr;
    }
}
