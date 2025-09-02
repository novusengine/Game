#include "UpdateProximityTriggers.h"

#include "Game-Lib/ECS/Components/AABB.h"
#include "Game-Lib/ECS/Components/Events.h"
#include "Game-Lib/ECS/Components/Name.h"
#include "Game-Lib/ECS/Components/ProximityTrigger.h"
#include "Game-Lib/ECS/Components/Tags.h"
#include "Game-Lib/ECS/Singletons/CharacterSingleton.h"
#include "Game-Lib/ECS/Singletons/NetworkState.h"
#include "Game-Lib/ECS/Singletons/ProximityTriggerSingleton.h"
#include "Game-Lib/ECS/Util/EventUtil.h"
#include "Game-Lib/ECS/Util/MessageBuilderUtil.h"
#include "Game-Lib/ECS/Util/Transforms.h"
#include "Game-Lib/ECS/Util/Network/NetworkUtil.h"
#include "Game-Lib/Scripting/LuaDefines.h"
#include "Game-Lib/Scripting/LuaManager.h"
#include "Game-Lib/Scripting/Handlers/TriggerEventHandler.h"

#include <Base/Memory/Bytebuffer.h>
#include <Base/Util/DebugHandler.h>

#include <Meta/Generated/Shared/NetworkPacket.h>
#include <Meta/Generated/Shared/ProximityTriggerEnum.h>

#include <Network/Client.h>

#include <entt/entt.hpp>
#include <tracy/Tracy.hpp>


namespace ECS::Systems
{
    bool IsTransformInAABB(const Components::Transform& transform, const Components::WorldAABB& aabb)
    {
        const vec3& p = transform.GetWorldPosition();

        // Center/half-extents form reduces comparisons against two bounds to one abs compare per axis.
        const vec3 center = (aabb.min + aabb.max) * 0.5f;
        const vec3 half = (aabb.max - aabb.min) * 0.5f;

        const vec3 d = glm::abs(p - center); // component-wise abs
        return (d.x <= half.x) && (d.y <= half.y) && (d.z <= half.z);
    }

    void OnEnter(Scripting::TriggerEventHandler* triggerEventHandler, Scripting::LuaManager* luaManager, ECS::Singletons::NetworkState& networkState, entt::entity triggerEntity, Components::ProximityTrigger& trigger, entt::entity playerEntity)
    {
        // This is an optimization so the server doesn't need to repeatedly test all triggers for all players
        if ((trigger.flags & Generated::ProximityTriggerFlagEnum::IsServerAuthorative) != Generated::ProximityTriggerFlagEnum::None)
        {
            // Serverside event for sure
            Util::Network::SendPacket(networkState, Generated::ClientTriggerEnterPacket{
                .triggerID = trigger.networkID
            });
        }
        
        // Clientside event
        Scripting::LuaTriggerEventOnTriggerEnterData eventData =
        {
            .triggerID = entt::to_integral(triggerEntity),
            .playerID = entt::to_integral(playerEntity)
        };
        triggerEventHandler->CallEvent(luaManager->GetInternalState(), static_cast<u32>(Generated::LuaTriggerEventEnum::OnEnter), &eventData);
    }

    void OnExit(Scripting::TriggerEventHandler* triggerEventHandler, Scripting::LuaManager* luaManager, ECS::Singletons::NetworkState& networkState, entt::entity triggerEntity, Components::ProximityTrigger& trigger, entt::entity playerEntity)
    {
        Scripting::LuaTriggerEventOnTriggerExitData eventData =
        {
            .triggerID = entt::to_integral(triggerEntity),
            .playerID = entt::to_integral(playerEntity)
        };
        triggerEventHandler->CallEvent(luaManager->GetInternalState(), static_cast<u32>(Generated::LuaTriggerEventEnum::OnExit), &eventData);
    }

