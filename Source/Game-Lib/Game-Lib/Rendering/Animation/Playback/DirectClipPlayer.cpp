#include "DirectClipPlayer.h"

#include "Game-Lib/Rendering/Animation/Asset/AnimationAssetValidator.h"

#include <chrono>
#include <cmath>

namespace Animation
{
    DirectPlaybackHandle DirectClipPlayer::Create(AnimationLoading::SkeletonHandle skeleton, AnimationLoading::AnimationClipHandle clip, const AnimationLoading::SkeletonAssetView& skeletonView, const AnimationLoading::AnimationClipAssetView& clipView)
    {
        if (AnimationLoading::AnimationAssetValidator::ValidateDomain(clipView, FileFormat::Animation::PoseDomainType::Skeleton, _assets->GetAssetID(skeleton)))
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
        return true;
    }

    bool DirectClipPlayer::Seek(DirectPlaybackHandle handle, f64 timeSeconds)
    {
        Slot* slot = GetSlot(handle);
        if (!slot || !std::isfinite(timeSeconds))
            return false;
        slot->controller.timeSeconds = MapControllerTime(slot->controller, timeSeconds);
        slot->controller.sampleDirty = true;
        return true;
    }

    bool DirectClipPlayer::SetLooping(DirectPlaybackHandle handle, bool looping)
    {
        Slot* slot = GetSlot(handle);
        if (!slot)
            return false;
        slot->controller.looping = looping;
        slot->controller.timeSeconds = MapControllerTime(slot->controller, slot->controller.timeSeconds);
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
        const AnimationLoading::AnimationClipAssetView* clip = _assets->Get(slot->controller.clip);
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

    void DirectClipPlayer::Advance(f32 deltaTime)
    {
        if (!std::isfinite(deltaTime))
            return;
        for (Slot& slot : _slots)
        {
            if (!slot.live || !slot.controller.playing)
                continue;
            slot.controller.timeSeconds = MapControllerTime(slot.controller, slot.controller.timeSeconds + static_cast<f64>(deltaTime) * slot.controller.playbackRate);
            slot.controller.sampleDirty = true;
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
        if (!slot.controller.sampleDirty)
            return true;
        const AnimationLoading::SkeletonAssetView* skeleton = _assets->Get(slot.controller.skeleton);
        const AnimationLoading::AnimationClipAssetView* clip = _assets->Get(slot.controller.clip);
        if (!skeleton || !clip)
            return false;
        const auto start = std::chrono::steady_clock::now();
        const ClipSampleError sample = SampleClipLocalPose(*clip, *skeleton, slot.controller.timeSeconds, slot.controller.looping, slot.controller.localPose);
        if (sample != ClipSampleError::None || !_poseSources->SetLocalPose(slot.controller.poseSource, slot.controller.localPose, *skeleton))
            return false;
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
        const AnimationLoading::AnimationClipAssetView* clip = _assets->Get(controller.clip);
        if (!clip)
            return 0.0;
        return MapClipTime(clip->root, timeSeconds, controller.looping).mappedTimeSeconds;
    }
}
