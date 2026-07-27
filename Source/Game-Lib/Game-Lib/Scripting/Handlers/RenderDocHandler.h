#pragma once

#include <Base/Types.h>

#include <Scripting/LuaMethodTable.h>

namespace Scripting
{
    struct Zenith;
}

namespace Scripting::RenderDoc
{
    class RenderDocHandler
    {
    public:
        static void Register(Zenith* zenith);
        static i32 IsAvailable(Zenith* zenith);
        static i32 CaptureNextFrame(Zenith* zenith);
    };

    static LuaRegister<> renderDocGlobalMethods[] =
    {
        { "IsAvailable", RenderDocHandler::IsAvailable, Scripting::LuaMethodFlags::DeveloperOnly },
        { "CaptureNextFrame", RenderDocHandler::CaptureNextFrame, Scripting::LuaMethodFlags::DeveloperOnly },
    };
}
