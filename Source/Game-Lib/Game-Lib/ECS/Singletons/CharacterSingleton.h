#pragma once
#include <Gameplay/GameDefine.h>

#include <entt/fwd.hpp>

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
        entt::entity controllerEntity;
        entt::entity moverEntity;

        JPH::CharacterVirtual* character = nullptr;
        KeybindGroup* keybindGroup = nullptr;
        KeybindGroup* cameraToggleKeybindGroup = nullptr;

        bool canControlInAir = true;

        entt::entity baseContainerEntity;
        std::array<GameDefine::ObjectGuid, 6> containers;
    };
}