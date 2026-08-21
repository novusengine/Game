#pragma once

#include <Base/Types.h>

#include <Scripting/LuaMethodTable.h>

namespace Scripting::Game
{
    struct Interaction
    {
    public:
        static void Register(Zenith* zenith);
    };

    namespace InteractionMethods
    {
        i32 GetState(Zenith* zenith);
        i32 Open(Zenith* zenith);
        i32 Select(Zenith* zenith);
        i32 Close(Zenith* zenith);
    }

    static LuaRegister<> interactionGlobalFunctions[] =
    {
        { "GetState", InteractionMethods::GetState },
        { "Open", InteractionMethods::Open },
        { "Select", InteractionMethods::Select },
        { "Close", InteractionMethods::Close }
    };
}
