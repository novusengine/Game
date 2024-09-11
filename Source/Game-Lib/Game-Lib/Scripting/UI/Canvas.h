#pragma once
#include "Game/Scripting/UI/Widget.h"

#include <Base/Types.h>

#include <entt/fwd.hpp>

namespace Scripting::UI
{
    struct Canvas : public Widget
    {
    public:
        static void Register(lua_State* state);

    };

    namespace CanvasMethods
    {
    };
}