#include "DatabaseHandler.h"
#include "Game-Lib/Scripting/Database/Item.h"
#include "Game-Lib/Scripting/Database/Spell.h"

#include <lualib.h>

namespace Scripting::Database
{
    void DatabaseHandler::Register(lua_State* state)
    {
        Item::Register(state);
        Spell::Register(state);
    }

    void DatabaseHandler::Clear()
    {
    }
}
