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
            f32 minX = zenith->CheckVal<f32>(1);
            f32 minY = zenith->CheckVal<f32>(2);
            f32 maxX = zenith->CheckVal<f32>(3);
            f32 maxY = zenith->CheckVal<f32>(4);

            ::UI::Box* box = zenith->PushUserData<::UI::Box>([](void* x) {});
            box->min = vec2(minX, minY);
            box->max = vec2(maxX, maxY);

            luaL_getmetatable(zenith->state, "BoxMetaTable");
            lua_setmetatable(zenith->state, -2);

            return 1;
        }
    }
}