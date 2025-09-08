#pragma once
#include "Game-Lib/Scripting/UI/Widget.h"

#include <Base/Types.h>

#include <Scripting/Defines.h>
#include <Scripting/LuaMethodTable.h>

namespace Scripting::UI
{
    struct Canvas : public Widget
    {
    public:
        static void Register(Zenith* zenith);

    };

    namespace CanvasMethods
    {
    };
}