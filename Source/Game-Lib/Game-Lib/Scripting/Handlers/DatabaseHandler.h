#pragma once
#include "LuaEventHandlerBase.h"
#include "Game-Lib/Scripting/LuaDefines.h"
#include "Game-Lib/Scripting/LuaMethodTable.h"

#include <Base/Types.h>

namespace Scripting::Database
{
    class DatabaseHandler : public LuaHandlerBase
    {
    public:
        void Register(lua_State* state) override;
        void Clear() override;
    };
}