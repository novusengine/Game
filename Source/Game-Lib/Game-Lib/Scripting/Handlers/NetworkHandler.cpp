#include "NetworkHandler.h"
#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Components/MovementInfo.h"
#include "Game-Lib/ECS/Components/Unit.h"
#include "Game-Lib/ECS/Components/UnitPowersComponent.h"
#include "Game-Lib/ECS/Singletons/CharacterSingleton.h"
#include "Game-Lib/ECS/Singletons/NetworkState.h"
#include "Game-Lib/ECS/Util/Network/NetworkUtil.h"
#include "Game-Lib/ECS/Util/Transforms.h"
#include "Game-Lib/Util/ServiceLocator.h"
#include "Game-Lib/Util/UnitUtil.h"

#include <Base/CVarSystem/CVarSystem.h>

#include <MetaGen/Shared/Packet/Packet.h>
#include <MetaGen/Shared/Unit/Unit.h>

#include <Network/Client.h>

#include <Scripting/LuaManager.h>
#include <Scripting/Zenith.h>

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <lualib.h>

namespace Scripting::Network
{
    void NetworkHandler::Register(Zenith* zenith)
    {
        LuaManager* luaManager = ServiceLocator::GetLuaManager();
        const bool inDeveloperMode = luaManager && luaManager->IsDeveloperMode();
        const Scripting::LuaMethodFlags excludeFlags = inDeveloperMode
            ? Scripting::LuaMethodFlags::None
            : Scripting::LuaMethodFlags::DeveloperOnly;

        LuaMethodTable::Set(zenith, networkGlobalMethods, "Network", excludeFlags);
    }

    static ECS::Singletons::NetworkState* GetNetworkState()
    {
        EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
        if (!registries || !registries->gameRegistry)
            return nullptr;
        return &registries->gameRegistry->ctx().get<ECS::Singletons::NetworkState>();
    }

    // Pushes { name, healthBase, healthCurrent, healthMax } for a unit entity, or nil if invalid.
    static void PushUnit(Zenith* zenith, entt::registry& registry, entt::entity entity)
    {
        if (entity == entt::null || !registry.valid(entity))
        {
            zenith->Push();
            return;
        }

        auto& unit = registry.get<ECS::Components::Unit>(entity);
        auto& powers = registry.get<ECS::Components::UnitPowersComponent>(entity);
        auto& health = ::Util::Unit::GetPower(powers, MetaGen::Shared::Unit::PowerTypeEnum::Health);

        zenith->CreateTable();
        zenith->AddTableField("name", unit.name.c_str());
        zenith->AddTableField("healthBase", health.base);
        zenith->AddTableField("healthCurrent", health.current);
        zenith->AddTableField("healthMax", health.max);
    }

    i32 NetworkHandler::IsConnected(Zenith* zenith)
    {
        ECS::Singletons::NetworkState* networkState = GetNetworkState();
        zenith->Push(networkState && networkState->client && networkState->client->IsConnected());
        return 1;
    }

    i32 NetworkHandler::GetPingInfo(Zenith* zenith)
    {
        ECS::Singletons::NetworkState* networkState = GetNetworkState();
        zenith->CreateTable();
        zenith->AddTableField("ping", networkState ? static_cast<u32>(networkState->pingInfo.ping) : 0u);
        zenith->AddTableField("serverUpdateDiff", networkState ? static_cast<u32>(networkState->pingInfo.serverUpdateDiff) : 0u);
        return 1;
    }

    i32 NetworkHandler::Connect(Zenith* /*zenith*/)
    {
        ECS::Singletons::NetworkState* networkState = GetNetworkState();
        if (!networkState || !networkState->client)
            return 0;

        CVarSystem* cvarSystem = CVarSystem::Get();
        const char* ip = cvarSystem->GetStringCVar(CVarCategory::Network, "connectIP"_h);
        const char* accountName = cvarSystem->GetStringCVar(CVarCategory::Network, "accountName"_h);

        if (accountName && accountName[0] != '\0')
        {
            if (networkState->client->Connect(ip, 4000))
            {
                ECS::Util::Network::SendPacket(*networkState, MetaGen::Shared::Packet::ClientConnectPacket{
                    .accountName = accountName
                });
            }
        }
        return 0;
    }

    i32 NetworkHandler::Disconnect(Zenith* /*zenith*/)
    {
        ECS::Singletons::NetworkState* networkState = GetNetworkState();
        if (networkState && networkState->client)
            networkState->client->Stop();
        return 0;
    }

    i32 NetworkHandler::GetConnectIP(Zenith* zenith)
    {
        const char* ip = CVarSystem::Get()->GetStringCVar(CVarCategory::Network, "connectIP"_h);
        zenith->Push(ip ? ip : "");
        return 1;
    }

