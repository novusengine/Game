#pragma once

#include <Base/Types.h>

#include "Game-Lib/Input/InputActionSystem.h"

#include <entt/fwd.hpp>

namespace ECS::Systems::CharacterControllerInput
{
    void UpdateHoveredUnit(entt::registry& registry, f32 deltaTime);
    void UpdateAutoAttack(entt::registry& registry, f32 deltaTime);
    bool ClearTarget();
    InputReply HandleTargetInput(const InputActionEvent& event);
}
