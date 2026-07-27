#include "RenderDocHandler.h"

#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/RenderDocCapture.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Scripting/LuaManager.h>
#include <Scripting/Zenith.h>

#include <string>

namespace Scripting::RenderDoc
{
    void RenderDocHandler::Register(Zenith* zenith)
    {
        LuaManager* luaManager = ServiceLocator::GetLuaManager();
        const bool inDeveloperMode = luaManager && luaManager->IsDeveloperMode();
        const Scripting::LuaMethodFlags excludeFlags = inDeveloperMode
            ? Scripting::LuaMethodFlags::None
            : Scripting::LuaMethodFlags::DeveloperOnly;

        LuaMethodTable::Set(zenith, renderDocGlobalMethods, "RenderDoc", excludeFlags);
    }

    i32 RenderDocHandler::IsAvailable(Zenith* zenith)
    {
        GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
        RenderDocCapture* capture = gameRenderer
            ? gameRenderer->GetRenderDocCapture()
            : nullptr;

        zenith->Push(capture && capture->IsAvailable());
        return 1;
    }

    i32 RenderDocHandler::CaptureNextFrame(Zenith* zenith)
    {
        const char* artifactPathRaw = zenith->CheckVal<const char*>(1);
        const std::string artifactPath = artifactPathRaw ? artifactPathRaw : "";

        GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
        RenderDocCapture* capture = gameRenderer
            ? gameRenderer->GetRenderDocCapture()
            : nullptr;

        std::string error;
        const bool queued =
            capture &&
            capture->QueueNextFrame(artifactPath, error);
        if (!queued && error.empty())
            error = "RenderDoc capture is unavailable";

        zenith->Push(queued);
        zenith->Push(error.c_str());
        return 2;
    }
}
