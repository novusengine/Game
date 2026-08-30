#include "Box.h"
#include "Game-Lib/UI/Box.h"

#include <Scripting/Zenith.h>

namespace Scripting::UI
{
    void Box::Register(Zenith* zenith)
    {
        LuaMethodTable::Set(zenith, boxGlobalMethods, "Box");

        LuaMetaTable<Box>::Register(zenith, "BoxMetaTable");
    }

    namespace BoxMethods
    {
        i32 CreateBox(Zenith* zenith)
        {
            const vec3 min = zenith->CheckVal<vec3>(1);
            const vec3 max = zenith->CheckVal<vec3>(2);

            ::UI::Box* box = zenith->PushUserData<::UI::Box>([](void* x) {});
            box->min = vec2(min.x, min.y);
            box->max = vec2(max.x, max.y);

            luaL_getmetatable(zenith->state, "BoxMetaTable");
            lua_setmetatable(zenith->state, -2);

            return 1;
        }
    }
}
