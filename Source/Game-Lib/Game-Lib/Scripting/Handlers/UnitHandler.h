#pragma once
#include "LuaEventHandlerBase.h"
#include "Game-Lib/Scripting/LuaDefines.h"
#include "Game-Lib/Scripting/LuaMethodTable.h"

#include <Base/Types.h>

namespace Scripting
{
    class UnitHandler : public LuaHandlerBase
    {
    public:
        void Register(lua_State* state) override;
        void PostLoad(lua_State* state) override;
        void Clear() override;

    public:
       

    public: // Registered Functions
        static i32 GetLocal(lua_State* state);
        static i32 GetName(lua_State* state);
        static i32 SetWidgetToNamePos(lua_State* state);
    };
}