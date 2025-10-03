#pragma once
#include <Base/Types.h>

#include <Scripting/Defines.h>
#include <Scripting/LuaMethodTable.h>

namespace Scripting::Database
{
    struct Spell
    {
    public:
        static void Register(Zenith* zenith);
    };

    namespace SpellMethods
    {
        i32 GetSpellInfo(Zenith* zenith);
        i32 GetIconInfo(Zenith* zenith);
        i32 CastByID(Zenith* zenith);
    };

    static LuaRegister<> spellGlobalFunctions[] =
    {
        { "GetSpellInfo", SpellMethods::GetSpellInfo },
        { "GetIconInfo", SpellMethods::GetIconInfo },
        { "CastByID", SpellMethods::CastByID }
    };
}