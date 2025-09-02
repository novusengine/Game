#pragma once
#include <Base/Types.h>

struct lua_State;

namespace Scripting
{
    class LuaHandlerBase
    {
    public:
        virtual void Register(lua_State* state) = 0;
        virtual void PostLoad(lua_State* state) {}
        virtual void Clear() = 0;
    };
}