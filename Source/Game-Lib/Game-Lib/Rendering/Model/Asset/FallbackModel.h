#pragma once
#include "ModelAssetReader.h"

namespace ModelLoading
{
    // Returns precomputed cooker-final cube data. No runtime meshlet generation is performed.
    ModelAssetView GetFallbackModelAssetView();
} // namespace ModelLoading
