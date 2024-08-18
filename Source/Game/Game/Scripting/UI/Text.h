#pragma once
#include "Game/Scripting/UI/Widget.h"

#include <Base/Types.h>

namespace Scripting::UI
{
    struct Text : public Widget
    {
    public:
        static void Register(lua_State* state);

    };

    namespace TextMethods
    {
    };
}