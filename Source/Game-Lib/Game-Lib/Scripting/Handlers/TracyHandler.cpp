#include "TracyHandler.h"

#include "Game-Lib/Util/ServiceLocator.h"

#include <Scripting/LuaManager.h>
#include <Scripting/Zenith.h>

#include <tracy/Tracy.hpp>

#include <cstring>

namespace Scripting::Tracy
{
    void TracyHandler::Register(Zenith* zenith)
    {
        LuaManager* luaManager = ServiceLocator::GetLuaManager();
        const bool inDeveloperMode = luaManager && luaManager->IsDeveloperMode();
        const Scripting::LuaMethodFlags excludeFlags = inDeveloperMode
            ? Scripting::LuaMethodFlags::None
            : Scripting::LuaMethodFlags::DeveloperOnly;

        LuaMethodTable::Set(zenith, tracyGlobalMethods, "Tracy", excludeFlags);
    }

    i32 TracyHandler::IsConnected(Zenith* zenith)
    {
        zenith->Push(TracyIsConnected);
        return 1;
    }

    i32 TracyHandler::Message(Zenith* zenith)
    {
        const char* message = zenith->CheckVal<const char*>(1);
        if (message)
        {
            TracyMessage(message, std::strlen(message));
        }
        return 0;
    }
}
