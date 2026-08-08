#pragma once

#include <Base/Types.h>

#include "Game-Lib/Input/InputActionSystem.h"

#include <entt/fwd.hpp>

namespace ECS::Systems::CharacterControllerInput
{
    void UpdateHoveredUnit(entt::registry& registry, f32 deltaTime);
    bool ClearTarget();
    bool SetTarget(entt::entity targetEntity, bool updateAutoAttackState = false, bool autoAttackEnabled = false);
    InputReply HandleTargetInput(const InputActionEvent& event);
}
