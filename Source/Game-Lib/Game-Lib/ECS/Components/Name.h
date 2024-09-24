#pragma once
#include <Base/Types.h>
#include <Base/Util/Reflection.h>

#include <limits>

namespace ECS::Components
{
    struct Name
    {
    public:
        std::string name;
        std::string fullName;
        u32 nameHash = std::numeric_limits<u32>().max();
    };
}

REFL_TYPE(ECS::Components::Name)
    REFL_FIELD(name, Reflection::ReadOnly())
    REFL_FIELD(fullName, Reflection::ReadOnly())
    REFL_FIELD(nameHash, Reflection::Hidden())
REFL_END