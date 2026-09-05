#pragma once
#include "AnimationAssetReader.h"

namespace AnimationLoading
{
    // Performs opt-in semantic validation of structurally decoded animation assets.
    // It verifies hierarchy, sample, and reference contracts produced by offline tools.
    class AnimationAssetValidator
    {
      public:
        static SkeletonAssetReadResult Validate(SkeletonAssetReadResult result);
        static AnimationClipAssetReadResult Validate(AnimationClipAssetReadResult result);
        static AnimationSetAssetReadResult Validate(AnimationSetAssetReadResult result);
        static BoneMaskAssetReadResult Validate(BoneMaskAssetReadResult result);
        static IKRigAssetReadResult Validate(IKRigAssetReadResult result);
        static AnimationBoundsAssetReadResult Validate(AnimationBoundsAssetReadResult result);
        static AssetLoading::Diagnostic ValidateDomain(const AnimationClipAssetView& clip, FileFormat::Animation::PoseDomainType expectedType, FileFormat::AssetID expectedAssetID);
    };
} // namespace AnimationLoading
