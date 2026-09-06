#include "AnimationClipSampler.h"

#include <algorithm>
#include <cmath>

namespace Animation
{
    ClipSampleLocation MapClipTime(const FileFormat::Animation::AnimationClipAsset& clip, f64 timeSeconds, bool looping)
    {
        ClipSampleLocation location;
        if (clip.sampleCount < 2 || clip.sampleRateHz == 0 || clip.durationMicroseconds == 0 || !std::isfinite(timeSeconds))
            return location;

        const f64 duration = static_cast<f64>(clip.durationMicroseconds) / 1'000'000.0;
        f64 mapped = timeSeconds;
        if (looping)
        {
            mapped = std::fmod(mapped, duration);
            if (mapped < 0.0)
                mapped += duration;
        }
        else
        {
            mapped = std::clamp(mapped, 0.0, duration);
        }
        location.mappedTimeSeconds = mapped;

        if (!looping && mapped >= duration)
        {
            location.firstSample = clip.sampleCount - 1;
            location.secondSample = location.firstSample;
            return location;
        }

        const f64 samplePosition = std::min(mapped * static_cast<f64>(clip.sampleCount - 1) / duration, static_cast<f64>(clip.sampleCount - 1));
        location.firstSample = std::min(static_cast<u32>(samplePosition), clip.sampleCount - 1);
        location.secondSample = std::min(location.firstSample + 1, clip.sampleCount - 1);
        location.alpha = static_cast<f32>(samplePosition - static_cast<f64>(location.firstSample));
        return location;
    }

    ClipSampleError SampleClipLocalPose(const AnimationLoading::AnimationClipAssetView& clip, const AnimationLoading::SkeletonAssetView& skeleton, f64 timeSeconds, bool looping, std::span<FileFormat::Animation::LocalTransform> outLocalPose)
    {
        if (clip.root.sampleCount < 2 || clip.root.sampleRateHz == 0 || clip.root.durationMicroseconds == 0)
            return ClipSampleError::InvalidTiming;
        if (outLocalPose.size() != skeleton.joints.size())
            return ClipSampleError::PoseSizeMismatch;

        for (u32 jointIndex = 0; jointIndex < skeleton.joints.size(); ++jointIndex)
            outLocalPose[jointIndex] = (clip.root.flags & FileFormat::Animation::AnimationClipFlags_Additive) != 0 ? FileFormat::Animation::LocalTransform{} : skeleton.joints[jointIndex].restTransform;

        const ClipSampleLocation location = MapClipTime(clip.root, timeSeconds, looping);
        for (const FileFormat::Animation::AnimationTrack& track : clip.tracks)
        {
            if (track.sourceJointIndex >= outLocalPose.size())
                return ClipSampleError::InvalidTrackJoint;
            FileFormat::Animation::LocalTransform& local = outLocalPose[track.sourceJointIndex];
            local.translation = track.defaultTranslation;
            local.rotation = track.defaultRotation;
            local.scale = track.defaultScale;

            if ((track.flags & FileFormat::Animation::AnimationTrackFlags_HasTranslationSamples) != 0)
            {
                if (track.numTranslations != clip.root.sampleCount || track.translationOffset > clip.translationSamples.size() || track.numTranslations > clip.translationSamples.size() - track.translationOffset)
                    return ClipSampleError::InvalidTrackRange;
                local.translation = glm::mix(clip.translationSamples[track.translationOffset + location.firstSample], clip.translationSamples[track.translationOffset + location.secondSample], location.alpha);
            }
            if ((track.flags & FileFormat::Animation::AnimationTrackFlags_HasRotationSamples) != 0)
            {
                if (track.numRotations != clip.root.sampleCount || track.rotationOffset > clip.rotationSamples.size() || track.numRotations > clip.rotationSamples.size() - track.rotationOffset)
                    return ClipSampleError::InvalidTrackRange;
                quat first = clip.rotationSamples[track.rotationOffset + location.firstSample];
                quat second = clip.rotationSamples[track.rotationOffset + location.secondSample];
                if (glm::dot(first, second) < 0.0f)
                    second = -second;
                local.rotation = glm::normalize(glm::slerp(first, second, location.alpha));
            }
            if ((track.flags & FileFormat::Animation::AnimationTrackFlags_HasScaleSamples) != 0)
            {
                if (track.numScales != clip.root.sampleCount || track.scaleOffset > clip.scaleSamples.size() || track.numScales > clip.scaleSamples.size() - track.scaleOffset)
                    return ClipSampleError::InvalidTrackRange;
                local.scale = glm::mix(clip.scaleSamples[track.scaleOffset + location.firstSample], clip.scaleSamples[track.scaleOffset + location.secondSample], location.alpha);
            }
        }
        return ClipSampleError::None;
    }
}
