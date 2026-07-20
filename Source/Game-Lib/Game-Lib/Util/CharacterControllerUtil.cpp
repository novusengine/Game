#include "CharacterControllerUtil.h"

#include "Game-Lib/ECS/Components/MovementInfo.h"
#include "Game-Lib/ECS/Util/Transforms.h"
#include "Game-Lib/ECS/Singletons/JoltState.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace Util::CharacterController
{
    JPH::Vec3 ToJolt(const vec3& value)
    {
        return JPH::Vec3(value.x, value.y, value.z);
    }

    vec3 FromJolt(const JPH::Vec3Arg& value)
    {
        return vec3(value.GetX(), value.GetY(), value.GetZ());
    }

    BoxPyramidShapeDimensions GetBoxPyramidShapeDimensionsFromCollision(f32 collisionWidth, f32 collisionHeight)
    {
        const f32 collisionHalfWidth = glm::max(0.0f, collisionWidth);
        return
        {
            .collisionHalfWidth = collisionHalfWidth,
            .collisionHeight = glm::max(0.0f, collisionHeight),
            .pyramidHeight = collisionHalfWidth
        };
    }

    BoxPyramidShapeDimensions GetBoxPyramidShapeDimensions(const ECS::Singletons::CharacterControllerSettings& settings)
    {
        return GetBoxPyramidShapeDimensionsFromCollision(settings.collisionHalfWidth, settings.collisionHeight);
    }

    BoxPyramidShapeDimensions EstimateBoxPyramidShapeDimensionsFromGeoBox(const vec3& geoBoxMin, const vec3& geoBoxMax)
    {
        // GeoBox coordinates are expected in engine space: Y is vertical and Z is
        // lateral. The supplied data indicate a 9/8 lateral envelope scale with
        // widths quantized to inches, while collision height tracks GeoBox height.
        constexpr f32 lateralEnvelopeScale = 9.0f / 8.0f;
        constexpr f32 yardsPerInch = 1.0f / 36.0f;

        const f32 geoBoxWidth = glm::max(0.0f, geoBoxMax.z - geoBoxMin.z);
        const f32 geoBoxHeight = glm::max(0.0f, geoBoxMax.y - geoBoxMin.y);
        const f32 estimatedHalfWidth = geoBoxWidth * lateralEnvelopeScale * 0.5f;
        const f32 quantizedHalfWidth = glm::round(estimatedHalfWidth / yardsPerInch) * yardsPerInch;

        return
        {
            .collisionHalfWidth = quantizedHalfWidth,
            .collisionHeight = geoBoxHeight,
            .pyramidHeight = quantizedHalfWidth
        };
    }

    JPH::ShapeRefC CreateBoxPyramidShape(const BoxPyramidShapeDimensions& dimensions, f32 convexRadius)
    {
        const f32 halfWidth = dimensions.collisionHalfWidth;
        const f32 collisionHeight = dimensions.collisionHeight;
        const f32 pyramidHeight = dimensions.pyramidHeight;
        if (halfWidth <= 0.0f || collisionHeight <= pyramidHeight || pyramidHeight < 0.0f)
            return {};

        const JPH::Vec3 points[9] =
        {
            // Top of the box.
            {-halfWidth, collisionHeight, -halfWidth},
            {-halfWidth, collisionHeight,  halfWidth},
            { halfWidth, collisionHeight,  halfWidth},
            { halfWidth, collisionHeight, -halfWidth},

            // Bottom of the box.
            {-halfWidth, pyramidHeight, -halfWidth},
            {-halfWidth, pyramidHeight,  halfWidth},
            { halfWidth, pyramidHeight,  halfWidth},
            { halfWidth, pyramidHeight, -halfWidth},

            // Foot apex. This is the authoritative gameplay position.
            { 0.0f, 0.0f, 0.0f }
        };

        JPH::ConvexHullShapeSettings shapeSetting(points, 9, glm::max(0.0f, convexRadius));
        JPH::ShapeSettings::ShapeResult shapeResult = shapeSetting.Create();
        if (!shapeResult.IsValid())
            return {};

        return shapeResult.Get();
    }

    JPH::ShapeRefC CreateBoxPyramidShape(const ECS::Singletons::CharacterControllerSettings& settings)
    {
        return CreateBoxPyramidShape(GetBoxPyramidShapeDimensions(settings), settings.shapeConvexRadius);
    }

    bool HasMovementInput(const ECS::Singletons::CharacterMovementIntent& intent)
    {
        const bool moveForward = intent.moveForward || intent.autorun || intent.mouseForward;
        const bool hasForwardBackMovement = moveForward != static_cast<bool>(intent.moveBackward);
        const bool hasStrafeMovement = static_cast<bool>(intent.strafeLeft) != static_cast<bool>(intent.strafeRight);
        return hasForwardBackMovement || hasStrafeMovement;
    }

    void ResetMovementInput(ECS::Singletons::CharacterControllerSingleton& state, ECS::Components::MovementInfo* movementInfo)
    {
        state.autorunEnabled = false;
        state.intent = {};

        if (!movementInfo)
            return;

        movementInfo->movementFlags.forward = false;
        movementInfo->movementFlags.backward = false;
        movementInfo->movementFlags.left = false;
        movementInfo->movementFlags.right = false;
        movementInfo->movementFlags.justGrounded = false;
        movementInfo->movementFlags.justEndedJump = false;
    }

    JPH::Vec3 BuildCharacterRelativeMoveDirection(const ECS::Singletons::CharacterMovementIntent& intent, const quat& characterRotation)
    {
        const bool moveForward = (intent.moveForward || intent.autorun || intent.mouseForward) && !intent.moveBackward;
        const bool moveBackward = intent.moveBackward && !(intent.moveForward || intent.autorun || intent.mouseForward);
        const bool moveLeft = intent.strafeLeft && !intent.strafeRight;
        const bool moveRight = intent.strafeRight && !intent.strafeLeft;
        if (!moveForward && !moveBackward && !moveLeft && !moveRight)
            return JPH::Vec3::sZero();

        const JPH::Vec3 worldForward = ToJolt(ECS::Components::Transform::WORLD_FORWARD);
        const JPH::Vec3 worldRight = ToJolt(ECS::Components::Transform::WORLD_RIGHT);
        JPH::Vec3 moveDirection = JPH::Vec3::sZero();

        if (moveForward)
            moveDirection -= worldForward;
        else if (moveBackward)
            moveDirection += worldForward;

        if (moveLeft)
            moveDirection += worldRight;
        else if (moveRight)
            moveDirection -= worldRight;

        const JPH::Quat rotation(characterRotation.x, characterRotation.y, characterRotation.z, characterRotation.w);
        return (rotation * moveDirection).Normalized();
    }

    f32 GetPlanarSpeed(const ECS::Components::MovementInfo& movementInfo, const ECS::Singletons::CharacterMovementIntent& intent, ECS::Singletons::CharacterMotorType motor)
    {
        const bool movingBackward = intent.moveBackward && !(intent.moveForward || intent.autorun || intent.mouseForward);
        if (movingBackward)
            return movementInfo.speeds.backward;

        switch (motor)
        {
            case ECS::Singletons::CharacterMotorType::Flight:
                return movementInfo.speeds.flight;
            case ECS::Singletons::CharacterMotorType::Swim:
                return movementInfo.speeds.swim;
            case ECS::Singletons::CharacterMotorType::Ground:
            case ECS::Singletons::CharacterMotorType::Vehicle:
            default:
                return movementInfo.speeds.ground;
        }
    }

    void ResetGroundProbeDebug(ECS::Singletons::CharacterControllerDebugSingleton& debugState)
    {
        debugState.supportProbeStart = vec3(0.0f);
        debugState.supportProbeEnd = vec3(0.0f);
        debugState.supportProbeHitPosition = vec3(0.0f);
        debugState.supportProbeNormal = vec3(0.0f);
        debugState.supportProbeActive = false;
        debugState.supportProbeHit = false;
        debugState.supportProbeWalkable = false;
        debugState.supportProbeUsedForGrounding = false;
    }

    bool IsWalkableGroundNormal(JPH::CharacterVirtual* character, const ECS::Singletons::CharacterControllerSettings& settings, const JPH::Vec3Arg& groundNormal)
    {
        if (!character || groundNormal.IsNearZero() || character->IsSlopeTooSteep(groundNormal))
            return false;

        const JPH::Vec3 up = ToJolt(settings.up);
        return groundNormal.Dot(up) > 1.0e-4f;
    }

    bool TryGetWalkableGroundProbeNormal(JPH::CharacterVirtual* character, const ECS::Singletons::CharacterControllerSettings& settings, ECS::Singletons::JoltState& joltState, const JPH::Vec3Arg& position, f32 startOffset, f32 distance, JPH::Vec3& outGroundNormal, JPH::Vec3* outHitPosition, ECS::Singletons::CharacterControllerDebugSingleton* debugState)
    {
        if (debugState)
            ResetGroundProbeDebug(*debugState);

        if (distance <= 0.0f)
            return false;

        const JPH::Vec3 up = ToJolt(settings.up);
        startOffset = glm::max(0.0f, startOffset);
        distance = glm::max(0.0f, distance);
        const JPH::Vec3 start = position + up * startOffset;
        const JPH::Vec3 displacement = -up * (startOffset + distance);
        if (debugState)
        {
            debugState->supportProbeStart = FromJolt(start);
            debugState->supportProbeEnd = FromJolt(start + displacement);
            debugState->supportProbeActive = true;
        }

        JPH::RRayCast ray(JPH::RVec3(start.GetX(), start.GetY(), start.GetZ()), displacement);
        JPH::RayCastResult hit;

        JPH::DefaultBroadPhaseLayerFilter broadPhaseLayerFilter(joltState.objectVSBroadPhaseLayerFilter, Jolt::Layers::MOVING);
        JPH::DefaultObjectLayerFilter objectLayerFilter(joltState.objectVSObjectLayerFilter, Jolt::Layers::MOVING);
        JPH::BodyFilter bodyFilter;

        if (!joltState.physicsSystem.GetNarrowPhaseQuery().CastRay(ray, hit, broadPhaseLayerFilter, objectLayerFilter, bodyFilter))
            return false;

        JPH::BodyLockRead lock(joltState.physicsSystem.GetBodyLockInterface(), hit.mBodyID);
        if (!lock.SucceededAndIsInBroadPhase())
            return false;

        const JPH::RVec3 hitPosition = ray.GetPointOnRay(hit.mFraction);
        const JPH::Vec3 hitPositionFloat(static_cast<f32>(hitPosition.GetX()), static_cast<f32>(hitPosition.GetY()), static_cast<f32>(hitPosition.GetZ()));
        const JPH::Vec3 groundNormal = lock.GetBody().GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, hitPosition).NormalizedOr(JPH::Vec3::sZero());
        const bool isWalkable = IsWalkableGroundNormal(character, settings, groundNormal);
        if (debugState)
        {
            debugState->supportProbeHitPosition = FromJolt(hitPositionFloat);
            debugState->supportProbeNormal = FromJolt(groundNormal);
            debugState->supportProbeHit = true;
            debugState->supportProbeWalkable = isWalkable;
        }

        if (!isWalkable)
            return false;

        outGroundNormal = groundNormal;
        if (outHitPosition)
            *outHitPosition = hitPositionFloat;

        return true;
    }

    JPH::Vec3 ResolveGroundMovementNormal(JPH::CharacterVirtual* character, const ECS::Singletons::CharacterControllerSettings& settings)
    {
        const JPH::Vec3 up = ToJolt(settings.up);
        JPH::Vec3 groundNormal = character->GetGroundNormal();
        f32 bestUpDot = IsWalkableGroundNormal(character, settings, groundNormal) ? groundNormal.Dot(up) : 0.0f;

        for (const JPH::CharacterContact& contact : character->GetActiveContacts())
        {
            if (!contact.mHadCollision || !IsWalkableGroundNormal(character, settings, contact.mSurfaceNormal))
                continue;

            const f32 upDot = contact.mSurfaceNormal.Dot(up);
            if (upDot <= bestUpDot)
                continue;

            groundNormal = contact.mSurfaceNormal;
            bestUpDot = upDot;
        }

        return groundNormal;
    }

    JPH::Vec3 BuildGroundSlopeVelocity(JPH::CharacterVirtual* character, const ECS::Singletons::CharacterControllerSettings& settings, const JPH::Vec3Arg& planarVelocity, const JPH::Vec3Arg& groundNormal)
    {
        if (!IsWalkableGroundNormal(character, settings, groundNormal))
            return JPH::Vec3::sZero();

        const JPH::Vec3 up = ToJolt(settings.up);
        const f32 groundUpDot = groundNormal.Dot(up);
        if (groundUpDot <= 1.0e-4f)
            return JPH::Vec3::sZero();

        const f32 slopeVerticalSpeed = -planarVelocity.Dot(groundNormal) / groundUpDot;
        return up * slopeVerticalSpeed;
    }

    void UpdateGroundSnapGrace(ECS::Singletons::CharacterControllerSingleton& state, const ECS::Singletons::CharacterControllerSettings& settings, bool isGrounded, f32 fixedDeltaTime)
    {
        if (isGrounded)
        {
            state.groundSnapGraceTimer = settings.groundSnapGraceTime;
            return;
        }

        state.groundSnapGraceTimer = glm::max(0.0f, state.groundSnapGraceTimer - fixedDeltaTime);
    }

    bool ShouldSnapToGround(const ECS::Singletons::CharacterControllerSingleton& state, const ECS::Singletons::CharacterControllerSettings& settings, const JPH::Vec3Arg& persistentVelocity, bool isFlying, bool justStartedJump)
    {
        if (isFlying || justStartedJump || settings.groundSnapDistance <= 0.0f || state.groundSnapGraceTimer <= 0.0f)
            return false;

        const JPH::Vec3 up = ToJolt(settings.up);
        const f32 verticalSpeed = persistentVelocity.Dot(up);
        return verticalSpeed <= 1.0e-3f && verticalSpeed >= -settings.groundSnapMaxDownVelocity;
    }

    bool ShouldPreserveSteepSlopeJumpVelocity(const ECS::Singletons::CharacterControllerSingleton& state, const ECS::Singletons::CharacterControllerSettings& settings, const JPH::Vec3Arg& velocity, bool isFlying, bool isJumping, bool justStartedJump)
    {
        if (isFlying)
            return false;

        if (justStartedJump)
            return true;

        if (!isJumping && !state.preserveSteepSlopeJumpVelocityUntilFalling)
            return false;

        const JPH::Vec3 up = ToJolt(settings.up);
        return velocity.Dot(up) > 1.0e-3f;
    }

    bool HasRisingHeadContact(const ECS::Singletons::CharacterControllerSingleton& state, const ECS::Singletons::CharacterControllerSettings& settings, const JPH::Vec3Arg& velocity, bool isFlying, bool isJumping)
    {
        if (!state.character || isFlying || !isJumping)
            return false;

        const JPH::Vec3 up = ToJolt(settings.up);
        if (velocity.Dot(up) <= 1.0e-3f)
            return false;

        constexpr f32 maxUpDot = -0.55f;
        for (const JPH::CharacterContact& contact : state.character->GetActiveContacts())
        {
            if (!contact.mHadCollision)
                continue;

            if (contact.mContactNormal.Dot(up) < maxUpDot || contact.mSurfaceNormal.Dot(up) < maxUpDot)
                return true;
        }

        return false;
    }

    JPH::Vec3 RemoveUpwardVelocity(const ECS::Singletons::CharacterControllerSettings& settings, const JPH::Vec3Arg& velocity)
    {
        const JPH::Vec3 up = ToJolt(settings.up);
        const f32 verticalSpeed = velocity.Dot(up);
        return verticalSpeed > 0.0f ? velocity - up * verticalSpeed : velocity;
    }

    JPH::Vec3 GetGroundSnapStepDown(const ECS::Singletons::CharacterControllerSettings& settings)
    {
        return -ToJolt(settings.up) * settings.groundSnapDistance;
    }

    bool IsOnGround(const JPH::CharacterVirtual* character)
    {
        return character->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
    }
}
