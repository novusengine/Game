#include "CharacterControllerInput.h"

#include "Game-Lib/ECS/Components/AABB.h"
#include "Game-Lib/ECS/Components/Camera.h"
#include "Game-Lib/ECS/Components/Model.h"
#include "Game-Lib/ECS/Components/Unit.h"
#include "Game-Lib/ECS/Singletons/ActiveCamera.h"
#include "Game-Lib/ECS/Singletons/CharacterSingleton.h"
#include "Game-Lib/ECS/Singletons/CharacterControllerSingleton.h"
#include "Game-Lib/ECS/Singletons/NetworkState.h"
#include "Game-Lib/ECS/Singletons/OrbitalCameraSettings.h"
#include "Game-Lib/ECS/Singletons/UISingleton.h"
#include "Game-Lib/ECS/Util/FactionUtil.h"
#include "Game-Lib/ECS/Util/Network/NetworkUtil.h"
#include "Game-Lib/ECS/Util/Transforms.h"
#include "Game-Lib/Editor/EditorHandler.h"
#include "Game-Lib/Editor/Viewport.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Model/ModelLoader.h"
#include "Game-Lib/Scripting/Util/ZenithUtil.h"
#include "Game-Lib/Util/AnimationUtil.h"
#include "Game-Lib/Util/ServiceLocator.h"
#include "Game-Lib/Util/UnitUtil.h"

#include <MetaGen/Game/Lua/Lua.h>
#include <MetaGen/Shared/Packet/Packet.h>

#include <Input/InputSystem.h>

#include <Renderer/Renderer.h>

#include <Scripting/Zenith.h>

#include <entt/entt.hpp>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <imgui.h>

#include <algorithm>
#include <limits>

namespace
{
    constexpr f32 HOVER_HIGHLIGHT_INTENSITY = 1.15f;
    constexpr f32 TARGET_HIGHLIGHT_INTENSITY = 1.25f;

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
        if (!ctx.contains<ECS::Singletons::ActiveCamera>() || !ctx.contains<ECS::Singletons::NetworkState>())
            return entt::null;

