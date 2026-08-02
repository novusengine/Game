#pragma once
#include "ModelAssetReader.h"

namespace ModelLoading
{
    // Performs opt-in CPU-side structural and semantic validation of decoded model assets.
    // It catches malformed offline data before unsafe geometry ranges reach GPU-backed storage.
    class ModelAssetValidator
    {
      public:
        static ModelAssetReadResult Validate(ModelAssetReadResult result);
    };
} // namespace ModelLoading
