#pragma once
#include <entt/fwd.hpp>

namespace ECS::Util::UIRefCleanup
{
    // entt on_destroy observers attached at UI registry construction
    // (Application.cpp). Each iterates the i32 Lua-registry refs on the
    // outgoing component and lua_unref's the populated ones, so callbacks
    // installed via SetOnMouseUp / RegisterLayoutRefresh / etc. don't
    // leak the registry slot when the widget entity is destroyed.
    void ReleaseEventInputInfoRefs(entt::registry& registry, entt::entity entity);
    void ReleaseLayoutEventInfoRefs(entt::registry& registry, entt::entity entity);
}
