#pragma once

#include "Game-Lib/Rendering/Animation/Asset/AnimationAssetReader.h"

#include <Base/Types.h>

#include <span>

namespace Animation
{
    struct ClipSampleLocation
    {
        u32 firstSample = 0;
        u32 secondSample = 0;
        f32 alpha = 0.0f;
        f64 mappedTimeSeconds = 0.0;
    };

    enum class ClipSampleError : u8
    {
        None,
        InvalidTiming,
        PoseSizeMismatch,
        InvalidTrackJoint,
        InvalidTrackRange
    };

    ClipSampleLocation MapClipTime(const FileFormat::Animation::AnimationClipAsset& clip, f64 timeSeconds, bool looping);
    ClipSampleError SampleClipLocalPose(const AnimationLoading::AnimationClipAssetView& clip, const AnimationLoading::SkeletonAssetView& skeleton, f64 timeSeconds, bool looping, std::span<FileFormat::Animation::LocalTransform> outLocalPose);
}
