#pragma once
#include "MaterialAssetReader.h"

#include <functional>
#include <vector>

namespace MaterialLoading
{
    using ResolveTextureCallback = std::function<u32(FileFormat::AssetID, bool)>;

    // Builds CPU-side material parameter blocks by patching their declared bindings with GPU texture indices.
    // It converts portable AssetID bindings into the runtime texture references required by shaders.
    class MaterialInstancePatcher
    {
      public:
        static bool Patch(const MaterialInstanceAssetView& instance, const ResolveTextureCallback& resolveTexture, std::vector<u8>& outParameterData);
    };
} // namespace MaterialLoading
