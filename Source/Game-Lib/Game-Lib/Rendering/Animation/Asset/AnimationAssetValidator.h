#pragma once
#include "AnimationAssetReader.h"

namespace AnimationLoading
{
    class AnimationAssetValidator
    {
      public:
        static SkeletonAssetReadResult Validate(SkeletonAssetReadResult result);
        static AnimationClipAssetReadResult Validate(AnimationClipAssetReadResult result);
        static AssetLoading::Diagnostic ValidateDomain(const AnimationClipAssetView& clip, FileFormat::Animation::PoseDomainType expectedType, FileFormat::AssetID expectedAssetID);
    };
} // namespace AnimationLoading
