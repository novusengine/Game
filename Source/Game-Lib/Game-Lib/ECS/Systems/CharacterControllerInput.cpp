#include "CharacterControllerInput.h"

#include "Game-Lib/ECS/Components/AABB.h"
#include "Game-Lib/ECS/Components/Camera.h"
#include "Game-Lib/ECS/Components/Model.h"
#include "Game-Lib/ECS/Components/Unit.h"
#include "Game-Lib/ECS/Components/UnitEquipment.h"
#include "Game-Lib/ECS/Components/UnitPowersComponent.h"
#include "Game-Lib/ECS/Singletons/ActiveCamera.h"
#include "Game-Lib/ECS/Singletons/CharacterSingleton.h"
#include "Game-Lib/ECS/Singletons/CharacterControllerSingleton.h"
#include "Game-Lib/ECS/Singletons/NetworkState.h"
#include "Game-Lib/ECS/Singletons/OrbitalCameraSettings.h"
#include "Game-Lib/ECS/Singletons/UISingleton.h"
#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/ECS/Util/Network/NetworkUtil.h"
#include "Game-Lib/ECS/Util/Transforms.h"
#include "Game-Lib/Editor/EditorHandler.h"
#include "Game-Lib/Editor/Viewport.h"
#include "Game-Lib/Gameplay/Database/Item.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Model/ModelLoader.h"
#include "Game-Lib/Scripting/Util/ZenithUtil.h"
#include "Game-Lib/Util/AnimationUtil.h"
#include "Game-Lib/Util/ServiceLocator.h"
#include "Game-Lib/Util/UnitUtil.h"

#include <MetaGen/Game/Lua/Lua.h>
#include <MetaGen/Shared/Packet/Packet.h>

#include <Input/InputManager.h>

#include <Renderer/Renderer.h>

#include <Scripting/Zenith.h>

#include <entt/entt.hpp>
#include <GLFW/glfw3.h>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <imgui.h>

#include <algorithm>
#include <limits>

namespace
{
    struct Ray
    {
    public:
        vec3 origin;
        vec3 dir;
        f32 length;
    };

    vec3 UnprojectNDC(const vec3& ndc, const mat4x4& invViewProj)
    {
        const vec4 world = invViewProj * vec4(ndc, 1.0f);
        return vec3(world) / world.w;
    }

    Ray ScreenToWorldRay(const vec2& screenPosition, const vec2& viewportSize, const mat4x4& invViewProj)
    {
        const vec2 ndcPosition =
        {
            (2.0f * screenPosition.x) / viewportSize.x - 1.0f,
            1.0f - (2.0f * screenPosition.y) / viewportSize.y
        };

        const vec3 nearPosition = UnprojectNDC(vec3(ndcPosition, 1.0f), invViewProj);
        const vec3 farPosition = UnprojectNDC(vec3(ndcPosition, 0.0f), invViewProj);
        const vec3 direction = farPosition - nearPosition;
        const f32 length = glm::length(direction);
        return { nearPosition, direction / length, length };
    }

    bool RayIntersectsAABB(const Ray& ray, const vec3& min, const vec3& max, f32& tNear, f32& tFar)
    {
        tNear = -std::numeric_limits<f32>::infinity();
        tFar = std::numeric_limits<f32>::infinity();

        for (i32 i = 0; i < 3; i++)
        {
            if (glm::abs(ray.dir[i]) < 1.0e-8f)
            {
                if (ray.origin[i] < min[i] || ray.origin[i] > max[i])
                    return false;

                continue;
            }

            f32 t1 = (min[i] - ray.origin[i]) / ray.dir[i];
            f32 t2 = (max[i] - ray.origin[i]) / ray.dir[i];
            if (t1 > t2)
                std::swap(t1, t2);

            tNear = glm::max(tNear, t1);
            tFar = glm::min(tFar, t2);
            if (tNear > tFar)
                return false;
        }

        return tFar >= 0.0f;
    }

