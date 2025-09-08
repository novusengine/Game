#include "GameHandler.h"
#include "Game-Lib/Scripting/Game/Container.h"

#include <lualib.h>

namespace Scripting::Game
{
    void GameHandler::Register(Zenith* zenith)
    {
        Scripting::Game::Container::Register(zenith);
    }
}
