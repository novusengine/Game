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
    struct CharacterSingleton
    {
    public:
        entt::entity entity;
        entt::entity modelEntity;
        entt::entity targetEntity;

        JPH::CharacterVirtual* character = nullptr;
        KeybindGroup* keybindGroup = nullptr;
        KeybindGroup* cameraToggleKeybindGroup = nullptr;

        bool canControlInAir = true;
        bool positionOrRotationIsDirty = false;
        f32 positionOrRotationUpdateTimer = 0.0f;

        // These rotation offsets are specifically for Y axis only
        vec4 spineRotationSettings = vec4(0.0f);    // x = current, y = target, z = time to change, w = time since last change
        vec4 headRotationSettings = vec4(0.0f);	    // x = current, y = target, z = time to change, w = time since last change
        vec4 waistRotationSettings = vec4(0.0f);    // x = current, y = target, z = time to change, w = time since last change
    };
}