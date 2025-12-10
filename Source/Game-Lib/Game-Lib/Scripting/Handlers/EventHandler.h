#pragma once
#include <Base/Types.h>

#include <Network/Define.h>

#include <Scripting/Defines.h>
#include <Scripting/LuaMethodTable.h>

#include <robinhood/robinhood.h>

namespace Scripting
{
    class EventHandler : public LuaHandlerBase
    {
    private:
        void Register(Zenith* zenith);
        void Clear(Zenith* zenith) {}

        void PostLoad(Zenith* zenith) {}
        void Update(Zenith* zenith, f32 deltaTime) {}

    public: // Registered Functions
        static i32 RegisterEventHandler(Zenith* zenith);

    private: // Utility Functions
        void CreateEventTables(Zenith* zenith);
    };

    static LuaRegister<> EventHandlerGlobalMethods[] =
    {
        { "RegisterEvent", EventHandler::RegisterEventHandler },
    };
}