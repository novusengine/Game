#include "GameHandler.h"
#include "Game-Lib/Scripting/Game/Container.h"

#include <Base/Util/StringUtils.h>

#include <lualib.h>

namespace Scripting::Game
{
    void GameHandler::Register(lua_State* state)
    {
        Container::Register(state);
    }

    void GameHandler::Clear()
    {
    }
}
