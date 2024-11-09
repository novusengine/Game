#pragma once
#include "LuaSystemBase.h"

#include <Base/Types.h>

namespace Scripting
{
    class GenericSystem : public LuaSystemBase
    {
    public:
        GenericSystem();

        void Prepare(f32 deltaTime, lua_State* state);
        void Run(f32 deltaTime, lua_State* state);
    };
}