    i32 NetworkHandler::SetConnectIP(Zenith* zenith)
    {
        if (!zenith->IsString(1))
            return 0;
        CVarSystem::Get()->SetStringCVar(CVarCategory::Network, "connectIP"_h, zenith->Get<const char*>(1));
        return 0;
    }

    i32 NetworkHandler::GetAccountName(Zenith* zenith)
    {
        const char* accountName = CVarSystem::Get()->GetStringCVar(CVarCategory::Network, "accountName"_h);
        zenith->Push(accountName ? accountName : "");
        return 1;
    }

    i32 NetworkHandler::SetAccountName(Zenith* zenith)
    {
        if (!zenith->IsString(1))
            return 0;
        CVarSystem::Get()->SetStringCVar(CVarCategory::Network, "accountName"_h, zenith->Get<const char*>(1));
        return 0;
    }

    i32 NetworkHandler::GetDrawTargetAABB(Zenith* zenith)
    {
        i32* value = CVarSystem::Get()->GetIntCVar(CVarCategory::Network, "drawTargetAABB"_h);
        zenith->Push(value && *value != 0);
        return 1;
    }

    i32 NetworkHandler::SetDrawTargetAABB(Zenith* zenith)
    {
        bool enabled = zenith->CheckVal<bool>(1);
        CVarSystem::Get()->SetIntCVar(CVarCategory::Network, "drawTargetAABB"_h, enabled ? 1 : 0);
        return 0;
    }

    // Returns nil if there is no active character, else the mover's transform/movement readout
    // (positions/vectors as flat fields; the panel formats them).
    i32 NetworkHandler::GetCharacterInfo(Zenith* zenith)
    {
        EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
        if (!registries || !registries->gameRegistry)
        {
            zenith->Push();
            return 1;
        }
        entt::registry& registry = *registries->gameRegistry;
        auto& characterSingleton = registry.ctx().get<ECS::Singletons::CharacterSingleton>();

        entt::entity mover = characterSingleton.moverEntity;
        if (mover == entt::null || !registry.valid(mover))
        {
            zenith->Push();
            return 1;
        }

        auto& transform = registry.get<ECS::Components::Transform>(mover);
        auto& movementInfo = registry.get<ECS::Components::MovementInfo>(mover);

        vec3 pos = transform.GetWorldPosition();
        vec3 forward = transform.GetLocalForward();
        vec3 right = transform.GetLocalRight();
        vec3 up = transform.GetLocalUp();

        zenith->CreateTable();
        zenith->AddTableField("posX", pos.x);
        zenith->AddTableField("posY", pos.y);
        zenith->AddTableField("posZ", pos.z);
        zenith->AddTableField("yaw", movementInfo.yaw);
        zenith->AddTableField("yawDegrees", glm::degrees(movementInfo.yaw));
        zenith->AddTableField("pitchDegrees", glm::degrees(movementInfo.pitch));
        zenith->AddTableField("speed", movementInfo.speed);
        zenith->AddTableField("forwardX", forward.x);
        zenith->AddTableField("forwardY", forward.y);
        zenith->AddTableField("forwardZ", forward.z);
        zenith->AddTableField("rightX", right.x);
        zenith->AddTableField("rightY", right.y);
        zenith->AddTableField("rightZ", right.z);
        zenith->AddTableField("upX", up.x);
        zenith->AddTableField("upY", up.y);
        zenith->AddTableField("upZ", up.z);
        return 1;
    }

    i32 NetworkHandler::GetMoverUnit(Zenith* zenith)
    {
        EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
        if (!registries || !registries->gameRegistry)
        {
            zenith->Push();
            return 1;
        }
        entt::registry& registry = *registries->gameRegistry;
        auto& characterSingleton = registry.ctx().get<ECS::Singletons::CharacterSingleton>();
        PushUnit(zenith, registry, characterSingleton.moverEntity);
        return 1;
    }

    i32 NetworkHandler::GetTargetUnit(Zenith* zenith)
    {
        EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
        if (!registries || !registries->gameRegistry)
        {
            zenith->Push();
            return 1;
        }
        entt::registry& registry = *registries->gameRegistry;
        auto& characterSingleton = registry.ctx().get<ECS::Singletons::CharacterSingleton>();

        entt::entity mover = characterSingleton.moverEntity;
        if (mover == entt::null || !registry.valid(mover))
        {
            zenith->Push();
            return 1;
        }

        auto& moverUnit = registry.get<ECS::Components::Unit>(mover);
        PushUnit(zenith, registry, moverUnit.targetEntity);
        return 1;
    }
}
