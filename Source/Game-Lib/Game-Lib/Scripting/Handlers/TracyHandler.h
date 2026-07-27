#pragma once

#include <Base/Types.h>

#include <Scripting/LuaMethodTable.h>

namespace Scripting
{
    struct Zenith;
}

namespace Scripting::Tracy
{
    class TracyHandler
    {
    public:
        static void Register(Zenith* zenith);
        static i32 IsConnected(Zenith* zenith);
        static i32 Message(Zenith* zenith);
    };

    static LuaRegister<> tracyGlobalMethods[] =
    {
        { "IsConnected", TracyHandler::IsConnected, Scripting::LuaMethodFlags::DeveloperOnly },
        { "Message", TracyHandler::Message, Scripting::LuaMethodFlags::DeveloperOnly },
    };
}
