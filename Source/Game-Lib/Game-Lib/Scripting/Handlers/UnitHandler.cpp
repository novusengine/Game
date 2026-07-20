#include "UnitHandler.h"
#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Components/AnimationData.h"
#include "Game-Lib/ECS/Components/AttachmentData.h"
#include "Game-Lib/ECS/Components/Model.h"
#include "Game-Lib/ECS/Components/Unit.h"
#include "Game-Lib/ECS/Components/UnitAuraInfo.h"
#include "Game-Lib/ECS/Components/UnitFaction.h"
#include "Game-Lib/ECS/Components/UnitPowersComponent.h"
#include "Game-Lib/ECS/Components/UnitStatsComponent.h"
#include "Game-Lib/ECS/Components/AABB.h"
#include "Game-Lib/ECS/Singletons/CharacterSingleton.h"
#include "Game-Lib/ECS/Singletons/CharacterControllerSingleton.h"
#include "Game-Lib/ECS/Singletons/NetworkState.h"
#include "Game-Lib/ECS/Systems/CharacterControllerInput.h"
#include "Game-Lib/ECS/Util/FactionUtil.h"
#include "Game-Lib/ECS/Util/Network/NetworkUtil.h"
#include "Game-Lib/ECS/Util/Transforms.h"
#include "Game-Lib/ECS/Util/UIUtil.h"
#include "Game-Lib/Scripting/UI/Widget.h"
#include "Game-Lib/Util/AttachmentUtil.h"
#include "Game-Lib/Util/ServiceLocator.h"
#include "Game-Lib/Util/UnitUtil.h"

#include <Gameplay/ECS/Components/UnitFields.h>

#include <MetaGen/Game/Lua/Lua.h>
#include <MetaGen/Shared/Unit/Unit.h>

#include <Scripting/Zenith.h>

#include <lualib.h>
#include <entt/entt.hpp>

#include <format>
#include <unordered_set>

namespace Scripting::Unit
{
    void UnitHandler::Register(Zenith* zenith)
    {
        LuaMethodTable::Set(zenith, unitGlobalMethods, "Unit");

        zenith->AddGlobalField("INVALID_UNIT_ID", std::numeric_limits<entt::id_type>().max());

        // PowerType Enum
        {
            zenith->CreateTable(MetaGen::Shared::Unit::PowerTypeEnumMeta::ENUM_NAME.data());

            for (const auto& pair : MetaGen::Shared::Unit::PowerTypeEnumMeta::ENUM_FIELD_LIST)
            {
                zenith->AddTableField(pair.first.data(), pair.second);
            }

            zenith->Pop();
        }

        // StatType Enum
        {
            zenith->CreateTable(MetaGen::Shared::Unit::StatTypeEnumMeta::ENUM_NAME.data());

            for (const auto& pair : MetaGen::Shared::Unit::StatTypeEnumMeta::ENUM_FIELD_LIST)
            {
                zenith->AddTableField(pair.first.data(), pair.second);
            }

            zenith->Pop();
        }

        zenith->CreateTable("FactionReaction");
        zenith->AddTableField("Hostile", static_cast<u8>(Gameplay::Faction::Reaction::Hostile));
        zenith->AddTableField("Unfriendly", static_cast<u8>(Gameplay::Faction::Reaction::Unfriendly));
        zenith->AddTableField("Neutral", static_cast<u8>(Gameplay::Faction::Reaction::Neutral));
        zenith->AddTableField("Friendly", static_cast<u8>(Gameplay::Faction::Reaction::Friendly));
        zenith->Pop();
    }

