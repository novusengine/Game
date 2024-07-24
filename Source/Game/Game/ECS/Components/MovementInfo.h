#pragma once

#include <Base/Types.h>

namespace ECS::Components
{
    struct MovementFlags
    {
        u32 forward : 1 = 0;
        u32 backward : 1 = 0;
        u32 left : 1 = 0;
        u32 right : 1 = 0;
        u32 grounded : 1 = 0;
        u32 jumping : 1 = 0;
        u32 justGrounded : 1 = 0;
        u32 justEndedJump : 1 = 0;
    };

    enum class JumpState : u8
    {
        None,
        Begin,
        Jumping,
        Fall
    };

    struct MovementInfo
    {
        f32 pitch = 0.0f;
        f32 yaw = 0.0f;

        f32 speed = 7.1111f;
        f32 jumpSpeed = 7.9555f;
        f32 gravityModifier = 1.0f;

        vec2 horizontalVelocity = vec2(0.0f);
        f32 verticalVelocity = 0.0f;

        MovementFlags movementFlags;
        JumpState jumpState = JumpState::None;
    };
}