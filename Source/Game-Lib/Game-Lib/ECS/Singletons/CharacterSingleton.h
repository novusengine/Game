#pragma once
#include <Gameplay/GameDefine.h>

#include <entt/entt.hpp>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>

#include <array>

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
        entt::entity controllerEntity = entt::null;
        entt::entity moverEntity = entt::null;
        f32 primaryAttackTimer = 0.0f;
        f32 secondaryAttackTimer = 0.0f;

        JPH::CharacterVirtual* character = nullptr;
        KeybindGroup* keybindGroup = nullptr;
        KeybindGroup* cameraToggleKeybindGroup = nullptr;

        bool canControlInAir = true;

        entt::entity baseContainerEntity = entt::null;
        std::array<ObjectGUID, 6> containers;
    };
}