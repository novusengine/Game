#include "RemoteUnitPresentation.h"

#include "Game-Lib/ECS/Components/MovementInfo.h"
#include "Game-Lib/ECS/Components/RemoteGroundVisualAlignment.h"
#include "Game-Lib/ECS/Components/Unit.h"
#include "Game-Lib/ECS/Components/UnitMovementOverTime.h"
#include "Game-Lib/ECS/Singletons/CharacterSingleton.h"
#include "Game-Lib/ECS/Singletons/JoltState.h"
#include "Game-Lib/ECS/Util/Transforms.h"

#include <Base/CVarSystem/CVarSystem.h>

#include <entt/entt.hpp>
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/RayCast.h>

AutoCVar_Int CVAR_NetworkRemoteGroundCorrection(CVarCategory::Network, "remoteGroundCorrection", "Snaps grounded remote units to static world collision", 1, CVarFlags::EditCheckbox | CVarFlags::DoNotSave);
AutoCVar_Int CVAR_NetworkRemoteSlopeAlignment(CVarCategory::Network, "remoteSlopeAlignment", "Visually aligns grounded remote units to sampled slopes", 0, CVarFlags::EditCheckbox | CVarFlags::DoNotSave);

namespace
{
    constexpr f32 GROUND_SEARCH_RANGE = 3.0f;
    constexpr f32 GROUND_NORMAL_SMOOTHING = 12.0f;
    constexpr f32 SPEED_SMOOTHING = 10.0f;
    const f32 MAX_SLOPE_PITCH = glm::radians(12.0f);

    class StaticBroadPhaseFilter final : public JPH::BroadPhaseLayerFilter
    {
    public:
        bool ShouldCollide(JPH::BroadPhaseLayer layer) const override
        {
            return layer == Jolt::BroadPhaseLayers::NON_MOVING;
        }
    };

    class StaticObjectLayerFilter final : public JPH::ObjectLayerFilter
    {
    public:
        bool ShouldCollide(JPH::ObjectLayer layer) const override
        {
            return layer == Jolt::Layers::NON_MOVING;
        }
    };

    struct GroundSample
    {
        vec3 position;
        vec3 normal;
    };

    bool TrySampleGround(ECS::Singletons::JoltState& joltState, const vec3& expectedPosition, GroundSample& outSample)
    {
        const StaticBroadPhaseFilter broadPhaseFilter;
        const StaticObjectLayerFilter objectLayerFilter;
        const JPH::BodyFilter bodyFilter;
        const JPH::RRayCast ray(
            JPH::RVec3(expectedPosition.x, expectedPosition.y + GROUND_SEARCH_RANGE, expectedPosition.z),
            JPH::Vec3(0.0f, -2.0f * GROUND_SEARCH_RANGE, 0.0f));
        JPH::AllHitCollisionCollector<JPH::CastRayCollector> collector;
        joltState.physicsSystem.GetNarrowPhaseQuery().CastRay(
            ray,
            JPH::RayCastSettings(),
            collector,
            broadPhaseFilter,
            objectLayerFilter,
            bodyFilter);

        bool foundSurface = false;
        f32 closestDistance = GROUND_SEARCH_RANGE;
        for (const JPH::RayCastResult& hit : collector.mHits)
        {
            const JPH::RVec3 hitPosition = ray.GetPointOnRay(hit.mFraction);
            const f32 distance = glm::abs(static_cast<f32>(hitPosition.GetY()) - expectedPosition.y);
            if (distance >= closestDistance)
                continue;

            JPH::BodyLockRead lock(joltState.physicsSystem.GetBodyLockInterface(), hit.mBodyID);
            if (!lock.SucceededAndIsInBroadPhase())
                continue;

            const JPH::Vec3 normal = lock.GetBody().GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, hitPosition).NormalizedOr(JPH::Vec3::sAxisY());
            if (normal.GetY() <= 0.05f)
                continue;

            closestDistance = distance;
            outSample.position = vec3(static_cast<f32>(hitPosition.GetX()), static_cast<f32>(hitPosition.GetY()), static_cast<f32>(hitPosition.GetZ()));
            outSample.normal = vec3(normal.GetX(), normal.GetY(), normal.GetZ());
            foundSurface = true;
        }

