#pragma once
#include "MaterialAssetReader.h"

namespace MaterialLoading
{
    // Performs opt-in CPU-side structural and semantic validation of decoded material assets.
    class MaterialAssetValidator
    {
      public:
        static MaterialAssetReadResult<MaterialAssetView> ValidateMaterial(MaterialAssetReadResult<MaterialAssetView> result);
        static MaterialAssetReadResult<MaterialInstanceAssetView> ValidateMaterialInstance(
            MaterialAssetReadResult<MaterialInstanceAssetView> result, const MaterialAssetView& material);
    };
} // namespace MaterialLoading
