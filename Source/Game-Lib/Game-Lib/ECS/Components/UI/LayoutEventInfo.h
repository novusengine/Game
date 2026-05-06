#pragma once
#include <Base/Types.h>

namespace ECS::Components::UI
{
    struct LayoutEventInfo
    {
    public:
        // Lua function ref (LUA_REGISTRYINDEX) of the layout's refresh callback,
        // installed by Lua-side LinearLayout/GridLayout via Widget:RegisterLayoutRefresh.
        // Cleared on entity destroy by ReleaseLayoutEventInfoRefs (UIRefCleanup).
        i32 onLayoutRefresh = -1;
    };

    struct DirtyLayoutTag {};
}
