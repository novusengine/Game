#pragma once
#include <Base/Types.h>

#include <Scripting/Defines.h>
#include <Scripting/LuaMethodTable.h>

#include <entt/fwd.hpp>

#include <string>

namespace Scripting::Asset
{
    // Backs the dev-only "Asset" Lua table: filesystem enumeration under Data/ and model
    // spawning, for the Lua Asset Browser. Mirrors Editor::AssetBrowser's data access.
    class AssetHandler : public LuaHandlerBase
    {
    public:
        void Register(Zenith* zenith);
        void Clear(Zenith* zenith) {}

        void PostLoad(Zenith* zenith) {}
        void Update(Zenith* zenith, f32 deltaTime) {}

        static i32 ListDir(Zenith* zenith);
        static i32 SpawnModel(Zenith* zenith);
        static i32 BeginDragSpawn(Zenith* zenith);

        // Creates an entity for the model at `dataRelativePath` (path relative to Data/) at the
        // given world position and kicks off its load. Shared by SpawnModel and the drag-spawn
        // editor flow. Returns entt::null on failure.
        static entt::entity CreateModelAtPosition(const std::string& dataRelativePath, const vec3& position);
    };

    static LuaRegister<> assetGlobalMethods[] =
    {
        { "ListDir",        AssetHandler::ListDir,        Scripting::LuaMethodFlags::DeveloperOnly },
        { "SpawnModel",     AssetHandler::SpawnModel,     Scripting::LuaMethodFlags::DeveloperOnly },
        { "BeginDragSpawn", AssetHandler::BeginDragSpawn, Scripting::LuaMethodFlags::DeveloperOnly },
    };
}
