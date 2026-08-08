#pragma once
#include "MaterialAssetReader.h"

#include <functional>
#include <vector>

namespace MaterialLoading
{
    using ResolveTextureCallback = std::function<u32(FileFormat::AssetID, bool)>;

    // Builds CPU-side material parameter blocks and generic texture-slot tables for one material instance.
    // It resolves portable texture AssetIDs while keeping texture selection independent of parameter layout.
    class MaterialInstancePatcher
    {
      public:
        static bool Patch(const MaterialInstanceAssetView& instance, const ResolveTextureCallback& resolveTexture, u32 textureSlotCount, u32 fallbackTextureIndex, std::vector<u8>& outParameterData,
                          std::vector<u32>& outTextureIndices, std::vector<u32>& outSamplerIDs);
    };
} // namespace MaterialLoading
