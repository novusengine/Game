#include "UnitHandler.h"
#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Components/AnimationData.h"
#include "Game-Lib/ECS/Components/AttachmentData.h"
#include "Game-Lib/ECS/Components/Camera.h"
#include "Game-Lib/ECS/Components/Model.h"
#include "Game-Lib/ECS/Components/Unit.h"
#include "Game-Lib/ECS/Components/UnitAuraInfo.h"
#include "Game-Lib/ECS/Components/UnitPowersComponent.h"
#include "Game-Lib/ECS/Components/UnitStatsComponent.h"
#include "Game-Lib/ECS/Components/AABB.h"
#include "Game-Lib/ECS/Components/UI/TextTemplate.h"
#include "Game-Lib/ECS/Components/UI/Widget.h"
#include "Game-Lib/ECS/Singletons/ActiveCamera.h"
#include "Game-Lib/ECS/Singletons/CharacterSingleton.h"
#include "Game-Lib/ECS/Singletons/NetworkState.h"
#include "Game-Lib/ECS/Util/Network/NetworkUtil.h"
#include "Game-Lib/ECS/Util/Transforms.h"
#include "Game-Lib/ECS/Util/UIUtil.h"
#include "Game-Lib/Scripting/UI/Widget.h"
#include "Game-Lib/Util/AttachmentUtil.h"
#include "Game-Lib/Util/ServiceLocator.h"
#include "Game-Lib/Util/UnitUtil.h"

#include <Meta/Generated/Game/LuaEvent.h>
#include <Meta/Generated/Shared/UnitEnum.h>

#include <Scripting/Zenith.h>

#include <lualib.h>
#include <entt/entt.hpp>

#include <format>

namespace Scripting::Unit
{
    void UnitHandler::Register(Zenith* zenith)
    {
        LuaMethodTable::Set(zenith, unitGlobalMethods, "Unit");

        zenith->AddGlobalField("INVALID_UNIT_ID", std::numeric_limits<entt::id_type>().max());

        // PowerType Enum
        {
            zenith->CreateTable(Generated::PowerTypeEnumMeta::EnumName.data());

            for (const auto& pair : Generated::PowerTypeEnumMeta::EnumList)
            {
                zenith->AddTableField(pair.first.data(), pair.second);
            }

            zenith->Pop();
        }

        // StatType Enum
        {
            zenith->CreateTable(Generated::StatTypeEnumMeta::EnumName.data());

            for (const auto& pair : Generated::StatTypeEnumMeta::EnumList)
            {
                zenith->AddTableField(pair.first.data(), pair.second);
            }

            zenith->Pop();
        }
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

            networkState.networkVisTree->Search(&minBounds.x, &maxBounds.x, [&](const ObjectGUID objectGUID)
            {
                entt::entity entity = entt::null;
                if (!::ECS::Util::Network::GetEntityIDFromObjectGUID(networkState, objectGUID, entity))
                    return true;

                zenith->CallEvent(Generated::LuaUnitEventEnum::Add, Generated::LuaUnitEventDataAdd{
                    .unitID = entt::to_integral(entity)
                });

                return true;
            });
        }

        // Resend LocalMoverChanged
        zenith->CallEvent(Generated::LuaGameEventEnum::LocalMoverChanged, Generated::LuaGameEventDataLocalMoverChanged{
            .moverID = entt::to_integral(characterSingleton.moverEntity)
        });

