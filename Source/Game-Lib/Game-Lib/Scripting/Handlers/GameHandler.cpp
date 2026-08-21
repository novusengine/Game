#include "GameHandler.h"
#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Components/Events.h"
#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/ECS/Util/EventUtil.h"
#include "Game-Lib/Scripting/Game/Container.h"
#include "Game-Lib/Scripting/Game/Interaction.h"
#include "Game-Lib/Scripting/Handlers/RenderTargetHandler.h"
#include "Game-Lib/Scripting/Handlers/RenderDocHandler.h"
#include "Game-Lib/Scripting/Handlers/TracyHandler.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <FileFormat/Novus/ClientDB/ClientDB.h>
#include <MetaGen/EnumTraits.h>
#include <MetaGen/Game/Lua/Lua.h>
#include <MetaGen/Shared/ClientDB/ClientDB.h>
#include <Scripting/LuaManager.h>

#include <entt/entt.hpp>
#include <lualib.h>
#include <limits>
#include <string>

namespace Scripting::Game
{
    void GameHandler::Register(Zenith* zenith)
    {
        LuaMethodTable::Set(zenith, gameGlobalMethods, "Game");
        Scripting::Game::Container::Register(zenith);
        Scripting::Game::Interaction::Register(zenith);
        Scripting::RenderTarget::RenderTargetHandler::Register(zenith);
        Scripting::RenderDoc::RenderDocHandler::Register(zenith);
        Scripting::Tracy::TracyHandler::Register(zenith);
    }

    void GameHandler::Clear(Zenith*)
    {
        _isLoaded = false;
    }

    void GameHandler::PostLoad(Zenith*)
    {
        _isLoaded = true;
    }

    void GameHandler::Update(Zenith* zenith, f32)
    {
        ECS::Util::EventUtil::OnEvent<ECS::Components::MapLoadedEvent>(
            [zenith](const ECS::Components::MapLoadedEvent& event)
            {
                const bool isLoaded = event.mapId != std::numeric_limits<u32>::max();
                std::string mapInternalName;

                if (isLoaded)
                {
                    EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
                    if (registries && registries->dbRegistry)
                    {
                        auto& context = registries->dbRegistry->ctx();
                        if (context.contains<ECS::Singletons::ClientDBSingleton>())
                        {
                            auto& clientDB = context.get<ECS::Singletons::ClientDBSingleton>();
                            ClientDB::Data* mapStorage = clientDB.Get(ClientDBHash::Map);
                            if (mapStorage && mapStorage->Has(event.mapId))
                            {
                                const auto& map = mapStorage->Get<MetaGen::Shared::ClientDB::MapRecord>(event.mapId);
                                mapInternalName = mapStorage->GetString(map.internalName);
                            }
                        }
                    }
                }

                zenith->CallEvent(MetaGen::Game::Lua::GameEvent::MapLoaded, MetaGen::Game::Lua::GameEventDataMapLoaded{ .mapID = event.mapId, .mapInternalName = mapInternalName, .isLoaded = isLoaded });
            });

        ECS::Util::EventUtil::OnEvent<ECS::Components::MapLoadFailedEvent>(
            [zenith](const ECS::Components::MapLoadFailedEvent& event)
            {
                std::string mapInternalName;
                EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
                if (registries && registries->dbRegistry)
                {
                    auto& context = registries->dbRegistry->ctx();
                    if (context.contains<ECS::Singletons::ClientDBSingleton>())
                    {
                        auto& clientDB = context.get<ECS::Singletons::ClientDBSingleton>();
                        ClientDB::Data* mapStorage = clientDB.Get(ClientDBHash::Map);
                        if (mapStorage && mapStorage->Has(event.mapId))
                        {
                            const auto& map = mapStorage->Get<MetaGen::Shared::ClientDB::MapRecord>(event.mapId);
                            mapInternalName = mapStorage->GetString(map.internalName);
                        }
                    }
                }

                const char* reason = "unknown";
                switch (event.reason)
                {
                    case ECS::Components::MapLoadFailureReason::MissingDatabaseRecord: reason = "missing-database-record"; break;
                    case ECS::Components::MapLoadFailureReason::MissingHeader: reason = "missing-header"; break;
                    case ECS::Components::MapLoadFailureReason::InvalidHeader: reason = "invalid-header"; break;
                    case ECS::Components::MapLoadFailureReason::MissingBaseModel: reason = "missing-base-model"; break;
                    case ECS::Components::MapLoadFailureReason::NoChunks: reason = "no-chunks"; break;
                    case ECS::Components::MapLoadFailureReason::NoAvailableChunks: reason = "no-available-chunks"; break;
                }

                zenith->CallEvent(MetaGen::Game::Lua::GameEvent::MapLoadFailed, MetaGen::Game::Lua::GameEventDataMapLoadFailed{ .mapID = event.mapId, .mapInternalName = mapInternalName, .reason = reason });
            });
    }

    i32 GameHandler::IsLoaded(Zenith* zenith)
    {
        zenith->GetGlobalKey("Zenith");
        LuaManager* luaManager = zenith->IsLightUserData(-1)
            ? static_cast<LuaManager*>(zenith->ToLightUserData(-1))
            : nullptr;
        zenith->Pop();

        GameHandler* self = luaManager
            ? luaManager->GetLuaHandler<GameHandler>(static_cast<LuaHandlerID>(MetaGen::Game::Lua::LuaHandlerTypeEnum::Game))
            : nullptr;
        zenith->Push(self && self->_isLoaded);
        return 1;
    }
}
