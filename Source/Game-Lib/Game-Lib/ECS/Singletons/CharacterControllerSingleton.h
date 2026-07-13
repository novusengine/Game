#pragma once

#include <Base/Types.h>

#include <entt/entt.hpp>
#include <glm/trigonometric.hpp>

class KeybindGroup;

namespace JPH
{
    class CharacterVirtual;
}

namespace ECS::Singletons
{
    enum class CharacterMotorType : u8
    {
        Ground,
        Flight,
        Swim,
        Vehicle
    };

    struct CharacterMovementIntent
    {
    public:
        u8 moveForward : 1 = 0;
        u8 moveBackward : 1 = 0;
        u8 strafeLeft : 1 = 0;
        u8 strafeRight : 1 = 0;
        u8 jumpOrAscend : 1 = 0;
        u8 descend : 1 = 0;
        u8 autorun : 1 = 0;
        u8 mouseForward : 1 = 0;

        bool operator==(const CharacterMovementIntent&) const = default;
    };

    struct CharacterControlMask
    {
    public:
        u8 allowYaw : 1 = 1;
        u8 allowPitch : 1 = 1;
        u8 allowForwardBack : 1 = 1;
        u8 allowStrafe : 1 = 1;
        u8 allowJump : 1 = 1;
        u8 allowAscendDescend : 1 = 1;
        u8 allowModeTransitions : 1 = 1;
        u8 allowForcedForward : 1 = 1;
    };

    enum class CharacterControllerGroundDebugState : u8
    {
        OnGround,
        OnSteepGround,
        NotSupported,
        InAir
    };

    struct CharacterControllerDebugSingleton
    {
    public:
        vec3 position = vec3(0.0f);
        vec3 inputVelocity = vec3(0.0f);
        vec3 persistentVelocity = vec3(0.0f);
        vec3 solveVelocity = vec3(0.0f);
        vec3 actualVelocity = vec3(0.0f);
        vec3 groundNormal = vec3(0.0f);
        vec3 movementGroundNormal = vec3(0.0f);
        vec3 snapStepDown = vec3(0.0f);
        vec3 supportProbeStart = vec3(0.0f);
        vec3 supportProbeEnd = vec3(0.0f);
        vec3 supportProbeHitPosition = vec3(0.0f);
        vec3 supportProbeNormal = vec3(0.0f);
        f32 groundSnapGraceTimer = 0.0f;
        CharacterControllerGroundDebugState groundState = CharacterControllerGroundDebugState::InAir;
        u8 supportProbeActive : 1 = 0;
        u8 supportProbeHit : 1 = 0;
        u8 supportProbeWalkable : 1 = 0;
        u8 supportProbeUsedForGrounding : 1 = 0;
        u8 valid : 1 = 0;
    };

    struct CharacterControllerSettings
    {
    public:
        f32 collisionHalfWidth = 0.41666671634f;
        f32 collisionHeight = 1.91345489025f;
        f32 shapeConvexRadius = 0.05f;

        f32 gravity = -19.291105f;
        f32 neutralJumpAirControlMultiplier = 0.6f;

        f32 maxWalkableSlopeAngleRadians = glm::radians(50.0f);
        f32 groundSnapDistance = 0.5f;
        f32 groundSnapGraceTime = 0.1f;
        f32 groundSnapMaxDownVelocity = 6.0f;
        f32 flightGroundProbeStartOffset = 0.25f;
        f32 flightGroundProbeDistance = 0.75f;
        f32 predictiveContactDistance = 0.2f;
        f32 penetrationRecoverySpeed = 1.0f;
        f32 walkStairsStepUp = 1.1918f;
        f32 walkStairsMinStepForward = 0.02f;
        f32 walkStairsStepForwardTest = 0.1f;
        f32 walkStairsForwardContactAngleRadians = glm::radians(50.0f);
        f32 characterMass = 1000000.0f;

        vec3 up = vec3(0.0f, 1.0f, 0.0f);
        u8 maxSubsteps = 4;
        u8 enhancedInternalEdgeRemoval : 1 = 0;
    };

    struct CharacterControllerSingleton
    {
    public:
        JPH::CharacterVirtual* character = nullptr;
        KeybindGroup* keybindGroup = nullptr;

        f32 groundSnapGraceTimer = 0.0f;
        f32 appliedPitch = 0.0f;
        f32 appliedYaw = 0.0f;

        entt::entity controllerEntity = entt::null;
        entt::entity moverEntity = entt::null;
        entt::entity hoveredEntity = entt::null;

        CharacterMovementIntent intent;
        CharacterControlMask controlMask;
        CharacterMotorType activeMotor = CharacterMotorType::Ground;

        u8 initialized : 1 = 0;
        u8 autorunEnabled : 1 = 0;
        u8 neutralJumpAirControlAvailable : 1 = 0;
        u8 neutralJumpAirControlConsumed : 1 = 0;
        u8 preserveSteepSlopeJumpVelocityUntilFalling : 1 = 0;
    };

    static_assert(sizeof(CharacterMovementIntent) == sizeof(u8));
    static_assert(sizeof(CharacterControlMask) == sizeof(u8));
    static_assert(sizeof(CharacterControllerDebugSingleton) <= 152);
    static_assert(sizeof(CharacterControllerSettings) <= 128);
    static_assert(sizeof(CharacterControllerSingleton) <= 48);
}