    entt::entity FindUnitUnderCursor(entt::registry& registry, entt::entity moverEntity)
    {
        entt::registry::context& ctx = registry.ctx();
        if (!ctx.contains<ECS::Singletons::ActiveCamera>()
            || !ctx.contains<ECS::Singletons::NetworkState>())
            return entt::null;

        const auto& activeCamera = ctx.get<ECS::Singletons::ActiveCamera>();
        auto& networkState = ctx.get<ECS::Singletons::NetworkState>();
        if (activeCamera.entity == entt::null
            || !registry.valid(activeCamera.entity)
            || moverEntity == entt::null
            || !registry.valid(moverEntity)
            || !networkState.networkVisTree)
            return entt::null;

        vec2 mousePosition;
        Editor::Viewport* viewport = ServiceLocator::GetEditorHandler()->GetViewport();
        if (!viewport->GetMousePosition(mousePosition))
            return entt::null;

        const vec2 renderSize = ServiceLocator::GetGameRenderer()->GetRenderer()->GetRenderSize();
        if (renderSize.x <= 0.0f || renderSize.y <= 0.0f)
            return entt::null;

        const auto* camera = registry.try_get<ECS::Components::Camera>(activeCamera.entity);
        const auto* moverUnit = registry.try_get<ECS::Components::Unit>(moverEntity);
        if (!camera || !moverUnit)
            return entt::null;

        const Ray ray = ScreenToWorldRay(mousePosition, renderSize, camera->clipToWorld);
        const vec3 rayEnd = ray.origin + ray.dir * ray.length;
        const vec3 rayMin = glm::min(ray.origin, rayEnd);
        const vec3 rayMax = glm::max(ray.origin, rayEnd);

        entt::entity nearestEntity = entt::null;
        f32 nearestDistance = std::numeric_limits<f32>::infinity();
        networkState.networkVisTree->Search(&rayMin.x, &rayMax.x, [&registry, &networkState, moverUnit, &ray, &nearestEntity, &nearestDistance](const ObjectGUID& guid)
        {
            if (!networkState.networkIDToEntity.contains(guid) || moverUnit->networkID == guid)
                return true;

            const entt::entity entity = networkState.networkIDToEntity[guid];
            if (!registry.valid(entity) || !registry.all_of<ECS::Components::Transform, ECS::Components::AABB, ECS::Components::WorldAABB, ECS::Components::Unit>(entity))
                return true;

            const auto& transform = registry.get<ECS::Components::Transform>(entity);
            const auto& aabb = registry.get<ECS::Components::AABB>(entity);
            const mat4x4 worldToLocal = glm::inverse(transform.GetMatrix());
            const Ray localRay =
            {
                vec3(worldToLocal * vec4(ray.origin, 1.0f)),
                vec3(worldToLocal * vec4(ray.dir, 0.0f)),
                ray.length
            };

            f32 tNear;
            f32 tFar;
            if (RayIntersectsAABB(localRay, aabb.centerPos - aabb.extents, aabb.centerPos + aabb.extents, tNear, tFar) && tNear <= ray.length)
            {
                const f32 hitDistance = glm::max(tNear, 0.0f);
                if (hitDistance < nearestDistance)
                {
                    nearestDistance = hitDistance;
                    nearestEntity = entity;
                }
            }

            return true;
        });

        return nearestEntity;
    }
}

namespace ECS::Systems::CharacterControllerInput
{
    void UpdateHoveredUnit(entt::registry& registry, f32)
    {
        auto& controllerState = registry.ctx().get<Singletons::CharacterControllerSingleton>();
        controllerState.hoveredEntity = entt::null;

        InputManager* inputManager = ServiceLocator::GetInputManager();
        Editor::Viewport* viewport = ServiceLocator::GetEditorHandler()->GetViewport();
        if (inputManager->IsCursorVirtual() || (!viewport->IsEditorMode() && ImGui::GetIO().WantCaptureMouse))
            return;

        EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
        if (registries->uiRegistry)
        {
            auto& uiCtx = registries->uiRegistry->ctx();
            if (uiCtx.contains<Singletons::UISingleton>() && !uiCtx.get<Singletons::UISingleton>().allHoveredEntities.empty())
                return;
        }

        controllerState.hoveredEntity = FindUnitUnderCursor(registry, controllerState.moverEntity);
    }

