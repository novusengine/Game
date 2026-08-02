#pragma once
#include "ModelAssetReader.h"

namespace ModelLoading
{
    // Performs opt-in CPU-side structural and semantic validation of decoded model assets.
    class ModelAssetValidator
    {
      public:
        static ModelAssetReadResult Validate(ModelAssetReadResult result);
    };
} // namespace ModelLoading
