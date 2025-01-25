#include "DatabaseHandler.h"
#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/Scripting/LuaState.h"
#include "Game-Lib/Scripting/LuaManager.h"
#include "Game-Lib/Scripting/Database/Item.h"
#include "Game-Lib/Scripting/Systems/LuaSystemBase.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/Util/StringUtils.h>

#include <Input/KeybindGroup.h>

#include <entt/entt.hpp>
#include <lualib.h>

namespace Scripting::Database
{
    void DatabaseHandler::Register(lua_State* state)
    {
        Item::Register(state);
    }

    void DatabaseHandler::Clear()
    {
    }
}