    void UpdateAutoAttack(entt::registry& registry, f32 deltaTime)
    {
        entt::registry::context& ctx = registry.ctx();
        auto& characterSingleton = ctx.get<Singletons::CharacterSingleton>();
        auto& networkState = ctx.get<Singletons::NetworkState>();
        if (!Util::Network::IsConnected(networkState)
            || !registry.valid(characterSingleton.moverEntity)
            || !registry.all_of<Components::Unit, Components::Transform, Components::UnitEquipment>(characterSingleton.moverEntity))
            return;

        auto& unit = registry.get<Components::Unit>(characterSingleton.moverEntity);
        auto stopAutoAttack = [&]()
        {
            unit.isAutoAttacking = false;
            unit.attackReadyAnimation = Animation::Defines::Type::Invalid;
            characterSingleton.primaryAttackTimer = 0.0f;
            characterSingleton.secondaryAttackTimer = 0.0f;
        };

        if (unit.targetEntity == entt::null)
        {
            if (unit.isAutoAttacking)
                stopAutoAttack();
            return;
        }

        if (!registry.valid(unit.targetEntity)
            || !registry.all_of<Components::Transform, Components::UnitPowersComponent>(unit.targetEntity))
        {
            ClearTarget();
            return;
        }

        characterSingleton.primaryAttackTimer = glm::clamp(characterSingleton.primaryAttackTimer - deltaTime, 0.0f, std::numeric_limits<f32>().max());
        characterSingleton.secondaryAttackTimer = glm::clamp(characterSingleton.secondaryAttackTimer - deltaTime, 0.0f, std::numeric_limits<f32>().max());
        if (!unit.isAutoAttacking)
            return;

        auto& targetPowers = registry.get<Components::UnitPowersComponent>(unit.targetEntity);
        if (!::Util::Unit::HasPower(targetPowers, MetaGen::Shared::Unit::PowerTypeEnum::Health))
        {
            stopAutoAttack();
            return;
        }

        auto& healthPower = ::Util::Unit::GetPower(targetPowers, MetaGen::Shared::Unit::PowerTypeEnum::Health);
        if (healthPower.current <= 0.0f)
        {
            stopAutoAttack();
            return;
        }

        if (unit.attackReadyAnimation == ::Animation::Defines::Type::Invalid)
        {
            auto& equippedItems = registry.get<Components::UnitEquipment>(characterSingleton.moverEntity);
            const u32 mainHandItemID = equippedItems.equipmentSlotToItemID[static_cast<u32>(Database::Item::ItemEquipSlot::MainHand)];

            auto& clientDBSingleton = ServiceLocator::GetEnttRegistries()->dbRegistry->ctx().get<Singletons::ClientDBSingleton>();
            auto* itemStorage = clientDBSingleton.Get(ClientDBHash::Item);
            auto& itemTemplate = itemStorage->Get<MetaGen::Shared::ClientDB::ItemRecord>(mainHandItemID);
            unit.attackReadyAnimation = ::Util::Unit::GetAttackReadyAnimation(itemTemplate.categoryType);
        }

        const auto& transform = registry.get<Components::Transform>(characterSingleton.moverEntity);
        const auto& targetTransform = registry.get<Components::Transform>(unit.targetEntity);
        const vec3 directionToTarget = targetTransform.GetWorldPosition() - transform.GetWorldPosition();
        const f32 distanceToTarget = glm::length(directionToTarget);
        const bool targetIsWithin45DegreeAngle = distanceToTarget > 0.0f
            && glm::dot(directionToTarget / distanceToTarget, glm::normalize(-transform.GetLocalForward())) > glm::cos(glm::radians(45.0f));
        if (distanceToTarget > 5.0f || !targetIsWithin45DegreeAngle)
            return;

        if (characterSingleton.primaryAttackTimer <= 0.0f
            && ECS::Util::Network::SendPacket(networkState, MetaGen::Shared::Packet::ClientSpellCastPacket{
                .spellID = 1
            }))
            characterSingleton.primaryAttackTimer = std::numeric_limits<f32>().max();

        if (characterSingleton.secondaryAttackTimer <= 0.0f
            && ECS::Util::Network::SendPacket(networkState, MetaGen::Shared::Packet::ClientSpellCastPacket{
                .spellID = 2
            }))
            characterSingleton.secondaryAttackTimer = std::numeric_limits<f32>().max();
    }

    bool ClearTarget()
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        entt::registry::context& ctx = registry->ctx();
        auto& characterSingleton = ctx.get<Singletons::CharacterSingleton>();
        auto& networkState = ctx.get<Singletons::NetworkState>();
        if (!registry->valid(characterSingleton.moverEntity))
            return false;

