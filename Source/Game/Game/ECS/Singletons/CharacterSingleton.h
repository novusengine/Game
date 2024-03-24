#pragma once
#include <entt/fwd.hpp>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>

class KeybindGroup;

namespace Animation
{
    enum class Type;
}
namespace ECS::Singletons
{
    enum class JumpState
    {
        None,
        Begin,
        Fall,
        End
    };
    struct CharacterSingleton
    {
    public:
        entt::entity entity;
        entt::entity modelEntity;

        JPH::CharacterVirtual* character = nullptr;
        KeybindGroup* keybindGroup = nullptr;
        KeybindGroup* cameraToggleKeybindGroup = nullptr;

        // These rotation offsets are specifically for Y axis only
        vec4 spineRotationSettings = vec4(0.0f);    // x = current, y = target, z = time to change, w = time since last change
        vec4 headRotationSettings = vec4(0.0f);	    // x = current, y = target, z = time to change, w = time since last change
        vec4 waistRotationSettings = vec4(0.0f);    // x = current, y = target, z = time to change, w = time since last change

        f32 speed = 7.1111f;
        JumpState jumpState = JumpState::None;

        struct MovementFlags
        {
            u32 forward : 1;
            u32 backward : 1;
            u32 left : 1;
            u32 right : 1;
        };

        MovementFlags movementFlags;
    };
}