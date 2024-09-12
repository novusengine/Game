#pragma once
#include "Game-Lib/Scripting/UI/Widget.h"

#include <Base/Types.h>

namespace Scripting::UI
{
    struct Panel : public Widget
    {
    public:
        static void Register(lua_State* state);

    };

    namespace PanelMethods
    {
        
    };
}