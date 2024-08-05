#include "GlobalHandler.h"
#include "Game/Application/EnttRegistries.h"
#include "Game/ECS/Util/MapUtil.h"
#include "Game/ECS/Singletons/MapDB.h"
#include "Game/Gameplay/MapLoader.h"
#include "Game/Rendering/GameRenderer.h"
#include "Game/Scripting/LuaState.h"
#include "Game/Scripting/LuaManager.h"
#include "Game/Scripting/Systems/LuaSystemBase.h"
#include "Game/Util/ServiceLocator.h"

#include <entt/entt.hpp>
#include <lualib.h>

namespace Scripting
{
    struct Panel
    {
    public:
        vec3 position;
        vec3 extents;
    };

    void GlobalHandler::Register(lua_State* state)
    {
        LuaManager* luaManager = ServiceLocator::GetLuaManager();
        LuaState ctx(state);

        ctx.CreateTableAndPopulate("Engine", [&]()
        {
            ctx.SetTable("Name", "NovusEngine");
            ctx.SetTable("Version", vec3(0.0f, 0.0f, 1.0f));
        });

        LuaMethodTable::Set(state, globalMethods);

        LuaMethodTable::Set(state, panelMethods, "Panel");
        LuaMetaTable<Panel>::Register(state, "PanelMetaTable");
        LuaMetaTable<Panel>::Set(state, panelMetaTableMethods);
    }

    i32 GlobalHandler::AddCursor(lua_State* state)
    {
        LuaState ctx(state);

        const char* cursorName = ctx.Get(nullptr, 1);
        const char* cursorPath = ctx.Get(nullptr, 2);

        if (cursorName == nullptr || cursorPath == nullptr)
        {
            ctx.Push(false);
            return 1;
        }

        u32 hash = StringUtils::fnv1a_32(cursorName, strlen(cursorName));
        std::string path = cursorPath;

        GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
        bool result = gameRenderer->AddCursor(hash, path);

        ctx.Push(result);
        return 1;
    }
    i32 GlobalHandler::SetCursor(lua_State* state)
    {
        LuaState ctx(state);

        const char* cursorName = ctx.Get(nullptr);
        if (cursorName == nullptr)
        {
            ctx.Push(false);
            return 1;
        }

        u32 hash = StringUtils::fnv1a_32(cursorName, strlen(cursorName));

        GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
        bool result = gameRenderer->SetCursor(hash);

        ctx.Push(result);
        return 1;
    }
    i32 GlobalHandler::GetCurrentMap(lua_State* state)
    {
        LuaState ctx(state);

        const std::string& currentMapInternalName = ServiceLocator::GetGameRenderer()->GetTerrainLoader()->GetCurrentMapInternalName();

        ctx.Push(currentMapInternalName.c_str());
        return 1;
    }
    i32 GlobalHandler::LoadMap(lua_State* state)
    {
        LuaState ctx(state);

        const char* mapInternalName = ctx.Get(nullptr);
        size_t mapInternalNameLen = strlen(mapInternalName);

        if (mapInternalName == nullptr)
        {
            ctx.Push(false);
            return 1;
        }

        ClientDB::Definitions::Map* map = nullptr;
        if (!ECS::Util::Map::GetMapFromInternalName(mapInternalName, map))
        {
            ctx.Push(false);
            return 1;
        }

        u32 mapNameHash = StringUtils::fnv1a_32(mapInternalName, mapInternalNameLen);

        MapLoader* mapLoader = ServiceLocator::GetGameRenderer()->GetMapLoader();
        mapLoader->LoadMap(mapNameHash);

        ctx.Push(true);
        return 1;
    }
    i32 GlobalHandler::PanelCreate(lua_State* state)
    {
        LuaState ctx(state);

        Panel* panel = ctx.PushUserData<Panel>([](void* x) {
            // Very sad panel is gone now :(
        });

        panel->position = vec3(25.0f, 50.0f, 0);
        panel->extents = vec3(1.5f, 1.5f, 0);

        luaL_getmetatable(state, "PanelMetaTable");
        lua_setmetatable(state, -2);

        return 1;
    }
    i32 GlobalHandler::PanelGetPosition(lua_State* state)
    {
        LuaState ctx(state);

        Panel* panel = ctx.GetUserData<Panel>();
        ctx.Push(panel->position.x);
        ctx.Push(panel->position.y);

        return 2;
    }
    i32 GlobalHandler::PanelGetExtents(lua_State* state)
    {
        LuaState ctx(state);

        Panel* panel = ctx.GetUserData<Panel>();
        ctx.Push(panel->extents.x);
        ctx.Push(panel->extents.y);

        return 2;
    }
    i32 GlobalHandler::PanelToString(lua_State* state)
    {
        LuaState ctx(state);

        ctx.Push("Here is a panel :o");
        return 1;
    }
    i32 GlobalHandler::PanelIndex(lua_State* state)
    {
        LuaState ctx(state);

        Panel* panel = ctx.GetUserData<Panel>(nullptr, 1);
        const char* key = ctx.Get(nullptr, 2);
        u32 keyHash = StringUtils::fnv1a_32(key, strlen(key));

        switch (keyHash)
        {
            case "position"_h:
            {
                ctx.Push(panel->position);
                break;
            }
            case "extents"_h:
            {
                ctx.Push(panel->extents);
                break;
            }

            default:
            {
                ctx.GetGlobalRaw("Panel");
                ctx.Push(key);
                ctx.GetRaw(-2);
            }
        }

        return 1;
    }
}
