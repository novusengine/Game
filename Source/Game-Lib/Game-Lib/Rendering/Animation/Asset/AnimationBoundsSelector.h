#pragma once
#include "Game-Lib/Rendering/Animation/Asset/AnimationAssetReader.h"

#include <limits>

namespace AnimationLoading
{
    enum class AnimationBoundsSelection : u8
    {
        Segment,
        ClipFallback,
        StaticFallbackMissingAsset,
        StaticFallbackIncompatibleBinding,
        StaticFallbackMissingClip
    };

    struct SelectedAnimationBounds
    {
        FileFormat::Animation::Bounds bounds;
        AnimationBoundsSelection selection = AnimationBoundsSelection::StaticFallbackMissingAsset;
        u32 clipIndex = std::numeric_limits<u32>::max();
        u32 segmentIndex = std::numeric_limits<u32>::max();
    };

    // Selects tight clip bounds when available and a conservative fallback otherwise.
    // Binding checks prevent bounds authored for another model or skeleton from being used.
    inline SelectedAnimationBounds SelectAnimationBounds(const AnimationBoundsAssetView* asset, FileFormat::AssetID modelAssetID, FileFormat::AssetID skeletonAssetID, FileFormat::AssetID clipAssetID, u64 timeMicroseconds, bool supportsTightBounds, const FileFormat::Animation::Bounds& staticFallback)
    {
        if (!asset)
            return {staticFallback, AnimationBoundsSelection::StaticFallbackMissingAsset};
        if (asset->root.modelAssetID != modelAssetID || asset->root.skeletonAssetID != skeletonAssetID)
            return {staticFallback, AnimationBoundsSelection::StaticFallbackIncompatibleBinding};
        const bool hasGlobalFallback = (asset->root.flags & FileFormat::Animation::AnimationBoundsAssetFlags_HasGlobalFallback) != 0 && !asset->clips.empty() && asset->clips[0].animationClipAssetID == FileFormat::INVALID_ASSET_ID;
        const FileFormat::Animation::Bounds conservativeFallback = hasGlobalFallback ? asset->clips[0].fallbackBounds : staticFallback;
        for (u32 clipIndex = 0; clipIndex < asset->clips.size(); ++clipIndex)
        {
            const FileFormat::Animation::ClipBounds& clip = asset->clips[clipIndex];
            if (clip.animationClipAssetID != clipAssetID)
                continue;
            if (!supportsTightBounds || clip.numSegments == 0)
                return {hasGlobalFallback ? conservativeFallback : clip.fallbackBounds, AnimationBoundsSelection::ClipFallback, clipIndex};
            const FileFormat::Animation::AnimatedBoundsSegment& last = asset->segments[clip.segmentOffset + clip.numSegments - 1u];
            const u64 duration = last.endTimeMicroseconds;
            const u32 mappedTime = duration == 0 ? 0 : static_cast<u32>(timeMicroseconds % duration);
            for (u32 local = 0; local < clip.numSegments; ++local)
            {
                const u32 segmentIndex = clip.segmentOffset + local;
                const FileFormat::Animation::AnimatedBoundsSegment& segment = asset->segments[segmentIndex];
                if (mappedTime >= segment.startTimeMicroseconds && mappedTime < segment.endTimeMicroseconds)
                    return {segment.bounds, AnimationBoundsSelection::Segment, clipIndex, segmentIndex};
            }
            return {hasGlobalFallback ? conservativeFallback : clip.fallbackBounds, AnimationBoundsSelection::ClipFallback, clipIndex};
        }
        return {conservativeFallback, hasGlobalFallback ? AnimationBoundsSelection::ClipFallback : AnimationBoundsSelection::StaticFallbackMissingClip};
    }
} // namespace AnimationLoading
