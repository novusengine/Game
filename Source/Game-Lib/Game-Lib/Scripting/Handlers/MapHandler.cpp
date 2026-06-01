#include "MapHandler.h"
#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/ECS/Util/Database/MapUtil.h"
#include "Game-Lib/Gameplay/MapLoader.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Model/ModelLoader.h"
#include "Game-Lib/Rendering/Terrain/TerrainLoader.h"
#include "Game-Lib/Scripting/Util/ZenithUtil.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/Util/StringUtils.h>

#include <FileFormat/Novus/ClientDB/ClientDB.h>

#include <MetaGen/Game/Lua/Lua.h>
#include <MetaGen/Shared/ClientDB/ClientDB.h>

#include <Scripting/LuaManager.h>
#include <Scripting/Zenith.h>

#include <entt/entt.hpp>
#include <lualib.h>

#include <algorithm>
#include <string>
#include <vector>

namespace Scripting::Map
{
    void MapHandler::Register(Zenith* zenith)
    {
        LuaManager* luaManager = ServiceLocator::GetLuaManager();
        const bool inDeveloperMode = luaManager && luaManager->IsDeveloperMode();
        const Scripting::LuaMethodFlags excludeFlags = inDeveloperMode
            ? Scripting::LuaMethodFlags::None
            : Scripting::LuaMethodFlags::DeveloperOnly;

        LuaMethodTable::Set(zenith, mapGlobalMethods, "Map", excludeFlags);

        _onCurrentMapChangedRef = LUA_NOREF;
    }

    void MapHandler::Clear(Zenith* zenith)
    {
        _onCurrentMapChangedRef = LUA_NOREF;
    }

    static MapHandler* GetSelf()
    {
        LuaManager* luaManager = ServiceLocator::GetLuaManager();
        if (!luaManager)
            return nullptr;
        return luaManager->GetLuaHandler<MapHandler>(static_cast<LuaHandlerID>(MetaGen::Game::Lua::LuaHandlerTypeEnum::Map));
    }

    static ClientDB::Data* GetMapStorage()
    {
        EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
        if (!registries || !registries->dbRegistry)
            return nullptr;
        auto& ctx = registries->dbRegistry->ctx();
        if (!ctx.contains<ECS::Singletons::ClientDBSingleton>())
            return nullptr;
        auto& clientDBSingleton = ctx.get<ECS::Singletons::ClientDBSingleton>();
        return clientDBSingleton.Get(ClientDBHash::Map);
    }

    i32 MapHandler::GetCurrent(Zenith* zenith)
    {
        GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
        if (!gameRenderer || !gameRenderer->GetTerrainLoader())
        {
            zenith->Push("");
            return 1;
        }
        const std::string& name = gameRenderer->GetTerrainLoader()->GetCurrentMapInternalName();
        zenith->Push(name.c_str());
        return 1;
    }

    i32 MapHandler::GetLoadingProgress(Zenith* zenith)
    {
        GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
        if (!gameRenderer)
        {
            zenith->Push(0.0f);
            return 1;
        }
        f32 terrainProgress = gameRenderer->GetTerrainLoader()->GetLoadingProgress();
        f32 modelProgress = gameRenderer->GetModelLoader()->GetLoadingProgress();
        zenith->Push((terrainProgress + modelProgress) * 0.5f);
        return 1;
    }

    i32 MapHandler::GetList(Zenith* zenith)
    {
        zenith->CreateTable();

        ClientDB::Data* storage = GetMapStorage();
        if (!storage)
            return 1;

        struct Entry
        {
            u32 id;
            std::string internalName;
            std::string name;
            u32 flags;
            u8 instanceType;
            u8 expansionID;
            u16 maxPlayers;
        };

        std::vector<Entry> entries;
        entries.reserve(storage->GetNumRows());
        storage->Each([&entries, storage](u32 id, const MetaGen::Shared::ClientDB::MapRecord& record) -> bool
        {
            entries.push_back({
                id,
                storage->GetString(record.nameInternal),
                storage->GetString(record.name),
                record.flags,
                record.instanceType,
                record.expansionID,
                record.maxPlayers,
            });
            return true;
        });
        std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b)
        {
            return a.name < b.name;
        });

        for (size_t i = 0; i < entries.size(); ++i)
        {
            const Entry& e = entries[i];
            zenith->CreateTable();
            zenith->AddTableField("id", e.id);
            zenith->AddTableField("internalName", e.internalName.c_str());
            zenith->AddTableField("name", e.name.c_str());
            zenith->AddTableField("flags", e.flags);
            zenith->AddTableField("instanceType", static_cast<u32>(e.instanceType));
            zenith->AddTableField("expansionID", static_cast<u32>(e.expansionID));
            zenith->AddTableField("maxPlayers", static_cast<u32>(e.maxPlayers));
            zenith->SetTableKey(static_cast<i32>(i + 1));
        }
        return 1;
    }

    i32 MapHandler::Load(Zenith* zenith)
    {
        const char* nameRaw = zenith->CheckVal<const char*>(1);
        if (!nameRaw)
        {
            zenith->Push(false);
            return 1;
        }

        std::string name = nameRaw;
        MetaGen::Shared::ClientDB::MapRecord* unused = nullptr;
        if (!ECSUtil::Map::GetMapFromInternalName(name, unused))
        {
            zenith->Push(false);
            return 1;
        }

        u32 hash = StringUtils::fnv1a_32(name.c_str(), name.length());
        ServiceLocator::GetGameRenderer()->GetMapLoader()->LoadMap(hash);
        zenith->Push(true);
        return 1;
    }

    i32 MapHandler::LoadByID(Zenith* zenith)
    {
        u32 id = zenith->CheckVal<u32>(1);

        ClientDB::Data* storage = GetMapStorage();
        if (!storage || !storage->Has(id))
        {
            zenith->Push(false);
            return 1;
        }

        const auto& record = storage->Get<MetaGen::Shared::ClientDB::MapRecord>(id);
        const std::string& internalName = storage->GetString(record.nameInternal);
        u32 hash = StringUtils::fnv1a_32(internalName.c_str(), internalName.length());
        ServiceLocator::GetGameRenderer()->GetMapLoader()->LoadMap(hash);
        zenith->Push(true);
        return 1;
    }

    i32 MapHandler::Unload(Zenith* zenith)
    {
        GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
        if (!gameRenderer || !gameRenderer->GetMapLoader())
        {
            zenith->Push(false);
            return 1;
        }
        gameRenderer->GetMapLoader()->UnloadMap();
        zenith->Push(true);
        return 1;
    }

    i32 MapHandler::SetOnCurrentMapChanged(Zenith* zenith)
    {
        MapHandler* self = GetSelf();
        if (!self)
            return 0;

        Scripting::Util::Zenith::Unref(zenith, self->_onCurrentMapChangedRef);
        self->_onCurrentMapChangedRef = LUA_NOREF;

        if (zenith->IsFunction(1))
        {
            self->_onCurrentMapChangedRef = zenith->GetRef(1);
        }
        return 0;
    }

    void MapHandler::OnCurrentMapChanged(Zenith* zenith)
    {
        if (_onCurrentMapChangedRef == LUA_NOREF)
            return;

        zenith->GetRawI(LUA_REGISTRYINDEX, _onCurrentMapChangedRef);
        zenith->PCall(0);
    }
}
