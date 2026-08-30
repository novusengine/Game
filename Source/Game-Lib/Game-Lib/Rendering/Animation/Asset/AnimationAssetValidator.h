#pragma once
#include "AnimationAssetReader.h"

namespace AnimationLoading
{
    // Performs opt-in structural and semantic validation of decoded animation assets.
    // It rejects malformed hierarchy, sample, and reference data before runtime use.
    class AnimationAssetValidator
    {
      public:
        static SkeletonAssetReadResult Validate(SkeletonAssetReadResult result);
        static AnimationClipAssetReadResult Validate(AnimationClipAssetReadResult result);
        static AnimationSetAssetReadResult Validate(AnimationSetAssetReadResult result);
        static AnimationBoundsAssetReadResult Validate(AnimationBoundsAssetReadResult result);
        static AssetLoading::Diagnostic ValidateDomain(const AnimationClipAssetView& clip, FileFormat::Animation::PoseDomainType expectedType, FileFormat::AssetID expectedAssetID);
    };
} // namespace AnimationLoading
