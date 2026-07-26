#pragma once
#include <Base/Types.h>

#include <Scripting/Defines.h>
#include <Scripting/LuaMethodTable.h>

namespace Scripting::Perf
{
    class PerfHandler : public LuaHandlerBase
    {
    public:
        void Register(Zenith* zenith);
        void Clear(Zenith* zenith) {}

        void PostLoad(Zenith* zenith) {}
        void Update(Zenith* zenith, f32 deltaTime) {}

        static i32 GetCPUName(Zenith* zenith);
        static i32 GetGPUName(Zenith* zenith);
        static i32 GetShadowCascadeNum(Zenith* zenith);
        static i32 GetFrameStats(Zenith* zenith);
        static i32 GetRenderPasses(Zenith* zenith);
        static i32 GetCullingStats(Zenith* zenith);
        static i32 GetGraphSeries(Zenith* zenith);
        static i32 DrawFrameGraph(Zenith* zenith);
    };

    static LuaRegister<> perfGlobalMethods[] =
    {
        { "GetCPUName",          PerfHandler::GetCPUName,          Scripting::LuaMethodFlags::DeveloperOnly },
        { "GetGPUName",          PerfHandler::GetGPUName,          Scripting::LuaMethodFlags::DeveloperOnly },
        { "GetShadowCascadeNum", PerfHandler::GetShadowCascadeNum, Scripting::LuaMethodFlags::DeveloperOnly },
        { "GetFrameStats",       PerfHandler::GetFrameStats,       Scripting::LuaMethodFlags::DeveloperOnly },
        { "GetRenderPasses",     PerfHandler::GetRenderPasses,     Scripting::LuaMethodFlags::DeveloperOnly },
        { "GetCullingStats",     PerfHandler::GetCullingStats,     Scripting::LuaMethodFlags::DeveloperOnly },
        { "GetGraphSeries",      PerfHandler::GetGraphSeries,      Scripting::LuaMethodFlags::DeveloperOnly },
        { "DrawFrameGraph",      PerfHandler::DrawFrameGraph,      Scripting::LuaMethodFlags::DeveloperOnly },
    };
}
