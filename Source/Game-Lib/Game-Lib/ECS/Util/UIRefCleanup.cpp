#include "UIRefCleanup.h"
#include "Game-Lib/ECS/Components/UI/EventInputInfo.h"
#include "Game-Lib/ECS/Components/UI/LayoutEventInfo.h"
#include "Game-Lib/Scripting/Util/ZenithUtil.h"

#include <entt/entt.hpp>

namespace ECS::Util::UIRefCleanup
{
    void ReleaseEventInputInfoRefs(entt::registry& registry, entt::entity entity)
    {
        // Shutdown order: the registry may destroy entities after the LuaManager
        // is gone. Skip rather than crash; the Lua state is being torn down anyway.
        ::Scripting::Zenith* zenith = Scripting::Util::Zenith::GetGlobal();
        if (zenith == nullptr) return;

        auto& info = registry.get<ECS::Components::UI::EventInputInfo>(entity);
        Scripting::Util::Zenith::Unref(zenith, info.onMouseDownEvent);
        Scripting::Util::Zenith::Unref(zenith, info.onMouseUpEvent);
        Scripting::Util::Zenith::Unref(zenith, info.onMouseHeldEvent);
        Scripting::Util::Zenith::Unref(zenith, info.onMouseScrollEvent);
        Scripting::Util::Zenith::Unref(zenith, info.onHoverBeginEvent);
        Scripting::Util::Zenith::Unref(zenith, info.onHoverEndEvent);
        Scripting::Util::Zenith::Unref(zenith, info.onHoverHeldEvent);
        Scripting::Util::Zenith::Unref(zenith, info.onFocusBeginEvent);
        Scripting::Util::Zenith::Unref(zenith, info.onFocusEndEvent);
        Scripting::Util::Zenith::Unref(zenith, info.onFocusHeldEvent);
        Scripting::Util::Zenith::Unref(zenith, info.onKeyboardEvent);
    }

    void ReleaseLayoutEventInfoRefs(entt::registry& registry, entt::entity entity)
    {
        ::Scripting::Zenith* zenith = Scripting::Util::Zenith::GetGlobal();
        if (zenith == nullptr) return;

        auto& info = registry.get<ECS::Components::UI::LayoutEventInfo>(entity);
        Scripting::Util::Zenith::Unref(zenith, info.onLayoutRefresh);
    }
}
