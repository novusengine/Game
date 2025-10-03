#pragma once
#include <Base/Types.h>

#include <Scripting/Defines.h>
#include <Scripting/LuaMethodTable.h>

namespace Scripting::Unit
{
    class UnitHandler : public LuaHandlerBase
    {
    public:
        void Register(Zenith* zenith);
        void Clear(Zenith* zenith) {}

        void PostLoad(Zenith* zenith);
        void Update(Zenith* zenith, f32 deltaTime) {}

    public: // Registered Functions
        static i32 GetLocal(Zenith* zenith);
        static i32 GetName(Zenith* zenith);
        static i32 GetHealth(Zenith* zenith);
        static i32 GetClass(Zenith* zenith);
        static i32 GetResourceType(Zenith* zenith);
        static i32 GetResource(Zenith* zenith);
        static i32 GetStat(Zenith* zenith);
        static i32 GetAuras(Zenith* zenith);

        static i32 SetWidgetToNamePos(Zenith* zenith);
    };

    static LuaRegister<> unitGlobalMethods[] =
    {
        { "GetLocal", UnitHandler::GetLocal },
        { "GetName", UnitHandler::GetName },
        { "GetHealth", UnitHandler::GetHealth },
        { "GetClass", UnitHandler::GetClass },
        { "GetResourceType", UnitHandler::GetResourceType },
        { "GetResource", UnitHandler::GetResource },
        { "GetStat", UnitHandler::GetStat },
        { "GetAuras", UnitHandler::GetAuras },

        { "SetWidgetToNamePos", UnitHandler::SetWidgetToNamePos }
    };
}