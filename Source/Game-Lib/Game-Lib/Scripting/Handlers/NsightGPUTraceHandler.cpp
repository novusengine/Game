#include "NsightGPUTraceHandler.h"

#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/NsightGPUTrace.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Scripting/LuaManager.h>
#include <Scripting/Zenith.h>

#include <string>

namespace
{
    ::NsightGPUTrace* GetNsightCapture()
    {
        GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
        return gameRenderer ? gameRenderer->GetNsightGPUTrace() : nullptr;
    }
}

namespace Scripting::NsightGPUTrace
{
    void NsightGPUTraceHandler::Register(Zenith* zenith)
    {
        LuaManager* luaManager = ServiceLocator::GetLuaManager();
        const bool inDeveloperMode = luaManager && luaManager->IsDeveloperMode();
        const Scripting::LuaMethodFlags excludeFlags = inDeveloperMode
            ? Scripting::LuaMethodFlags::None
            : Scripting::LuaMethodFlags::DeveloperOnly;

        LuaMethodTable::Set(zenith, nsightGPUTraceGlobalMethods, "NsightGPUTrace", excludeFlags);
    }

    i32 NsightGPUTraceHandler::IsAvailable(Zenith* zenith)
    {
        ::NsightGPUTrace* capture = GetNsightCapture();
        zenith->Push(capture && capture->IsAvailable());
        return 1;
    }

    i32 NsightGPUTraceHandler::IsReady(Zenith* zenith)
    {
        ::NsightGPUTrace* capture = GetNsightCapture();
        zenith->Push(capture && capture->IsReady());
        return 1;
    }

    i32 NsightGPUTraceHandler::IsCapturing(Zenith* zenith)
    {
        ::NsightGPUTrace* capture = GetNsightCapture();
        zenith->Push(capture && capture->GetState() == ::NsightGPUTrace::State::Capturing);
        return 1;
    }

    i32 NsightGPUTraceHandler::Start(Zenith* zenith)
    {
        ::NsightGPUTrace* capture = GetNsightCapture();
        std::string error;
        const bool started = capture && capture->Start(error);
        if (!started && error.empty())
            error = "Nsight GPU Trace is unavailable";

        zenith->Push(started);
        zenith->Push(error.c_str());
        return 2;
    }

    i32 NsightGPUTraceHandler::Stop(Zenith* zenith)
    {
        ::NsightGPUTrace* capture = GetNsightCapture();
        std::string error;
        const bool stopped = capture && capture->Stop(error);
        if (!stopped && error.empty())
            error = "Nsight GPU Trace is unavailable";

        zenith->Push(stopped);
        zenith->Push(error.c_str());
        return 2;
    }
}