    void OnStay(Scripting::TriggerEventHandler* triggerEventHandler, Scripting::LuaManager* luaManager, ECS::Singletons::NetworkState& networkState, entt::entity triggerEntity, Components::ProximityTrigger& trigger, entt::entity playerEntity)
    {
        Scripting::LuaTriggerEventOnTriggerStayData eventData =
        {
            .triggerID = entt::to_integral(triggerEntity),
            .playerID = entt::to_integral(playerEntity)
        };
        triggerEventHandler->CallEvent(luaManager->GetInternalState(), static_cast<u32>(Generated::LuaTriggerEventEnum::OnStay), &eventData);
    }

    void UpdateProximityTriggers::Update(entt::registry& registry, f32 deltaTime)
    {
        ZoneScopedN("ECS::ProximityTriggers");

        entt::registry::context& ctx = registry.ctx();
        auto& proximityTriggerSingleton = ctx.get<ECS::Singletons::ProximityTriggerSingleton>();

        auto view = registry.view<Components::Transform, Components::WorldAABB, Components::DirtyTransform, Components::LocalPlayerTag>();
        auto dirtyTriggerView = registry.view<Components::WorldAABB, Components::ProximityTrigger, Components::DirtyTransform>();

        // Update all dirty triggers in the RTree
        dirtyTriggerView.each([&](entt::entity triggerEntity, Components::WorldAABB& triggerAABB, Components::ProximityTrigger& proximityTrigger, Components::DirtyTransform&)
        {
            proximityTriggerSingleton.proximityTriggers.Remove(triggerEntity);
            proximityTriggerSingleton.proximityTriggers.Insert(reinterpret_cast<f32*>(&triggerAABB.min), reinterpret_cast<f32*>(&triggerAABB.max), triggerEntity);
        });

        auto& characterSingleton = ctx.get<ECS::Singletons::CharacterSingleton>();

        entt::entity playerEntity = characterSingleton.moverEntity;

        if (playerEntity == entt::null || !registry.valid(playerEntity))
        {
            return;
        }

        auto* luaManager = ServiceLocator::GetLuaManager();
        auto* triggerEventHandler = luaManager->GetLuaHandler<Scripting::TriggerEventHandler*>(Scripting::LuaHandlerType::TriggerEvent);
        auto& playerAABB = registry.get<Components::WorldAABB>(playerEntity);
        auto& networkState = ctx.get<ECS::Singletons::NetworkState>();

        // Get a COPY of the set of proximity triggers this player is inside of
        auto& triggerList = proximityTriggerSingleton.entityToProximityTriggers[playerEntity];
        auto previousTriggerList = triggerList;

        proximityTriggerSingleton.proximityTriggers.Search(reinterpret_cast<f32*>(&playerAABB.min), reinterpret_cast<f32*>(&playerAABB.max), [&](const entt::entity triggerEntity) -> bool
        {
            auto& proximityTrigger = registry.get<Components::ProximityTrigger>(triggerEntity);
            auto& playersInTrigger = proximityTrigger.playersInside;
                
            bool wasInside = proximityTrigger.playersInside.contains(playerEntity);
            if (wasInside)
            {
                OnStay(triggerEventHandler, luaManager, networkState, triggerEntity, proximityTrigger, playerEntity);
            }
            else
            {
                OnEnter(triggerEventHandler, luaManager, networkState, triggerEntity, proximityTrigger, playerEntity);

                proximityTrigger.playersInside.insert(playerEntity);
                triggerList.insert(triggerEntity);
            }

            // Remove the trigger from the set of triggers exited
            previousTriggerList.erase(triggerEntity);
            return true;
        });

        // Now we have a set of triggers that the player was inside of, but is no longer
        for (const auto triggerEntity : previousTriggerList)
        {
            auto& proximityTrigger = registry.get<Components::ProximityTrigger>(triggerEntity);

            OnExit(triggerEventHandler, luaManager, networkState, triggerEntity, proximityTrigger, playerEntity);

            proximityTrigger.playersInside.erase(playerEntity);
            triggerList.erase(triggerEntity);
        }
    }
}