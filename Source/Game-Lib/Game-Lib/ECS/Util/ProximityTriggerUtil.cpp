#include "ProximityTriggerUtil.h"
#include "Game-Lib/ECS/Components/AABB.h"
#include "Game-Lib/ECS/Components/Name.h"
#include "Game-Lib/ECS/Components/ProximityTrigger.h"
#include "Game-Lib/ECS/Singletons/CharacterSingleton.h"
#include "Game-Lib/ECS/Singletons/ProximityTriggerSingleton.h"
#include "Game-Lib/ECS/Util/Transforms.h"
#include "Game-Lib/Scripting/LuaDefines.h"
#include "Game-Lib/Scripting/LuaManager.h"
#include "Game-Lib/Scripting/Handlers/TriggerEventHandler.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <entt/entt.hpp>

void ECS::Util::ProximityTriggerUtil::CreateTrigger(entt::registry& registry, u32 triggerID, const std::string& name, Generated::ProximityTriggerFlagEnum flags, u16 mapID, const vec3& position, const vec3& extents)
{
    entt::entity entity = registry.create();

    auto& nameComp = registry.emplace<ECS::Components::Name>(entity);
    nameComp.name = name;
    registry.emplace<ECS::Components::Transform>(entity);
    auto& aabb = registry.emplace<ECS::Components::AABB>(entity);
    aabb.centerPos = vec3(0.0f, 0.0f, 0.0f);
    aabb.extents = extents;
    registry.emplace<ECS::Components::WorldAABB>(entity);
    auto& trigger = registry.emplace<ECS::Components::ProximityTrigger>(entity);
    trigger.networkID = triggerID;
    trigger.flags = flags;

    auto& transformSystem = ECS::TransformSystem::Get(registry);
    transformSystem.SetWorldPosition(entity, position);

    auto& proximityTriggerSingleton = registry.ctx().get<ECS::Singletons::ProximityTriggerSingleton>();
    proximityTriggerSingleton.triggerIDToEntity[triggerID] = entity;
}

void ECS::Util::ProximityTriggerUtil::DestroyTrigger(entt::registry& registry, u32 triggerID)
{
    auto& ctx = registry.ctx();
    auto& proximityTriggerSingleton = ctx.get<ECS::Singletons::ProximityTriggerSingleton>();

    if (!proximityTriggerSingleton.triggerIDToEntity.contains(triggerID))
        return; // Trigger does not exist

    entt::entity triggerEntity = proximityTriggerSingleton.triggerIDToEntity[triggerID];
    if (!registry.valid(triggerEntity))
        return; // Entity is not valid

    proximityTriggerSingleton.triggerIDToEntity.erase(triggerID);
    proximityTriggerSingleton.proximityTriggers.Remove(triggerEntity);

    // Remove this trigger from all entityToProximityTriggers 
    for (auto& [_, triggers] : proximityTriggerSingleton.entityToProximityTriggers)
    {
        triggers.erase(triggerEntity);
    }

    // Send on trigger exit events
    auto& characterSingleton = ctx.get<ECS::Singletons::CharacterSingleton>();

    entt::entity playerEntity = characterSingleton.moverEntity;

    if (playerEntity == entt::null || !registry.valid(playerEntity))
    {
        registry.destroy(triggerEntity);
        return;
    }

    auto& proximityTrigger = registry.get<Components::ProximityTrigger>(triggerEntity);
    if (proximityTrigger.playersInside.contains(playerEntity) )
    {
        auto* luaManager = ServiceLocator::GetLuaManager();
        auto triggerEventHandler = luaManager->GetLuaHandler<Scripting::TriggerEventHandler*>(Scripting::LuaHandlerType::TriggerEvent);
        Scripting::LuaTriggerEventOnTriggerExitData eventData =
        {
            .triggerID = entt::to_integral(triggerEntity),
            .playerID = entt::to_integral(playerEntity)
        };
        triggerEventHandler->CallEvent(luaManager->GetInternalState(), static_cast<u32>(Generated::LuaTriggerEventEnum::OnExit), &eventData);
    }
    
    registry.destroy(triggerEntity);
}
