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

    void Unref(::Scripting::Zenith* zenith, i32 ref)
    {
        if (ref == -1) return;
        // lua_State is transiently null between Zenith::Clear and the next LoadBytecode.
        if (zenith == nullptr || zenith->state == nullptr) return;
        lua_unref(zenith->state, ref);
    }
}
