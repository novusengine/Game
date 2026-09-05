#pragma once
#include "ModelAssetReader.h"

namespace ModelLoading
{
    // Performs opt-in CPU-side semantic validation of structurally decoded model assets.
    // It verifies offline cooker contracts before geometry reaches GPU-backed storage.
    class ModelAssetValidator
    {
      public:
        static ModelAssetReadResult Validate(ModelAssetReadResult result);
    };
} // namespace ModelLoading
