#pragma once
#include "MaterialAssetReader.h"

namespace MaterialLoading
{
    class MaterialAssetValidator
    {
      public:
        static MaterialAssetReadResult<MaterialAssetView> ValidateMaterial(MaterialAssetReadResult<MaterialAssetView> result);
        static MaterialAssetReadResult<MaterialInstanceAssetView> ValidateMaterialInstance(
            MaterialAssetReadResult<MaterialInstanceAssetView> result, const MaterialAssetView& material);
    };
} // namespace MaterialLoading
