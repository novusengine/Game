#include "DirectClipPlayer.h"
#include "Game-Lib/Rendering/Animation/Asset/AnimationAssetDocument.h"

#include "Game-Lib/Rendering/Animation/Asset/AnimationAssetValidator.h"

#include <chrono>
#include <cmath>

namespace Animation
{
    DirectPlaybackHandle DirectClipPlayer::Create(AnimationLoading::SkeletonHandle skeleton, AnimationLoading::AnimationClipHandle clip, const AnimationLoading::SkeletonAssetView& skeletonView, const AnimationLoading::AnimationClipAssetView& clipView)
    {
        if (AnimationLoading::AnimationAssetValidator::ValidateDomain(clipView, FileFormat::Animation::PoseDomainType::Skeleton, _assets->GetRetainedAssetID(skeleton)))
            return {};
        const PoseSourceHandle poseSource = _poseSources->CreateRestPose(skeleton, skeletonView);
        if (!poseSource)
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
        slot.controller = {};
        slot.controller.skeleton = skeleton;
        slot.controller.clip = clip;
        slot.controller.poseSource = poseSource;
        slot.controller.looping = (clipView.root.flags & FileFormat::Animation::AnimationClipFlags_Looping) != 0;
        slot.controller.localPose.resize(skeletonView.joints.size());
        _poseSources->SetBoundsMetadata(poseSource, _assets->GetRetainedAssetID(clip), 0, true);
        _poseToController[poseSource.GetRawValue()] = slotIndex;
        return DirectPlaybackHandle::Create(slotIndex, slot.generation);
    }

    bool DirectClipPlayer::Destroy(DirectPlaybackHandle handle)
    {
        Slot* slot = GetSlot(handle);
        if (!slot)
            return false;
        _poseToController.erase(slot->controller.poseSource.GetRawValue());
        _poseSources->Destroy(slot->controller.poseSource);
        _assets->Release(slot->controller.clip);
        _assets->Release(slot->controller.skeleton);
        slot->controller = {};
        slot->live = false;
        ++slot->generation;
        if (slot->generation == 0)
            slot->generation = 1;
        _freeSlots.push_back(handle.GetSlot());
        return true;
    }

    bool DirectClipPlayer::Play(DirectPlaybackHandle handle)
    {
        Slot* slot = GetSlot(handle);
        if (!slot)
            return false;
        slot->controller.playing = true;
        return true;
    }

    bool DirectClipPlayer::Pause(DirectPlaybackHandle handle)
    {
        Slot* slot = GetSlot(handle);
        if (!slot)
            return false;
        slot->controller.playing = false;
        return true;
    }

    bool DirectClipPlayer::Stop(DirectPlaybackHandle handle)
    {
        Slot* slot = GetSlot(handle);
        if (!slot)
            return false;
        slot->controller.playing = false;
        slot->controller.timeSeconds = 0.0;
        slot->controller.sampleDirty = true;
        _poseSources->Invalidate(slot->controller.poseSource);
        _poseSources->SetBoundsMetadata(slot->controller.poseSource, _assets->GetRetainedAssetID(slot->controller.clip), 0, true);
        return true;
    }

    bool DirectClipPlayer::Seek(DirectPlaybackHandle handle, f64 timeSeconds)
    {
        Slot* slot = GetSlot(handle);
        if (!slot || !std::isfinite(timeSeconds))
            return false;
        slot->controller.timeSeconds = MapControllerTime(slot->controller, timeSeconds);
        slot->controller.sampleDirty = true;
        _poseSources->Invalidate(slot->controller.poseSource);
        _poseSources->SetBoundsMetadata(slot->controller.poseSource, _assets->GetRetainedAssetID(slot->controller.clip), static_cast<u64>(std::max(slot->controller.timeSeconds, 0.0) * 1'000'000.0 + 0.5), true);
        return true;
    }