        auto* unit = registry->try_get<Components::Unit>(characterSingleton.moverEntity);
        if (!unit)
            return false;

        if (unit->targetEntity == entt::null)
            return true;

        if (!Util::Network::SendPacket(networkState, MetaGen::Shared::Packet::ClientUnitTargetUpdatePacket{
            .targetGUID = ObjectGUID::Empty
        }))
            return false;

        if (registry->valid(unit->targetEntity))
        {
            if (auto* model = registry->try_get<Components::Model>(unit->targetEntity))
                ServiceLocator::GetGameRenderer()->GetModelLoader()->SetModelHighlight(*model, 1.0f);
        }

        unit->targetEntity = entt::null;
        unit->isAutoAttacking = false;
        unit->attackReadyAnimation = Animation::Defines::Type::Invalid;
        characterSingleton.primaryAttackTimer = 0.0f;
        characterSingleton.secondaryAttackTimer = 0.0f;

        Scripting::Util::Zenith::GetGlobal()->CallEvent(MetaGen::Game::Lua::UnitEvent::TargetChanged, MetaGen::Game::Lua::UnitEventDataTargetChanged{
            .unitID = entt::to_integral(characterSingleton.moverEntity),
            .targetID = entt::to_integral(unit->targetEntity)
        });
        return true;
    }

    bool HandleTargetInput(i32 key, KeybindAction, KeybindModifier modifier)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        entt::registry::context& ctx = registry->ctx();
        auto& characterSingleton = ctx.get<Singletons::CharacterSingleton>();
        auto& activeCamera = ctx.get<Singletons::ActiveCamera>();
        auto& networkState = ctx.get<Singletons::NetworkState>();

        if (characterSingleton.moverEntity == entt::null || activeCamera.entity == entt::null)
            return false;

        Scripting::Zenith* zenith = Scripting::Util::Zenith::GetGlobal();
        ModelLoader* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();
        auto& unit = registry->get<Components::Unit>(characterSingleton.moverEntity);

        if (ctx.contains<Singletons::OrbitalCameraSettings>())
        {
            const auto& orbitalCameraSettings = ctx.get<Singletons::OrbitalCameraSettings>();
            if (activeCamera.entity == orbitalCameraSettings.entity && orbitalCameraSettings.captureMouseWasDragged)
                return true;
        }

        if (key == GLFW_MOUSE_BUTTON_LEFT && (modifier & KeybindModifier::Shift) != KeybindModifier::Invalid)
        {
            ClearTarget();
            return true;
        }

        entt::entity targetEntity = entt::null;
        if (const auto* controllerState = ctx.find<Singletons::CharacterControllerSingleton>())
            targetEntity = controllerState->hoveredEntity;
        else
            targetEntity = FindUnitUnderCursor(*registry, characterSingleton.moverEntity);

        if (!registry->valid(targetEntity) || targetEntity == characterSingleton.moverEntity || !registry->all_of<Components::Unit>(targetEntity))
            return false;

        const ObjectGUID targetNetworkID = registry->get<Components::Unit>(targetEntity).networkID;

        if (unit.targetEntity == targetEntity)
        {
            if (key == GLFW_MOUSE_BUTTON_RIGHT)
            {
                unit.isAutoAttacking = true;
                return true;
            }

            return false;
        }

        if (Util::Network::SendPacket(networkState, MetaGen::Shared::Packet::ClientUnitTargetUpdatePacket{
            .targetGUID = targetNetworkID
        }))
        {
            if (auto* model = registry->try_get<Components::Model>(unit.targetEntity))
                modelLoader->SetModelHighlight(*model, 1.0f);

            unit.targetEntity = targetEntity;

            if (auto* model = registry->try_get<Components::Model>(targetEntity))
                modelLoader->SetModelHighlight(*model, 1.25f);

            if (key == GLFW_MOUSE_BUTTON_RIGHT)
                unit.isAutoAttacking = true;

            zenith->CallEvent(MetaGen::Game::Lua::UnitEvent::TargetChanged, MetaGen::Game::Lua::UnitEventDataTargetChanged{
                .unitID = entt::to_integral(characterSingleton.moverEntity),
                .targetID = entt::to_integral(targetEntity)
            });
        }

        return true;
    }
}
