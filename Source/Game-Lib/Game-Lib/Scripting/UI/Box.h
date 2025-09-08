#pragma once
#include "Game-Lib/Scripting/UI/Widget.h"

#include <Base/Types.h>

#include <Scripting/Defines.h>
#include <Scripting/LuaMethodTable.h>

namespace Scripting::UI
{
    struct Box
    {
    public:
        static void Register(Zenith* zenith);

    };

    namespace BoxMethods
    {
        i32 CreateBox(Zenith* zenith);
    };

    static LuaRegister<> boxGlobalMethods[] =
    {
        { "new", BoxMethods::CreateBox }
    };
}