        return foundSurface;
    }

    void UpdateDisplayedSpeed(ECS::Components::UnitMovementOverTime& movement, const vec3& position, bool isGrounded, f32 deltaTime)
    {
        if (movement.hasRenderedPosition && deltaTime > 0.0f)
        {
            vec3 displacement = position - movement.previousRenderedPos;
            if (isGrounded)
                displacement.y = 0.0f;

            const f32 measuredSpeed = glm::length(displacement) / deltaTime;
            const f32 blend = 1.0f - glm::exp(-SPEED_SMOOTHING * deltaTime);
            movement.displayedSpeed = glm::mix(movement.displayedSpeed, measuredSpeed, blend);
        }

        movement.previousRenderedPos = position;
        movement.hasRenderedPosition = true;
    }

    quat GetNetworkRotation(const ECS::Components::MovementInfo& movementInfo)
    {
        return quat(vec3(movementInfo.pitch, movementInfo.yaw, 0.0f));
    }

    void UpdateGroundAlignment(
        entt::registry& registry,
        ECS::TransformSystem& transformSystem,
        entt::entity entity,
        const ECS::Components::MovementInfo& movementInfo,
        const GroundSample* groundSample,
        f32 deltaTime)
    {
        if (!groundSample || CVAR_NetworkRemoteSlopeAlignment.Get() == 0)
        {
            transformSystem.SetWorldRotation(entity, GetNetworkRotation(movementInfo));
            registry.remove<ECS::Components::RemoteGroundVisualAlignment>(entity);
            return;
        }

        auto& alignment = registry.get_or_emplace<ECS::Components::RemoteGroundVisualAlignment>(entity);
        const f32 blend = 1.0f - glm::exp(-GROUND_NORMAL_SMOOTHING * deltaTime);
        alignment.smoothedNormal = glm::normalize(glm::mix(alignment.smoothedNormal, groundSample->normal, blend));

        // Resolve the sampled normal in yaw-local space. Its Z component describes the slope
        // along the unit's facing direction; ignoring X deliberately prevents terrain-induced roll.
        const quat yawRotation = quat(vec3(0.0f, movementInfo.yaw, 0.0f));
        const vec3 localGroundNormal = glm::inverse(yawRotation) * alignment.smoothedNormal;
        const f32 slopePitch = glm::clamp(
            glm::atan(localGroundNormal.z, glm::max(localGroundNormal.y, 0.0001f)),
            -MAX_SLOPE_PITCH,
            MAX_SLOPE_PITCH);

        const quat alignedRotation = quat(vec3(movementInfo.pitch + slopePitch, movementInfo.yaw, 0.0f));
        transformSystem.SetWorldRotation(entity, alignedRotation);
    }

    void SynchronizeBody(
        ECS::Singletons::JoltState& joltState,
        const ECS::Components::Unit& unit,
        const ECS::Components::MovementInfo& movementInfo,
        const vec3& position)
    {
        if (unit.bodyID == std::numeric_limits<u32>().max())
            return;

        // Ground alignment is visual presentation. Keep collision on the authoritative network rotation.
        const quat rotation = GetNetworkRotation(movementInfo);
        joltState.physicsSystem.GetBodyInterface().SetPositionAndRotation(
            JPH::BodyID(unit.bodyID),
            JPH::Vec3(position.x, position.y, position.z),
            JPH::Quat(rotation.x, rotation.y, rotation.z, rotation.w),
            JPH::EActivation::DontActivate);
    }
}

void ECS::Util::RemoteUnitPresentation::Update(entt::registry& registry, f32 deltaTime)
{
    auto& characterSingleton = registry.ctx().get<Singletons::CharacterSingleton>();
    auto& joltState = registry.ctx().get<Singletons::JoltState>();
    TransformSystem& transformSystem = TransformSystem::Get(registry);

    auto view = registry.view<Components::Transform, Components::Unit, Components::MovementInfo, Components::UnitMovementOverTime>();
    view.each([&](entt::entity entity, Components::Transform& transform, Components::Unit& unit, const Components::MovementInfo& movementInfo, Components::UnitMovementOverTime& movement)
    {
        if (entity == characterSingleton.moverEntity || !movement.hasSnapshot)
            return;

        movement.elapsed = glm::min(movement.elapsed + deltaTime, movement.duration);
        const f32 progress = movement.duration > 0.0f ? movement.elapsed / movement.duration : 1.0f;
        vec3 renderedPosition = glm::mix(movement.startPos, movement.endPos, progress);

        const bool isGrounded = movementInfo.movementFlags.grounded &&
            !movementInfo.movementFlags.flying &&
            !movementInfo.movementFlags.jumping;

        GroundSample groundSample;
        const bool hasGroundSample = isGrounded &&
            CVAR_NetworkRemoteGroundCorrection.Get() != 0 &&
            TrySampleGround(joltState, renderedPosition, groundSample);
        if (hasGroundSample)
            renderedPosition.y = groundSample.position.y;

        UpdateDisplayedSpeed(movement, renderedPosition, isGrounded, deltaTime);
        transformSystem.SetWorldPosition(entity, renderedPosition);
        UpdateGroundAlignment(registry, transformSystem, entity, movementInfo, hasGroundSample ? &groundSample : nullptr, deltaTime);
        SynchronizeBody(joltState, unit, movementInfo, renderedPosition);
    });
}