    bool DirectClipPlayer::SetLooping(DirectPlaybackHandle handle, bool looping)
    {
        Slot* slot = GetSlot(handle);
        if (!slot)
            return false;
        slot->controller.looping = looping;
        const f64 previousTime = slot->controller.timeSeconds;
        slot->controller.timeSeconds = MapControllerTime(slot->controller, slot->controller.timeSeconds);
        if (previousTime != slot->controller.timeSeconds)
            _poseSources->Invalidate(slot->controller.poseSource);
        slot->controller.sampleDirty = true;
        return true;
    }

    bool DirectClipPlayer::SetPlaybackRate(DirectPlaybackHandle handle, f32 playbackRate)
    {
        Slot* slot = GetSlot(handle);
        if (!slot || !std::isfinite(playbackRate))
            return false;
        slot->controller.playbackRate = playbackRate;
        return true;
    }

    bool DirectClipPlayer::GetState(DirectPlaybackHandle handle, DirectPlaybackState& state) const
    {
        const Slot* slot = GetSlot(handle);
        if (!slot)
            return false;
        const AnimationLoading::AnimationClipAssetView* clip = _assets->GetRetained(slot->controller.clip);
        if (!clip)
            return false;
        state.timeSeconds = slot->controller.timeSeconds;
        state.durationSeconds = static_cast<f64>(clip->root.durationMicroseconds) / 1'000'000.0;
        state.playbackRate = slot->controller.playbackRate;
        state.playing = slot->controller.playing;
        state.looping = slot->controller.looping;
        return true;
    }

    PoseSourceHandle DirectClipPlayer::GetPoseSource(DirectPlaybackHandle handle) const
    {
        const Slot* slot = GetSlot(handle);
        return slot ? slot->controller.poseSource : PoseSourceHandle{};
    }

    bool DirectClipPlayer::ValidatePublication(AnimationDocumentType type, FileFormat::AssetID assetID, std::span<const u8> payload, std::string& error) const
    {
        for (const auto& slot : _slots)
        {
            if (!slot.live) continue;
            const auto& controller = slot.controller;
            if (type == AnimationDocumentType::Clip && _assets->GetRetainedAssetID(controller.clip) == assetID)
            {
                const auto clip = AnimationLoading::AnimationAssetReader::ReadAnimationClip(payload);
                const auto* skeleton = _assets->GetRetained(controller.skeleton);
                if (!clip || !skeleton || AnimationLoading::AnimationAssetValidator::ValidateDomain(clip.view, FileFormat::Animation::PoseDomainType::Skeleton, _assets->GetRetainedAssetID(controller.skeleton))) { error = "Clip domain is incompatible with direct playback"; return false; }
                for (const auto& track : clip.view.tracks)
                    if (track.sourceJointIndex >= skeleton->joints.size()) { error = "Clip track exceeds the bound skeleton"; return false; }
            }
            if (type == AnimationDocumentType::Skeleton && _assets->GetRetainedAssetID(controller.skeleton) == assetID)
            {
                const auto skeleton = AnimationLoading::AnimationAssetReader::ReadSkeleton(payload);
                const auto* previous = _assets->GetRetained(controller.skeleton);
                if (!skeleton || !previous || skeleton.view.joints.size() != previous->joints.size()) { error = "Rebind the model before changing live skeleton topology"; return false; }
                for (u32 i = 0; i < previous->joints.size(); ++i)
                    if (skeleton.view.joints[i].semanticID != previous->joints[i].semanticID || skeleton.view.joints[i].parentJointIndex != previous->joints[i].parentJointIndex) { error = "Rebind the model before changing live skeleton topology"; return false; }
            }
        }
        return true;
    }

