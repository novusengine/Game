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
        static i32 GetHovered(Zenith* zenith);
        static i32 GetName(Zenith* zenith);
        static i32 GetHealth(Zenith* zenith);
        static i32 GetLevel(Zenith* zenith);
        static i32 GetClass(Zenith* zenith);
        static i32 GetResourceType(Zenith* zenith);
        static i32 GetResource(Zenith* zenith);
        static i32 GetStat(Zenith* zenith);
        static i32 GetAuras(Zenith* zenith);
        static i32 GetFactionID(Zenith* zenith);
        static i32 GetReaction(Zenith* zenith);
        static i32 CanAttack(Zenith* zenith);
        static i32 GetLocalReactionToUnit(Zenith* zenith);
        static i32 GetUnitReactionToLocalPlayer(Zenith* zenith);
        static i32 GetPersistentReputation(Zenith* zenith);
        static i32 GetPersistentStanding(Zenith* zenith);
        static i32 GetEffectiveStanding(Zenith* zenith);
        static i32 ClearTarget(Zenith* zenith);

        static i32 SetWidgetToNamePos(Zenith* zenith);
    };

    static LuaRegister<> unitGlobalMethods[] =
    {
        { "GetLocal", UnitHandler::GetLocal },
        { "GetHovered", UnitHandler::GetHovered },
        { "GetName", UnitHandler::GetName },
        { "GetHealth", UnitHandler::GetHealth },
        { "GetLevel", UnitHandler::GetLevel },
        { "GetClass", UnitHandler::GetClass },
        { "GetResourceType", UnitHandler::GetResourceType },
        { "GetResource", UnitHandler::GetResource },
        { "GetStat", UnitHandler::GetStat },
        { "GetAuras", UnitHandler::GetAuras },
        { "GetFactionID", UnitHandler::GetFactionID },
        { "GetReaction", UnitHandler::GetReaction },
        { "CanAttack", UnitHandler::CanAttack },
        { "GetLocalReactionToUnit", UnitHandler::GetLocalReactionToUnit },
        { "GetUnitReactionToLocalPlayer", UnitHandler::GetUnitReactionToLocalPlayer },
        { "GetPersistentReputation", UnitHandler::GetPersistentReputation },
        { "GetPersistentStanding", UnitHandler::GetPersistentStanding },
        { "GetEffectiveStanding", UnitHandler::GetEffectiveStanding },
        { "ClearTarget", UnitHandler::ClearTarget },

        { "SetWidgetToNamePos", UnitHandler::SetWidgetToNamePos }
    };
}
