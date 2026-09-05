#pragma once
#include "Game-Lib/Rendering/Animation/Asset/AnimationAssetReader.h"

#include <span>
#include <limits>

namespace Animation
{
    struct AnimationClipDomainRecord
    {
        FileFormat::AssetID clipAssetID = FileFormat::INVALID_ASSET_ID;
        FileFormat::Animation::PoseDomainType domainType = FileFormat::Animation::PoseDomainType::RigFamily;
        FileFormat::AssetID domainAssetID = FileFormat::INVALID_ASSET_ID;
        u64 durationMicroseconds = 0;
        std::span<const FileFormat::Animation::SynchronizationMarker> synchronizationMarkers;
        std::span<const FileFormat::Animation::AnimationEvent> events;
        std::span<const u8> eventPayloadBytes;
        bool looping = false;
    };

    enum class AnimationSlotResolution : u8
    {
        Resolved,
        ResolvedFallback,
        MissingSlot,
        MissingRequiredSlot,
        MissingClip,
        IncompatibleDomain,
        InvalidSet
    };

    struct ResolvedAnimationSlot
    {
        FileFormat::AssetID clipAssetID = FileFormat::INVALID_ASSET_ID;
        u64 resolvedSemanticID = 0;
        u32 stableVariationID = 0;
        u32 slotIndex = std::numeric_limits<u32>::max();
        u32 fallbackDepth = 0;
        AnimationSlotResolution resolution = AnimationSlotResolution::MissingSlot;
        explicit operator bool() const { return resolution == AnimationSlotResolution::Resolved || resolution == AnimationSlotResolution::ResolvedFallback; }
    };

    // Resolves semantic animation slots to compatible clips and deterministic authored variations.
    // Slot fallback chains keep graph logic independent from a character's available animations.
    ResolvedAnimationSlot ResolveAnimationSlot(const AnimationLoading::AnimationSetAssetView& set, std::span<const AnimationClipDomainRecord> clips, u64 semanticID, u64 stableSeed, u32 preservedVariationID = 0);
} // namespace Animation
