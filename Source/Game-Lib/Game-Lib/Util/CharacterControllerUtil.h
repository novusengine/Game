#pragma once

#include "Game-Lib/ECS/Singletons/CharacterControllerSingleton.h"

#include <Jolt/Jolt.h>
#include <Jolt/Math/Vec3.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

namespace ECS::Singletons
{
    struct JoltState;
}

namespace ECS::Components
{
    struct MovementInfo;
}

namespace Util::CharacterController
{
    struct BoxPyramidShapeDimensions
    {
    public:
        f32 collisionHalfWidth = 0.0f;
        f32 collisionHeight = 0.0f;
        f32 pyramidHeight = 0.0f;
    };

    struct CharacterBlockingContact
    {
    public:
        JPH::Vec3 position = JPH::Vec3::sZero();
        JPH::Vec3 surfaceNormal = JPH::Vec3::sZero();
        JPH::Vec3 contactVelocity = JPH::Vec3::sZero();
        JPH::Vec3 movementDirection = JPH::Vec3::sZero();

        f32 height = 0.0f;
        f32 alignment = 0.0f;
    };

    JPH::Vec3 ToJolt(const vec3& value);
    vec3 FromJolt(const JPH::Vec3Arg& value);

    JPH::Vec3 GetPlanarVelocity(const ECS::Singletons::CharacterControllerSettings& settings, const JPH::Vec3Arg& velocity);
    f32 GetVerticalSpeed(const ECS::Singletons::CharacterControllerSettings& settings, const JPH::Vec3Arg& velocity);
    JPH::Vec3 ComposeVelocity(const ECS::Singletons::CharacterControllerSettings& settings, const JPH::Vec3Arg& planarVelocity, f32 verticalSpeed);

    BoxPyramidShapeDimensions GetBoxPyramidShapeDimensionsFromCollision(f32 collisionWidth, f32 collisionHeight);
    BoxPyramidShapeDimensions GetBoxPyramidShapeDimensions(const ECS::Singletons::CharacterControllerSettings& settings);
    BoxPyramidShapeDimensions EstimateBoxPyramidShapeDimensionsFromGeoBox(const vec3& geoBoxMin, const vec3& geoBoxMax);
    JPH::ShapeRefC CreateBoxPyramidShape(const BoxPyramidShapeDimensions& dimensions, f32 convexRadius);
    JPH::ShapeRefC CreateBoxPyramidShape(const ECS::Singletons::CharacterControllerSettings& settings);

    bool HasMovementInput(const ECS::Singletons::CharacterMovementIntent& intent);
    void ResetMovementInput(ECS::Singletons::CharacterControllerSingleton& state, ECS::Components::MovementInfo* movementInfo);
    JPH::Vec3 BuildCharacterRelativeMoveDirection(const ECS::Singletons::CharacterMovementIntent& intent, const quat& characterRotation);
    f32 GetPlanarSpeed(const ECS::Components::MovementInfo& movementInfo, const ECS::Singletons::CharacterMovementIntent& intent, ECS::Singletons::CharacterMotorType motor);

    void ResetGroundProbeDebug(ECS::Singletons::CharacterControllerDebugSingleton& debugState);
    bool IsWalkableGroundNormal(JPH::CharacterVirtual* character, const ECS::Singletons::CharacterControllerSettings& settings, const JPH::Vec3Arg& groundNormal);
    bool TryGetWalkableGroundProbeNormal(JPH::CharacterVirtual* character, const ECS::Singletons::CharacterControllerSettings& settings, ECS::Singletons::JoltState& joltState, const JPH::Vec3Arg& position, f32 startOffset, f32 distance, JPH::Vec3& outGroundNormal, JPH::Vec3* outHitPosition = nullptr, ECS::Singletons::CharacterControllerDebugSingleton* debugState = nullptr);
    JPH::Vec3 GetWalkSupportNormal(JPH::CharacterVirtual* character, const ECS::Singletons::CharacterControllerSettings& settings);
    bool TryGetBlockingContact(JPH::CharacterVirtual* character, const ECS::Singletons::CharacterControllerSettings& settings, const JPH::Vec3Arg& desiredVelocity, CharacterBlockingContact& outContact);
    JPH::Vec3 BuildGroundSlopeVelocity(JPH::CharacterVirtual* character, const ECS::Singletons::CharacterControllerSettings& settings, const JPH::Vec3Arg& planarVelocity, const JPH::Vec3Arg& groundNormal);
    bool ShouldPreserveSteepSlopeJumpVelocity(const ECS::Singletons::CharacterControllerSingleton& state, const ECS::Singletons::CharacterControllerSettings& settings, const JPH::Vec3Arg& velocity, bool isFlying, bool isJumping, bool justStartedJump);
    bool HasRisingHeadContact(const ECS::Singletons::CharacterControllerSingleton& state, const ECS::Singletons::CharacterControllerSettings& settings, const JPH::Vec3Arg& velocity, bool isFlying, bool isJumping);

    JPH::Vec3 RemoveUpwardVelocity(const ECS::Singletons::CharacterControllerSettings& settings, const JPH::Vec3Arg& velocity);
    JPH::Vec3 GetGroundSnapStepDown(const ECS::Singletons::CharacterControllerSettings& settings);

    bool IsOnGround(const JPH::CharacterVirtual* character);
    f32 CalculateContinuationDistance(const ECS::Singletons::CharacterControllerSettings& settings, const JPH::Vec3Arg& velocity);
}
