#pragma once

#include <Base/Types.h>

#include <Scripting/LuaMethodTable.h>

namespace Scripting
{
    struct Zenith;
}

namespace Scripting::RenderTarget
{
    class RenderTargetHandler
    {
    public:
        static void Register(Zenith* zenith);
        static i32 Dump(Zenith* zenith);
    };

    static LuaRegister<> renderTargetGlobalMethods[] =
    {
        { "Dump", RenderTargetHandler::Dump, Scripting::LuaMethodFlags::DeveloperOnly },
    };
}
