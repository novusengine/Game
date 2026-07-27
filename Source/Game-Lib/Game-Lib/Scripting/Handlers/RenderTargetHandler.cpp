#include "RenderTargetHandler.h"

#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/RenderTargetCapture.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Scripting/LuaManager.h>
#include <Scripting/Zenith.h>

#include <string>

namespace Scripting::RenderTarget
{
    void RenderTargetHandler::Register(Zenith* zenith)
    {
        LuaManager* luaManager = ServiceLocator::GetLuaManager();
        const bool inDeveloperMode = luaManager && luaManager->IsDeveloperMode();
        const Scripting::LuaMethodFlags excludeFlags = inDeveloperMode
            ? Scripting::LuaMethodFlags::None
            : Scripting::LuaMethodFlags::DeveloperOnly;

        LuaMethodTable::Set(zenith, renderTargetGlobalMethods, "RenderTarget", excludeFlags);
    }

    i32 RenderTargetHandler::Dump(Zenith* zenith)
    {
        const char* debugNameRaw = zenith->CheckVal<const char*>(1);
        const char* artifactPathRaw = zenith->CheckVal<const char*>(2);
        const std::string debugName = debugNameRaw ? debugNameRaw : "";
        const std::string artifactPath = artifactPathRaw ? artifactPathRaw : "";

        GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
        std::string error;
        const bool queued =
            gameRenderer &&
            gameRenderer->GetRenderTargetCapture() &&
            gameRenderer->GetRenderTargetCapture()->Queue(debugName, artifactPath, error);

        if (!queued && error.empty())
            error = "Render-target capture is unavailable";

        zenith->Push(queued);
        zenith->Push(error.c_str());
        return 2;
    }
}