    void UnitHandler::PostLoad(Zenith* zenith)
    {
        entt::registry* gameRegistry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& networkState = gameRegistry->ctx().get<ECS::Singletons::NetworkState>();
        auto& characterSingleton = gameRegistry->ctx().get<ECS::Singletons::CharacterSingleton>();

        // Resend created events for all existing networked units
        {
            vec3 minBounds = vec3(-1000000.0f);
            vec3 maxBounds = vec3(1000000.0f);
            std::unordered_set<entt::id_type> replayedUnitIDs;

            networkState.networkVisTree->Search(&minBounds.x, &maxBounds.x, [&](const ObjectGUID objectGUID)
            {
                entt::entity entity = entt::null;
                if (!::ECS::Util::Network::GetEntityIDFromObjectGUID(networkState, objectGUID, entity))
                    return true;

                const entt::id_type unitID = entt::to_integral(entity);
                if (!replayedUnitIDs.insert(unitID).second)
                    return true;

                zenith->CallEvent(MetaGen::Game::Lua::UnitEvent::Add, MetaGen::Game::Lua::UnitEventDataAdd{
                    .unitID = unitID
                });

                return true;
            });
        }

        // Resend LocalMoverChanged
        zenith->CallEvent(MetaGen::Game::Lua::GameEvent::LocalMoverChanged, MetaGen::Game::Lua::GameEventDataLocalMoverChanged{
            .moverID = entt::to_integral(characterSingleton.moverEntity)
        });

        if (gameRegistry->valid(characterSingleton.moverEntity))
        {
            if (auto* unit = gameRegistry->try_get<ECS::Components::Unit>(characterSingleton.moverEntity))
            {
                zenith->CallEvent(MetaGen::Game::Lua::UnitEvent::TargetChanged, MetaGen::Game::Lua::UnitEventDataTargetChanged{
                    .unitID = entt::to_integral(characterSingleton.moverEntity),
                    .targetID = entt::to_integral(unit->targetEntity)
                });
            }
        }
    }

    i32 UnitHandler::GetLocal(Zenith* zenith)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& characterSingleton = registry->ctx().get<ECS::Singletons::CharacterSingleton>();

        u32 localID = std::numeric_limits<entt::id_type>().max();

        if (registry->valid(characterSingleton.moverEntity))
            localID = entt::to_integral(characterSingleton.moverEntity);

