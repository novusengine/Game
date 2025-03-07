#pragma once
#include "Game-Lib/ECS/Singletons/Database/ItemSingleton.h"

#include <Base/Types.h>

#include <Gameplay/GameDefine.h>

namespace ECSUtil::Item
{
    bool Refresh();

    bool ItemHasStatTemplate(const ECS::Singletons::ItemSingleton& itemSingleton, u32 itemID);
    u32 GetItemStatTemplateID(ECS::Singletons::ItemSingleton& itemSingleton, u32 itemID);

    bool ItemHasArmorTemplate(const ECS::Singletons::ItemSingleton& itemSingleton, u32 itemID);
    u32 GetItemArmorTemplateID(ECS::Singletons::ItemSingleton& itemSingleton, u32 itemID);

    bool ItemHasWeaponTemplate(const ECS::Singletons::ItemSingleton& itemSingleton, u32 itemID);
    u32 GetItemWeaponTemplateID(ECS::Singletons::ItemSingleton& itemSingleton, u32 itemID);

    bool ItemHasShieldTemplate(const ECS::Singletons::ItemSingleton& itemSingleton, u32 itemID);
    u32 GetItemShieldTemplateID(ECS::Singletons::ItemSingleton& itemSingleton, u32 itemID);

    bool ItemHasAnyEffects(const ECS::Singletons::ItemSingleton& itemSingleton, u32 itemID);
    const u32* GetItemEffectIDs(ECS::Singletons::ItemSingleton& itemSingleton, u32 itemID, u32& count);

    u32 GetModelHashForHelm(ECS::Singletons::ItemSingleton& itemSingleton, u32 helmModelResourcesID, GameDefine::UnitRace race, GameDefine::Gender gender, u8& variant);
    void GetModelHashesForShoulders(ECS::Singletons::ItemSingleton& itemSingleton, u32 shoulderModelResourcesID, u32& modelHashLeftShoulder, u32& modelHashRightShoulder);

    u64 CreateItemDisplayMaterialResourcesKey(u32 displayID, u8 componentSection, u32 materialResourcesID);
    u64 CreateItemDisplayModelMaterialResourcesKey(u32 displayID, u8 modelIndex, u8 textureType, u32 materialResourcesID);
}