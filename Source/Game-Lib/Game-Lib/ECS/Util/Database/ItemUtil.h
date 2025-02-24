#pragma once
#include "Game-Lib/ECS/Singletons/Database/ItemSingleton.h"

#include <Base/Types.h>

#include <Gameplay/GameDefine.h>

namespace ECS
{
    namespace Util::Database::Item
    {
        bool Refresh();

        bool ItemHasStatTemplate(const Singletons::Database::ItemSingleton& itemSingleton, u32 itemID);
        u32 GetItemStatTemplateID(Singletons::Database::ItemSingleton& itemSingleton, u32 itemID);

        bool ItemHasArmorTemplate(const Singletons::Database::ItemSingleton& itemSingleton, u32 itemID);
        u32 GetItemArmorTemplateID(Singletons::Database::ItemSingleton& itemSingleton, u32 itemID);

        bool ItemHasWeaponTemplate(const Singletons::Database::ItemSingleton& itemSingleton, u32 itemID);
        u32 GetItemWeaponTemplateID(Singletons::Database::ItemSingleton& itemSingleton, u32 itemID);

        bool ItemHasShieldTemplate(const Singletons::Database::ItemSingleton& itemSingleton, u32 itemID);
        u32 GetItemShieldTemplateID(Singletons::Database::ItemSingleton& itemSingleton, u32 itemID);

        bool ItemHasAnyEffects(const Singletons::Database::ItemSingleton& itemSingleton, u32 itemID);
        const u32* GetItemEffectIDs(Singletons::Database::ItemSingleton& itemSingleton, u32 itemID, u32& count);

        u32 GetModelHashForHelm(Singletons::Database::ItemSingleton& itemSingleton, u32 helmModelResourcesID, GameDefine::UnitRace race, GameDefine::Gender gender, u8& variant);
        void GetModelHashesForShoulders(Singletons::Database::ItemSingleton& itemSingleton, u32 shoulderModelResourcesID, u32& modelHashLeftShoulder, u32& modelHashRightShoulder);
    }
}