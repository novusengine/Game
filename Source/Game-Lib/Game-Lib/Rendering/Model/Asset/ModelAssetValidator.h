#pragma once
#include "ModelAssetReader.h"

namespace ModelLoading
{
    class ModelAssetValidator
    {
      public:
        static ModelAssetReadResult Validate(ModelAssetReadResult result);
    };
} // namespace ModelLoading
