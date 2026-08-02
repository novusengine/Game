#pragma once

#include <Base/Types.h>

#include <type_safe/strong_typedef.hpp>

namespace RenderAssets
{
    STRONG_TYPEDEF(ModelHandle, u32);
    STRONG_TYPEDEF(MaterialHandle, u32);
    STRONG_TYPEDEF(MaterialInstanceHandle, u32);
} // namespace RenderAssets
