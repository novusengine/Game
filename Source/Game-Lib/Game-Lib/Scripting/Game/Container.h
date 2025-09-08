#pragma once
#include <Base/Types.h>

#include <Scripting/Defines.h>
#include <Scripting/LuaMethodTable.h>

namespace Scripting::Game
{
    struct Container
    {
    public:
        static void Register(Zenith* zenith);
    };

    namespace ContainerMethods
    {
        i32 RequestSwapSlots(Zenith* zenith);
        i32 GetContainerItems(Zenith* zenith);
    };

    static LuaRegister<> containerGlobalFunctions[] =
    {
        { "RequestSwapSlots", ContainerMethods::RequestSwapSlots },
        { "GetContainerItems", ContainerMethods::GetContainerItems }
    };
}