        zenith->Push(localID);
        return 1;
    }

    i32 UnitHandler::GetHovered(Zenith* zenith)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        entt::id_type unitID = std::numeric_limits<entt::id_type>().max();
        const auto* controllerState = registry->ctx().find<ECS::Singletons::CharacterControllerSingleton>();
        if (controllerState
            && registry->valid(controllerState->hoveredEntity)
            && registry->all_of<ECS::Components::Unit>(controllerState->hoveredEntity))
            unitID = entt::to_integral(controllerState->hoveredEntity);

        zenith->Push(unitID);
        return 1;
    }

    i32 UnitHandler::ClearTarget(Zenith* zenith)
    {
        zenith->Push(ECS::Systems::CharacterControllerInput::ClearTarget());
        return 1;
    }

    i32 UnitHandler::GetName(Zenith* zenith)
    {
        u32 unitID = zenith->CheckVal<u32>(1);
        if (unitID == std::numeric_limits<u32>().max())
            return 0;

        entt::entity entityID = entt::entity(unitID);

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        if (!registry->valid(entityID))
            return 0;

        if (!registry->all_of<ECS::Components::Unit>(entityID))
            return 0;

        auto& unit = registry->get<ECS::Components::Unit>(entityID);
        if (unit.name.empty())
            return 0;

        zenith->Push(unit.name.c_str());
        return 1;
    }

    i32 UnitHandler::GetHealth(Zenith* zenith)
    {
        u32 unitID = zenith->CheckVal<u32>(1);
        entt::entity entityID = entt::entity(unitID);

        f64 currentHealth = 0.0f;
        f64 maxHealth = 1.0f;

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        if (registry->valid(entityID))
        {
            if (auto* unitPowersComponent = registry->try_get<ECS::Components::UnitPowersComponent>(entityID))
            {
                if (::Util::Unit::HasPower(*unitPowersComponent, MetaGen::Shared::Unit::PowerTypeEnum::Health))
                {
                    auto& healthPower = ::Util::Unit::GetPower(*unitPowersComponent, MetaGen::Shared::Unit::PowerTypeEnum::Health);
                    currentHealth = healthPower.current;
                    maxHealth = healthPower.max;
                }
            }
        }

        zenith->Push(currentHealth);
        zenith->Push(maxHealth);
        return 2;
    }

    i32 UnitHandler::GetLevel(Zenith* zenith)
    {
        u32 unitID = zenith->CheckVal<u32>(1);
        entt::entity entityID = entt::entity(unitID);

        u16 level = 0;

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        if (registry->valid(entityID))
        {
            if (auto* unitFields = registry->try_get<ECS::Components::UnitFields>(entityID))
            {
                level = unitFields->fields.GetField<u16>(MetaGen::Shared::NetField::UnitNetFieldEnum::LevelRaceGenderClassPacked);
            }
        }

        zenith->Push(level);
        return 1;
    }

    i32 UnitHandler::GetClass(Zenith* zenith)
    {
        u32 unitID = zenith->CheckVal<u32>(1);
        entt::entity entityID = entt::entity(unitID);

        GameDefine::UnitClass unitClass = GameDefine::UnitClass::Warrior;

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        if (registry->valid(entityID))
        {
            if (auto* unit = registry->try_get<ECS::Components::Unit>(entityID))
            {
                unitClass = unit->unitClass;
            }
        }

        u32 value = static_cast<u32>(unitClass);
        zenith->Push(value);
        return 1;
    }

    i32 UnitHandler::GetResourceType(Zenith* zenith)
    {
        u32 unitID = zenith->CheckVal<u32>(1);
        entt::entity entityID = entt::entity(unitID);

        MetaGen::Shared::Unit::PowerTypeEnum resourceType = MetaGen::Shared::Unit::PowerTypeEnum::Mana;

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        if (registry->valid(entityID))
        {
            if (auto* unit = registry->try_get<ECS::Components::Unit>(entityID))
            {
                switch (unit->unitClass)
                {
                    case GameDefine::UnitClass::Warrior:
                    {
                        resourceType = MetaGen::Shared::Unit::PowerTypeEnum::Rage;
                        break;
                    }

                    case GameDefine::UnitClass::Paladin:
                    case GameDefine::UnitClass::Priest:
                    case GameDefine::UnitClass::Shaman:
                    case GameDefine::UnitClass::Mage:
                    case GameDefine::UnitClass::Warlock:
                    case GameDefine::UnitClass::Druid:
                    {
                        resourceType = MetaGen::Shared::Unit::PowerTypeEnum::Mana;
                        break;
                    }

                    case GameDefine::UnitClass::Hunter:
                    {
                        resourceType = MetaGen::Shared::Unit::PowerTypeEnum::Focus;
                        break;
                    }

                    case GameDefine::UnitClass::Rogue:
                    {
                        resourceType = MetaGen::Shared::Unit::PowerTypeEnum::Energy;
                        break;
                    }
                }
            }
        }

        u32 resource = static_cast<u32>(resourceType);
        zenith->Push(resource);
        return 1;
    }

    i32 UnitHandler::GetResource(Zenith* zenith)
    {
        u32 unitID = zenith->CheckVal<u32>(1);
        entt::entity entityID = entt::entity(unitID);

        MetaGen::Shared::Unit::PowerTypeEnum resourceType = static_cast<MetaGen::Shared::Unit::PowerTypeEnum>(zenith->Get<u32>(2));
        if (resourceType <= MetaGen::Shared::Unit::PowerTypeEnum::Invalid || resourceType >= MetaGen::Shared::Unit::PowerTypeEnum::Count)
            resourceType = MetaGen::Shared::Unit::PowerTypeEnum::Mana;

        f64 currentResource = 0.0f;
        f64 maxResource = 1.0f;

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        if (registry->valid(entityID))
        {
            if (auto* unitPowersComponent = registry->try_get<ECS::Components::UnitPowersComponent>(entityID))
            {
                if (::Util::Unit::HasPower(*unitPowersComponent, resourceType))
                {
                    auto& power = ::Util::Unit::GetPower(*unitPowersComponent, resourceType);
                    currentResource = power.current;
                    maxResource = power.max;
                }
            }
        }

        zenith->Push(currentResource);
        zenith->Push(maxResource);
        return 2;
    }

    i32 UnitHandler::GetStat(Zenith* zenith)
    {
        u32 unitID = zenith->CheckVal<u32>(1);
        entt::entity entityID = entt::entity(unitID);

        MetaGen::Shared::Unit::StatTypeEnum statType = static_cast<MetaGen::Shared::Unit::StatTypeEnum>(zenith->Get<u32>(2));
        if (statType <= MetaGen::Shared::Unit::StatTypeEnum::Invalid || statType >= MetaGen::Shared::Unit::StatTypeEnum::Count)
            return 0;

        f64 currentStat = 0.0f;
        f64 baseStat = 0.0f;

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        if (registry->valid(entityID))
        {
            if (auto* unitStatsComponent = registry->try_get<ECS::Components::UnitStatsComponent>(entityID))
            {
                if (::Util::Unit::HasStat(*unitStatsComponent, statType))
                {
                    ECS::UnitStat& stat = ::Util::Unit::GetStat(*unitStatsComponent, statType);
                    currentStat = stat.current;
                    baseStat = stat.base;
                }
            }
        }

        zenith->Push(currentStat);
        zenith->Push(baseStat);
        return 2;
    }

    i32 UnitHandler::GetAuras(Zenith* zenith)
    {
        u32 unitID = zenith->CheckVal<u32>(1);
        entt::entity entityID = entt::entity(unitID);

        zenith->CreateTable();

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        if (!registry->valid(entityID))
            return 1;

        if (auto* unitAuraInfo = registry->try_get<ECS::Components::UnitAuraInfo>(entityID))
        {
            u32 index = 1;
            for (const auto& auraInfo : unitAuraInfo->auras)
            {
                u64 currentTime = static_cast<u64>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
                f32 duration = auraInfo.expireTimestamp > currentTime ? static_cast<f32>(auraInfo.expireTimestamp - currentTime) / 1000.0f : 0.0f;

                zenith->CreateTable();
                zenith->AddTableField("auraID", auraInfo.auraID);
                zenith->AddTableField("spellID", auraInfo.spellID);
                zenith->AddTableField("duration", duration);
                zenith->AddTableField("stacks", auraInfo.stacks);

                zenith->SetTableKey(index++);
            }
        }

        return 1;
    }

    i32 UnitHandler::GetFactionID(Zenith* zenith)
    {
        const entt::entity entity = static_cast<entt::entity>(zenith->CheckVal<u32>(1));
        Gameplay::Faction::FactionID factionID = Gameplay::Faction::NONE_FACTION_ID;

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        if (registry->valid(entity))
        {
            if (const auto* unitFaction = registry->try_get<ECS::Components::UnitFaction>(entity))
                factionID = unitFaction->factionID;
        }

        zenith->Push(factionID);
        return 1;
    }

    i32 UnitHandler::GetReaction(Zenith* zenith)
    {
        const entt::entity entity = static_cast<entt::entity>(zenith->CheckVal<u32>(1));
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        zenith->Push(static_cast<u8>(ECS::Util::Faction::GetPresentationReaction(*registry, entity)));
        return 1;
    }

    i32 UnitHandler::CanAttack(Zenith* zenith)
    {
        const entt::entity entity = static_cast<entt::entity>(zenith->CheckVal<u32>(1));
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        zenith->Push(ECS::Util::Faction::CanAttack(*registry, entity));
        return 1;
    }

    i32 UnitHandler::GetLocalReactionToUnit(Zenith* zenith)
    {
        const entt::entity entity = static_cast<entt::entity>(zenith->CheckVal<u32>(1));
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        zenith->Push(static_cast<u8>(ECS::Util::Faction::GetLocalReactionToUnit(*registry, entity)));
        return 1;
    }

    i32 UnitHandler::GetUnitReactionToLocalPlayer(Zenith* zenith)
    {
        const entt::entity entity = static_cast<entt::entity>(zenith->CheckVal<u32>(1));
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        zenith->Push(static_cast<u8>(ECS::Util::Faction::GetUnitReactionToLocalPlayer(*registry, entity)));
        return 1;
    }

    i32 UnitHandler::GetPersistentReputation(Zenith* zenith)
    {
        const auto factionID = zenith->CheckVal<Gameplay::Faction::FactionID>(1);
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;

        Gameplay::Faction::ReputationState reputation;
        const bool isPresent = ECS::Util::Faction::FindReputation(*registry, factionID, reputation);
        const Gameplay::Faction::StandingThreshold* standing = ECS::Util::Faction::GetPersistentStanding(*registry, factionID);

        zenith->Push(isPresent ? reputation.value : 0);
        zenith->Push(standing ? standing->id : 0);
        zenith->Push(isPresent ? reputation.flags : 0);
        zenith->Push(isPresent);
        return 4;
    }

    i32 UnitHandler::GetPersistentStanding(Zenith* zenith)
    {
        const auto factionID = zenith->CheckVal<Gameplay::Faction::FactionID>(1);
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        const Gameplay::Faction::StandingThreshold* standing = ECS::Util::Faction::GetPersistentStanding(*registry, factionID);
        zenith->Push(standing ? standing->id : 0);
        return 1;
    }

    i32 UnitHandler::GetEffectiveStanding(Zenith* zenith)
    {
        const auto factionID = zenith->CheckVal<Gameplay::Faction::FactionID>(1);
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        const Gameplay::Faction::StandingThreshold* standing = ECS::Util::Faction::GetEffectiveStanding(*registry, factionID);
        zenith->Push(standing ? standing->id : 0);
        return 1;
    }

    i32 UnitHandler::SetWidgetToNamePos(Zenith* zenith)
    {
        auto* widget = zenith->GetUserData<UI::Widget>(1);
        if (widget == nullptr)
        {
            luaL_error(zenith->state, "Widget is null");
        }

        u32 unitID = zenith->CheckVal<u32>(2);
        auto pushFailure = [zenith]() -> i32
        {
            zenith->Push(0);
            return 1;
        };

        if (unitID == std::numeric_limits<u32>().max())
            return pushFailure();

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        entt::entity entityID = entt::entity(unitID);

        if (!registry->valid(entityID) || !registry->all_of<ECS::Components::AttachmentData>(entityID))
            return pushFailure();

        auto& model = registry->get<ECS::Components::Model>(entityID);
        if (!model.flags.loaded || !model.flags.visible || model.opacity == 0)
            return pushFailure();

        const auto& transform = registry->get<ECS::Components::Transform>(entityID);
        vec3 position = transform.GetWorldPosition();

        auto* animationData = registry->try_get<ECS::Components::AnimationData>(entityID);
        auto& attachmentData = registry->get<ECS::Components::AttachmentData>(entityID);
        bool hasNameAttachmentPosition = false;
        if (animationData && Util::Attachment::EnableAttachment(entityID, model, attachmentData, *animationData, Attachment::Defines::Type::PlayerName))
        {
            const mat4x4* mat = Util::Attachment::GetAttachmentMatrix(model, *animationData, attachmentData, Attachment::Defines::Type::PlayerName);
            auto attachmentIt = attachmentData.attachmentToInstance.find(Attachment::Defines::Type::PlayerName);
            if (mat && attachmentIt != attachmentData.attachmentToInstance.end() && registry->valid(attachmentIt->second.entity))
            {
                const auto& attachmentTransform = registry->get<ECS::Components::Transform>(attachmentIt->second.entity);
                position = attachmentTransform.GetWorldPosition();
                hasNameAttachmentPosition = true;
            }
        }

        if (!hasNameAttachmentPosition)
        {
            auto& aabb = registry->get<ECS::Components::AABB>(entityID);

            vec3 centerPos = transform.GetWorldRotation() * (aabb.centerPos * transform.GetLocalScale());
            vec3 extents = aabb.extents * transform.GetLocalScale();

            position += centerPos;
            position.y += extents.y * 1.25f; // Above head
        }

        ECS::Util::UI::SetPos3D(widget, position);

        zenith->Push(1);
        return 1;
    }
}
