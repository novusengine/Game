#include "AnimationSetResolver.h"

#include <algorithm>
#include <limits>

namespace
{
    u64 Mix(u64 value)
    {
        value += 0x9E3779B97F4A7C15ull;
        value = (value ^ (value >> 30u)) * 0xBF58476D1CE4E5B9ull;
        value = (value ^ (value >> 27u)) * 0x94D049BB133111EBull;
        return value ^ (value >> 31u);
    }
}

namespace Animation
{
    ResolvedAnimationSlot ResolveAnimationSlot(const AnimationLoading::AnimationSetAssetView& set, std::span<const AnimationClipDomainRecord> clips, u64 semanticID, u64 stableSeed, u32 preservedVariationID)
    {
        u32 slotIndex = std::numeric_limits<u32>::max();
        for (u32 index = 0; index < set.slots.size(); ++index)
            if (set.slots[index].semanticID == semanticID)
            {
                slotIndex = index;
                break;
            }
        if (slotIndex == std::numeric_limits<u32>::max())
            return {.resolvedSemanticID = semanticID, .resolution = AnimationSlotResolution::MissingSlot};

        u32 fallbackDepth = 0;
        while (slotIndex < set.slots.size())
        {
            const FileFormat::Animation::AnimationSetSlot& slot = set.slots[slotIndex];
            if (slot.variantOffset > set.variants.size() || slot.numVariants > set.variants.size() - slot.variantOffset)
                return {.resolvedSemanticID = slot.semanticID, .slotIndex = slotIndex, .fallbackDepth = fallbackDepth, .resolution = AnimationSlotResolution::InvalidSet};
            if (slot.numVariants != 0)
            {
                const FileFormat::Animation::AnimationClipVariant* selected = nullptr;
                if (preservedVariationID != 0)
                    for (u32 local = 0; local < slot.numVariants; ++local)
                        if (set.variants[slot.variantOffset + local].stableVariationID == preservedVariationID)
                            selected = &set.variants[slot.variantOffset + local];
                if (!selected)
                {
                    u64 totalWeight = 0;
                    for (u32 local = 0; local < slot.numVariants; ++local)
                        totalWeight += set.variants[slot.variantOffset + local].selectionWeight;
                    if (totalWeight == 0)
                        return {.resolvedSemanticID = slot.semanticID, .slotIndex = slotIndex, .fallbackDepth = fallbackDepth, .resolution = AnimationSlotResolution::InvalidSet};
                    u64 choice = Mix(stableSeed ^ slot.semanticID) % totalWeight;
                    for (u32 local = 0; local < slot.numVariants; ++local)
                    {
                        const FileFormat::Animation::AnimationClipVariant& variant = set.variants[slot.variantOffset + local];
                        if (choice < variant.selectionWeight)
                        {
                            selected = &variant;
                            break;
                        }
                        choice -= variant.selectionWeight;
                    }
                }
                if (!selected)
                    return {.resolvedSemanticID = slot.semanticID, .slotIndex = slotIndex, .fallbackDepth = fallbackDepth, .resolution = AnimationSlotResolution::InvalidSet};
                const auto clip = std::find_if(clips.begin(), clips.end(), [selected](const AnimationClipDomainRecord& value) { return value.clipAssetID == selected->animationClipAssetID; });
                if (clip == clips.end())
                    return {.clipAssetID = selected->animationClipAssetID, .resolvedSemanticID = slot.semanticID, .stableVariationID = selected->stableVariationID, .slotIndex = slotIndex, .fallbackDepth = fallbackDepth, .resolution = AnimationSlotResolution::MissingClip};
                if (clip->domainType != set.root.declaredPoseDomainType || clip->domainAssetID != set.root.declaredPoseDomainAssetID)
                    return {.clipAssetID = selected->animationClipAssetID, .resolvedSemanticID = slot.semanticID, .stableVariationID = selected->stableVariationID, .slotIndex = slotIndex, .fallbackDepth = fallbackDepth, .resolution = AnimationSlotResolution::IncompatibleDomain};
                return {selected->animationClipAssetID, slot.semanticID, selected->stableVariationID, slotIndex, fallbackDepth, fallbackDepth == 0 ? AnimationSlotResolution::Resolved : AnimationSlotResolution::ResolvedFallback};
            }
            if (slot.fallbackSlotIndex < 0)
                return {.resolvedSemanticID = slot.semanticID, .slotIndex = slotIndex, .fallbackDepth = fallbackDepth, .resolution = (slot.flags & FileFormat::Animation::AnimationSetSlotFlags_Required) != 0 ? AnimationSlotResolution::MissingRequiredSlot : AnimationSlotResolution::MissingSlot};
            slotIndex = static_cast<u32>(slot.fallbackSlotIndex);
            if (++fallbackDepth > set.slots.size())
                return {.resolvedSemanticID = semanticID, .fallbackDepth = fallbackDepth, .resolution = AnimationSlotResolution::InvalidSet};
        }
        return {.resolvedSemanticID = semanticID, .fallbackDepth = fallbackDepth, .resolution = AnimationSlotResolution::InvalidSet};
    }
} // namespace Animation
