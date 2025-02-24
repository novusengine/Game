#include "Box.h"

#include "Game-Lib/Scripting/LuaState.h"
#include "Game-Lib/UI/Box.h"

namespace Scripting::UI
{
    static LuaMethod boxStaticFunctions[] =
    {
        { "new", BoxMethods::CreateBox }
    };

    //static LuaMethod boxMethods[] =
    //{
    //    { nullptr, nullptr }
    //};

    void Box::Register(lua_State* state)
    {
        LuaMethodTable::Set(state, boxStaticFunctions, "Box");

        LuaMetaTable<Box>::Register(state, "BoxMetaTable");
        //LuaMetaTable<Box>::Set(state, boxMethods);
    }

    namespace BoxMethods
    {
        i32 CreateBox(lua_State* state)
        {
            LuaState ctx(state);

            f32 minX = ctx.Get(0.0f, 1);
            f32 minY = ctx.Get(0.0f, 2);
            f32 maxX = ctx.Get(1.0f, 3);
            f32 maxY = ctx.Get(1.0f, 4);

            ::UI::Box* box = ctx.PushUserData<::UI::Box>([](void* x)
            {

            });
            box->min = vec2(minX, minY);
            box->max = vec2(maxX, maxY);

            luaL_getmetatable(state, "BoxMetaTable");
            lua_setmetatable(state, -2);

            return 1;
        }
    }
}