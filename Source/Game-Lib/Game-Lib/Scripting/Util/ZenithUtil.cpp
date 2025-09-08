#include "ZenithUtil.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Scripting/LuaManager.h>
#include <Scripting/Zenith.h>

namespace Scripting::Util::Zenith
{
    ::Scripting::Zenith* GetGlobal()
    {
        LuaManager* luaManager = ServiceLocator::GetLuaManager();

        ZenithInfoKey globalKey = ZenithInfoKey::MakeGlobal(0, 0);
        return luaManager->GetZenithStateManager().Get(globalKey);
    }
}
