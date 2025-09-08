#pragma once
#include <Base/Types.h>

#include <Scripting/Defines.h>
#include <Scripting/LuaMethodTable.h>

namespace Scripting::Database
{
    struct Item
    {
    public:
        static void Register(Zenith* zenith);
    };

    namespace ItemMethods
    {
        i32 GetItemInfo(Zenith* zenith);
        i32 GetItemStatInfo(Zenith* zenith);
        i32 GetItemArmorInfo(Zenith* zenith);
        i32 GetItemWeaponInfo(Zenith* zenith);
        i32 GetItemShieldInfo(Zenith* zenith);
        i32 GetItemDisplayInfo(Zenith* zenith);
        i32 GetItemEffects(Zenith* zenith);

        i32 GetIconInfo(Zenith* zenith);
    };


    static LuaRegister<> itemGlobalFunctions[] =
    {
        { "GetItemInfo", ItemMethods::GetItemInfo },
        { "GetItemStatInfo", ItemMethods::GetItemStatInfo },
        { "GetItemArmorInfo", ItemMethods::GetItemArmorInfo },
        { "GetItemWeaponInfo", ItemMethods::GetItemWeaponInfo },
        { "GetItemShieldInfo", ItemMethods::GetItemShieldInfo },
        { "GetItemDisplayInfo", ItemMethods::GetItemDisplayInfo },
        { "GetItemEffects", ItemMethods::GetItemEffects },
        { "GetIconInfo", ItemMethods::GetIconInfo }
    };
}