        const auto& activeCamera = ctx.get<ECS::Singletons::ActiveCamera>();
        auto& networkState = ctx.get<ECS::Singletons::NetworkState>();
        if (activeCamera.entity == entt::null || !registry.valid(activeCamera.entity) || moverEntity == entt::null || !registry.valid(moverEntity) || !networkState.networkVisTree)
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
        f32 nearestCursorDistanceSquared = std::numeric_limits<f32>::infinity();
        f32 nearestHitDistance = std::numeric_limits<f32>::infinity();
        networkState.networkVisTree->Search(&rayMin.x, &rayMax.x, [&](const ObjectGUID& guid)
        {
            if (!networkState.networkIDToEntity.contains(guid) || moverUnit->networkID == guid)
                return true;

            const entt::entity entity = networkState.networkIDToEntity[guid];
            if (!registry.valid(entity) || !registry.all_of<ECS::Components::Transform, ECS::Components::AABB, ECS::Components::WorldAABB, ECS::Components::Unit>(entity))
                return true;

            const auto& transform = registry.get<ECS::Components::Transform>(entity);
            const auto& aabb = registry.get<ECS::Components::AABB>(entity);
            const mat4x4 transformMatrix = transform.GetMatrix();
            const mat4x4 worldToLocal = glm::inverse(transformMatrix);
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
                const vec3 worldCenter = vec3(transformMatrix * vec4(aabb.centerPos, 1.0f));
                const vec4 clipCenter = camera->worldToClip * vec4(worldCenter, 1.0f);
                if (clipCenter.w <= 0.0f)
                    return true;

                const vec2 ndcCenter = vec2(clipCenter) / clipCenter.w;
                const vec2 screenCenter =
                {
                    (ndcCenter.x * 0.5f + 0.5f) * renderSize.x,
                    (0.5f - ndcCenter.y * 0.5f) * renderSize.y
                };
                const vec2 cursorOffset = screenCenter - mousePosition;
                const f32 cursorDistanceSquared = glm::dot(cursorOffset, cursorOffset);
                const f32 hitDistance = glm::max(tNear, 0.0f);
                if (cursorDistanceSquared < nearestCursorDistanceSquared || (cursorDistanceSquared == nearestCursorDistanceSquared && hitDistance < nearestHitDistance))
                {
                    nearestCursorDistanceSquared = cursorDistanceSquared;
                    nearestHitDistance = hitDistance;
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
        entt::entity hoveredEntity = entt::null;
        InputSystem* inputSystem = ServiceLocator::GetInputSystem();
        Editor::Viewport* viewport = ServiceLocator::GetEditorHandler()->GetViewport();
        bool canHoverWorld = !inputSystem->IsMouseCaptured() && (viewport->IsEditorMode() || !ImGui::GetIO().WantCaptureMouse);

        if (canHoverWorld)
        {
            EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
            if (registries->uiRegistry)
            {
                auto& uiCtx = registries->uiRegistry->ctx();
                canHoverWorld = !uiCtx.contains<Singletons::UISingleton>() || uiCtx.get<Singletons::UISingleton>().allHoveredEntities.empty();
            }
        }

        if (canHoverWorld)
            hoveredEntity = FindUnitUnderCursor(registry, controllerState.moverEntity);

        if (hoveredEntity == controllerState.hoveredEntity)
            return;

        entt::entity targetEntity = entt::null;
        if (registry.valid(controllerState.moverEntity))
        {
            if (const auto* moverUnit = registry.try_get<Components::Unit>(controllerState.moverEntity))
                targetEntity = moverUnit->targetEntity;
        }

        ModelLoader* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();
        if (registry.valid(controllerState.hoveredEntity))
        {
            if (const auto* model = registry.try_get<Components::Model>(controllerState.hoveredEntity))
                modelLoader->SetModelHighlight(*model, controllerState.hoveredEntity == targetEntity ? TARGET_HIGHLIGHT_INTENSITY : 1.0f);
        }

        controllerState.hoveredEntity = hoveredEntity;
        if (registry.valid(hoveredEntity))
        {
            if (const auto* model = registry.try_get<Components::Model>(hoveredEntity))
            {
                const f32 highlightIntensity = hoveredEntity == targetEntity ? TARGET_HIGHLIGHT_INTENSITY : HOVER_HIGHLIGHT_INTENSITY;
                modelLoader->SetModelHighlight(*model, highlightIntensity);
            }
        }
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

        if (!Util::Network::SendPacket(networkState,
            MetaGen::Shared::Packet::ClientUnitTargetUpdatePacket{
                .targetGUID = ObjectGUID::Empty
            },
            MetaGen::Shared::Packet::ClientAutoAttackStatePacket{
                .enabled = 0 
            }))
            return false;

        if (registry->valid(unit->targetEntity))
        {
            if (auto* model = registry->try_get<Components::Model>(unit->targetEntity))
            {
                const auto* controllerState = ctx.find<Singletons::CharacterControllerSingleton>();
                const f32 highlightIntensity = controllerState && controllerState->hoveredEntity == unit->targetEntity ? HOVER_HIGHLIGHT_INTENSITY : 1.0f;
                ServiceLocator::GetGameRenderer()->GetModelLoader()->SetModelHighlight(*model, highlightIntensity);
            }
        }

        unit->targetEntity = entt::null;
        ::Util::Unit::SetAutoAttackVisualState(*registry, characterSingleton.moverEntity, false);

        Scripting::Util::Zenith::GetGlobal()->CallEvent(MetaGen::Game::Lua::UnitEvent::TargetChanged, MetaGen::Game::Lua::UnitEventDataTargetChanged{
            .unitID = entt::to_integral(characterSingleton.moverEntity),
            .targetID = entt::to_integral(unit->targetEntity)
        });
        return true;
    }

    bool SetTarget(entt::entity targetEntity, bool updateAutoAttackState, bool autoAttackEnabled)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        entt::registry::context& ctx = registry->ctx();
        auto& characterSingleton = ctx.get<Singletons::CharacterSingleton>();
        auto& networkState = ctx.get<Singletons::NetworkState>();
        if (!registry->valid(characterSingleton.moverEntity) || !registry->valid(targetEntity) ||
            !registry->all_of<Components::Unit>(characterSingleton.moverEntity) || !registry->all_of<Components::Unit>(targetEntity))
        {
            return false;
        }

        auto& unit = registry->get<Components::Unit>(characterSingleton.moverEntity);
        if (unit.targetEntity == targetEntity)
        {
            if (!updateAutoAttackState)
                return true;

            if (!Util::Network::SendPacket(networkState, MetaGen::Shared::Packet::ClientAutoAttackStatePacket{
                .enabled = static_cast<u8>(autoAttackEnabled) }))
            {
                return false;
            }

            ::Util::Unit::SetAutoAttackVisualState(*registry, characterSingleton.moverEntity, autoAttackEnabled);
            return true;
        }

        const ObjectGUID targetNetworkID = registry->get<Components::Unit>(targetEntity).networkID;
        const bool targetUpdateSent = updateAutoAttackState
            ? Util::Network::SendPacket(networkState,
                MetaGen::Shared::Packet::ClientUnitTargetUpdatePacket{
                    .targetGUID = targetNetworkID },
                MetaGen::Shared::Packet::ClientAutoAttackStatePacket{
                    .enabled = static_cast<u8>(autoAttackEnabled) })
            : Util::Network::SendPacket(networkState, MetaGen::Shared::Packet::ClientUnitTargetUpdatePacket{
                .targetGUID = targetNetworkID });
        if (!targetUpdateSent)
            return false;

        const auto* controllerState = ctx.find<Singletons::CharacterControllerSingleton>();
        ModelLoader* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();
        if (registry->valid(unit.targetEntity))
        {
            if (auto* model = registry->try_get<Components::Model>(unit.targetEntity))
            {
                const f32 highlightIntensity = controllerState && controllerState->hoveredEntity == unit.targetEntity ? HOVER_HIGHLIGHT_INTENSITY : 1.0f;
                modelLoader->SetModelHighlight(*model, highlightIntensity);
            }
        }

        unit.targetEntity = targetEntity;
        if (updateAutoAttackState)
            ::Util::Unit::SetAutoAttackVisualState(*registry, characterSingleton.moverEntity, autoAttackEnabled);

        if (targetEntity != characterSingleton.moverEntity)
        {
            if (auto* model = registry->try_get<Components::Model>(targetEntity))
                modelLoader->SetModelHighlight(*model, TARGET_HIGHLIGHT_INTENSITY);
        }

        Scripting::Util::Zenith::GetGlobal()->CallEvent(MetaGen::Game::Lua::UnitEvent::TargetChanged, MetaGen::Game::Lua::UnitEventDataTargetChanged{
            .unitID = entt::to_integral(characterSingleton.moverEntity),
            .targetID = entt::to_integral(targetEntity)
        });
        return true;
    }

    InputReply HandleTargetInput(const InputActionEvent& event)
    {
        if (event.phase != InputPhase::Released)
            return InputReply::Handled;

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        entt::registry::context& ctx = registry->ctx();
        auto& characterSingleton = ctx.get<Singletons::CharacterSingleton>();
        auto& activeCamera = ctx.get<Singletons::ActiveCamera>();

        if (characterSingleton.moverEntity == entt::null || activeCamera.entity == entt::null)
            return InputReply::Ignored;

        auto& unit = registry->get<Components::Unit>(characterSingleton.moverEntity);

        if (ctx.contains<Singletons::OrbitalCameraSettings>())
        {
            const auto& orbitalCameraSettings = ctx.get<Singletons::OrbitalCameraSettings>();
            if (activeCamera.entity == orbitalCameraSettings.entity && orbitalCameraSettings.captureMouseWasDragged)
                return InputReply::Consumed;
        }

        if (event.control == InputControl::Mouse(MouseButton::Left) && (event.modifiers & InputModifier::Shift) != InputModifier::None)
        {
            ClearTarget();
            return InputReply::Consumed;
        }

        const auto* controllerState = ctx.find<Singletons::CharacterControllerSingleton>();
        entt::entity targetEntity = entt::null;
        if (controllerState)
        {
            targetEntity = controllerState->hoveredEntity;
        }
        else
        {
            targetEntity = FindUnitUnderCursor(*registry, characterSingleton.moverEntity);
        }

        if (!registry->valid(targetEntity) || targetEntity == characterSingleton.moverEntity || !registry->all_of<Components::Unit>(targetEntity))
            return InputReply::Ignored;

        if (unit.targetEntity == targetEntity)
        {
            if (event.control == InputControl::Mouse(MouseButton::Right))
            {
                const bool canAttack = ECS::Util::Faction::CanAttack(*registry, targetEntity);
                SetTarget(targetEntity, true, canAttack);
                return InputReply::Consumed;
            }

            return InputReply::Ignored;
        }

        const bool startAutoAttack = event.control == InputControl::Mouse(MouseButton::Right) && ECS::Util::Faction::CanAttack(*registry, targetEntity);
        SetTarget(targetEntity, event.control == InputControl::Mouse(MouseButton::Right), startAutoAttack);

        return InputReply::Consumed;
    }
}
