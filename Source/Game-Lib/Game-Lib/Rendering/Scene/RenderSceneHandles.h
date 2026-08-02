#pragma once

#include <Base/Types.h>

#include <type_safe/strong_typedef.hpp>

#include <limits>

namespace RenderScenes
{
    STRONG_TYPEDEF(ModelInstanceHandle, u64);
    STRONG_TYPEDEF(ModelMaterialTableHandle, u32);
    STRONG_TYPEDEF(GeometryGroupMaskHandle, u32);

    inline constexpr u32 INVALID_SCENE_INDEX = std::numeric_limits<u32>().max();

    inline ModelInstanceHandle MakeModelInstanceHandle(u32 slot, u32 generation)
    {
        return ModelInstanceHandle((static_cast<u64>(generation) << 32u) | slot);
    }

    inline ModelInstanceHandle InvalidModelInstanceHandle()
    {
        return MakeModelInstanceHandle(INVALID_SCENE_INDEX, 0);
    }

    inline ModelMaterialTableHandle InvalidModelMaterialTableHandle()
    {
        return ModelMaterialTableHandle(INVALID_SCENE_INDEX);
    }

    inline GeometryGroupMaskHandle InvalidGeometryGroupMaskHandle()
    {
        return GeometryGroupMaskHandle(INVALID_SCENE_INDEX);
    }

    inline u32 GetModelInstanceSlot(ModelInstanceHandle handle)
    {
        return static_cast<u32>(static_cast<ModelInstanceHandle::type>(handle));
    }

    inline u32 GetModelInstanceGeneration(ModelInstanceHandle handle)
    {
        return static_cast<u32>(static_cast<ModelInstanceHandle::type>(handle) >> 32u);
    }
} // namespace RenderScenes
