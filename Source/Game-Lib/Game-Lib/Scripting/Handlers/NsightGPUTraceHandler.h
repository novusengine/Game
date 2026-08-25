#pragma once

#include <Base/Types.h>

#include <Scripting/LuaMethodTable.h>

namespace Scripting
{
    struct Zenith;
}

namespace Scripting::NsightGPUTrace
{
    class NsightGPUTraceHandler
    {
    public:
        static void Register(Zenith* zenith);
        static i32 IsAvailable(Zenith* zenith);
        static i32 IsReady(Zenith* zenith);
        static i32 IsCapturing(Zenith* zenith);
        static i32 Start(Zenith* zenith);
        static i32 Stop(Zenith* zenith);
    };

    static LuaRegister<> nsightGPUTraceGlobalMethods[] =
    {
        { "IsAvailable", NsightGPUTraceHandler::IsAvailable, Scripting::LuaMethodFlags::DeveloperOnly },
        { "IsReady", NsightGPUTraceHandler::IsReady, Scripting::LuaMethodFlags::DeveloperOnly },
        { "IsCapturing", NsightGPUTraceHandler::IsCapturing, Scripting::LuaMethodFlags::DeveloperOnly },
        { "Start", NsightGPUTraceHandler::Start, Scripting::LuaMethodFlags::DeveloperOnly },
        { "Stop", NsightGPUTraceHandler::Stop, Scripting::LuaMethodFlags::DeveloperOnly },
    };
}