        if (gameRegistry->valid(characterSingleton.moverEntity))
        {
            if (auto* unit = gameRegistry->try_get<ECS::Components::Unit>(characterSingleton.moverEntity))
            {
                zenith->CallEvent(Generated::LuaUnitEventEnum::TargetChanged, Generated::LuaUnitEventDataTargetChanged{
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
                auto& healthPower = ::Util::Unit::GetPower(*unitPowersComponent, Generated::PowerTypeEnum::Health);
                currentHealth = healthPower.current;
                maxHealth = healthPower.max;
            }
        }

        zenith->Push(currentHealth);
        zenith->Push(maxHealth);
        return 2;
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

        Generated::PowerTypeEnum resourceType = Generated::PowerTypeEnum::Mana;

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        if (registry->valid(entityID))
        {
            if (auto* unit = registry->try_get<ECS::Components::Unit>(entityID))
            {
                switch (unit->unitClass)
                {
                    case GameDefine::UnitClass::Warrior:
                    {
                        resourceType = Generated::PowerTypeEnum::Rage;
                        break;
                    }

                    case GameDefine::UnitClass::Paladin:
                    case GameDefine::UnitClass::Priest:
                    case GameDefine::UnitClass::Shaman:
                    case GameDefine::UnitClass::Mage:
                    case GameDefine::UnitClass::Warlock:
                    case GameDefine::UnitClass::Druid:
                    {
                        resourceType = Generated::PowerTypeEnum::Mana;
                        break;
                    }

                    case GameDefine::UnitClass::Hunter:
                    {
                        resourceType = Generated::PowerTypeEnum::Focus;
                        break;
                    }

                    case GameDefine::UnitClass::Rogue:
                    {
                        resourceType = Generated::PowerTypeEnum::Energy;
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

        Generated::PowerTypeEnum resourceType = static_cast<Generated::PowerTypeEnum>(zenith->Get<u32>(2));
        if (resourceType <= Generated::PowerTypeEnum::Invalid || resourceType >= Generated::PowerTypeEnum::Count)
            resourceType = Generated::PowerTypeEnum::Mana;

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

        Generated::StatTypeEnum statType = static_cast<Generated::StatTypeEnum>(zenith->Get<u32>(2));
        if (statType <= Generated::StatTypeEnum::Invalid || statType >= Generated::StatTypeEnum::Count)
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

    f32 GetNameFontSize(f32 distance)
    {
        const u32 maxSize = 14; // size when right in front
        const u32 minSize = 6; // never below this
        const f32 maxDistance = 80.0f * 80.0f; // beyond this, always min size

        f32 t = glm::clamp(distance / maxDistance, 0.0f, 1.0f);

        f32 curve = glm::pow(t, 5.0f);
        f32 size = maxSize - (maxSize - minSize) * curve;

        return size;
    }

    i32 UnitHandler::SetWidgetToNamePos(Zenith* zenith)
    {
        auto* widget = zenith->GetUserData<UI::Widget>(1);
        if (widget == nullptr)
        {
            luaL_error(zenith->state, "Widget is null");
        }

        u32 unitID = zenith->CheckVal<u32>(2);
        if (unitID == std::numeric_limits<u32>().max())
            return 0;

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        entt::entity entityID = entt::entity(unitID);

        if (!registry->valid(entityID) || !registry->all_of<ECS::Components::AttachmentData>(entityID))
            return 0;

        auto& model = registry->get<ECS::Components::Model>(entityID);
        if (!model.flags.loaded || !model.flags.visible || model.opacity == 0)
            return 0;

        auto& activeCameraSingleton = registry->ctx().get<ECS::Singletons::ActiveCamera>();
        if (activeCameraSingleton.entity == entt::null)
            return 0;

        if (!registry->all_of<ECS::Components::DirtyTransform>(activeCameraSingleton.entity) && !registry->all_of<ECS::Components::DirtyTransform>(entityID))
        {
            zenith->Push(2);
            return 1;
        }

        auto& cameraTransform = registry->get<ECS::Components::Transform>(activeCameraSingleton.entity);
        const auto& transform = registry->get<ECS::Components::Transform>(entityID);

        vec3 forwardDir = cameraTransform.GetLocalForward();
        vec3 cameraPosition = cameraTransform.GetWorldPosition();
        vec3 unitPosition = transform.GetWorldPosition();
        vec3 cameraToUnitDir = glm::normalize(unitPosition - cameraPosition);

        // If the unit is behind the camera, we don't want to set the widget position
        if (glm::dot(forwardDir, cameraToUnitDir) < 0.0f)
            return 0;

        // Update Font Size based on distance to camera
        bool isTextWidget = widget->type == Scripting::UI::WidgetType::Text;
        if (isTextWidget)
        {
            f32 distanceToCamera = glm::distance2(cameraPosition, unitPosition);
            if (distanceToCamera > (100.0f * 100.0f))
                return 0;

            entt::registry* uiRegistry = ServiceLocator::GetEnttRegistries()->uiRegistry;
            auto& textTemplate = uiRegistry->get<ECS::Components::UI::TextTemplate>(widget->entity);
            textTemplate.size = GetNameFontSize(distanceToCamera);

            uiRegistry->emplace_or_replace<ECS::Components::UI::DirtyWidgetTransform>(widget->entity);
            ECS::Util::UI::RefreshClipper(uiRegistry, widget->entity);
        }

        vec3 position = unitPosition;

        auto* animationData = registry->try_get<ECS::Components::AnimationData>(entityID);
        auto& attachmentData = registry->get<ECS::Components::AttachmentData>(entityID);
        if (animationData && Util::Attachment::EnableAttachment(entityID, model, attachmentData, *animationData, Attachment::Defines::Type::PlayerName))
        {
            const mat4x4* mat = Util::Attachment::GetAttachmentMatrix(model, *animationData, attachmentData, Attachment::Defines::Type::PlayerName);
            position += vec3((*mat)[3]);
        }
        else
        {
            auto& aabb = registry->get<ECS::Components::AABB>(entityID);

            vec3 centerPos = aabb.centerPos * model.scale;
            vec3 extents = aabb.extents * model.scale;

            position += centerPos;
            position.y += extents.y * 1.25f; // Above head
        }

        ECS::Util::UI::SetPos3D(widget, position);

        zenith->Push(1);
        return 1;
    }
}
