#include "DatabaseHandler.h"
#include "Game-Lib/Scripting/Database/Item.h"
#include "Game-Lib/Scripting/Database/Spell.h"

#include <lualib.h>

namespace Scripting::Database
{
    void DatabaseHandler::Register(Zenith* zenith)
    {
        Item::Register(zenith);
        Spell::Register(zenith);
    }
}