    void DirectClipPlayer::Advance(f32 deltaTime)
    {
        if (!std::isfinite(deltaTime))
            return;
        const bool assetsChanged = _assetRevision != _assets->GetPublicationRevision();
        _assetRevision = _assets->GetPublicationRevision();
        for (Slot& slot : _slots)
        {
            if (!slot.live)
                continue;
            if (assetsChanged)
            {
                auto& controller = slot.controller;
                const auto skeleton = _assets->LoadSkeleton(_assets->GetRetainedAssetID(controller.skeleton));
                const auto clip = _assets->LoadAnimationClip(_assets->GetRetainedAssetID(controller.clip));
                const auto* view = _assets->Get(skeleton);
                const auto* clipView = _assets->Get(clip);
                if (view && clipView && view->joints.size() == controller.localPose.size() && !AnimationLoading::AnimationAssetValidator::ValidateDomain(*clipView, FileFormat::Animation::PoseDomainType::Skeleton, _assets->GetAssetID(skeleton)))
                {
                    const bool changed = skeleton != controller.skeleton || clip != controller.clip;
                    _assets->Release(controller.skeleton); _assets->Release(controller.clip);
                    controller.skeleton = skeleton; controller.clip = clip;
                    if (changed) { controller.sampleDirty = true; _poseSources->RebindSkeleton(controller.poseSource, skeleton); controller.timeSeconds = MapControllerTime(controller, controller.timeSeconds); }
                }
                else { _assets->Release(skeleton); _assets->Release(clip); }
            }
            if (!slot.controller.playing) continue;
            slot.controller.timeSeconds = MapControllerTime(slot.controller, slot.controller.timeSeconds + static_cast<f64>(deltaTime) * slot.controller.playbackRate);
            slot.controller.sampleDirty = true;
            _poseSources->SetBoundsMetadata(slot.controller.poseSource, _assets->GetRetainedAssetID(slot.controller.clip), static_cast<u64>(std::max(slot.controller.timeSeconds, 0.0) * 1'000'000.0 + 0.5), true);
            ++_stats.controllerAdvances;
        }
    }

    bool DirectClipPlayer::RequestPose(PoseSourceHandle poseSource)
    {
        const auto found = _poseToController.find(poseSource.GetRawValue());
        if (found == _poseToController.end() || found->second >= _slots.size())
            return false;
        Slot& slot = _slots[found->second];
        if (!slot.live)
            return false;
        ++_stats.poseRequests;
        const AnimationLoading::SkeletonAssetView* skeleton = _assets->GetRetained(slot.controller.skeleton);
        const AnimationLoading::AnimationClipAssetView* clip = _assets->GetRetained(slot.controller.clip);
        if (!skeleton || !clip)
        {
            _poseSources->Invalidate(poseSource);
            return false;
        }
        if (!slot.controller.sampleDirty)
            return true;
        const auto start = std::chrono::steady_clock::now();
        const ClipSampleError sample = SampleClipLocalPose(*clip, *skeleton, slot.controller.timeSeconds, slot.controller.looping, slot.controller.localPose);
        const u64 sampleTimeMicroseconds = static_cast<u64>(std::max(slot.controller.timeSeconds, 0.0) * 1'000'000.0 + 0.5);
        if (sample != ClipSampleError::None || !_poseSources->SetLocalPose(slot.controller.poseSource, slot.controller.localPose, *skeleton, sampleTimeMicroseconds))
        {
            _poseSources->Invalidate(poseSource);
            return false;
        }
        _stats.evaluationMicroseconds += std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
        ++_stats.poseEvaluations;
        slot.controller.sampleDirty = false;
        return true;
    }

    DirectClipPlayer::Slot* DirectClipPlayer::GetSlot(DirectPlaybackHandle handle)
    {
        if (!handle || handle.GetSlot() >= _slots.size())
            return nullptr;
        Slot& slot = _slots[handle.GetSlot()];
        return slot.live && slot.generation == handle.GetGeneration() ? &slot : nullptr;
    }

    const DirectClipPlayer::Slot* DirectClipPlayer::GetSlot(DirectPlaybackHandle handle) const
    {
        if (!handle || handle.GetSlot() >= _slots.size())
            return nullptr;
        const Slot& slot = _slots[handle.GetSlot()];
        return slot.live && slot.generation == handle.GetGeneration() ? &slot : nullptr;
    }

    f64 DirectClipPlayer::MapControllerTime(const Controller& controller, f64 timeSeconds) const
    {
        const AnimationLoading::AnimationClipAssetView* clip = _assets->GetRetained(controller.clip);
        if (!clip)
            return 0.0;
        return MapClipTime(clip->root, timeSeconds, controller.looping).mappedTimeSeconds;
    }
}
