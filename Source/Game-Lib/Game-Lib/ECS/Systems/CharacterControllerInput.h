#pragma once

#include <Base/Types.h>
#include <Input/KeybindGroup.h>

#include <entt/fwd.hpp>

namespace ECS::Systems::CharacterControllerInput
{
    void UpdateHoveredUnit(entt::registry& registry, f32 deltaTime);
    void UpdateAutoAttack(entt::registry& registry, f32 deltaTime);
    bool ClearTarget();
    bool HandleTargetInput(i32 key, KeybindAction action, KeybindModifier modifier);
}
