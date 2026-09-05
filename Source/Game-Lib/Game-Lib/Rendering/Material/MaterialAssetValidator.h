#pragma once
#include "MaterialAssetReader.h"

namespace MaterialLoading
{
    // Performs opt-in semantic validation of structurally decoded material assets.
    // It verifies parameter layouts, resource bindings, and cross-asset compatibility.
    class MaterialAssetValidator
    {
      public:
        static MaterialAssetReadResult<MaterialAssetView> ValidateMaterial(MaterialAssetReadResult<MaterialAssetView> result);
        static MaterialAssetReadResult<MaterialInstanceAssetView> ValidateMaterialInstance(
            MaterialAssetReadResult<MaterialInstanceAssetView> result, const MaterialAssetView& material);
    };
} // namespace MaterialLoading
