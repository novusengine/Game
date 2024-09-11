#pragma once
#include "Game/Scripting/UI/Widget.h"

#include <Base/Types.h>

#include <entt/entt.hpp>

namespace Scripting::UI
{
    struct Button : public Widget
    {
    public:
        static void Register(lua_State* state);

        entt::entity panelEntity;
        entt::entity textEntity;
    };

    namespace ButtonMethods
    {
        i32 SetText(lua_State* state);
    };
}