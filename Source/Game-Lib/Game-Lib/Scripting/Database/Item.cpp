#include "Item.h"

#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/ECS/Singletons/Database/ItemSingleton.h"
#include "Game-Lib/ECS/Util/Database/ItemUtil.h"
#include "Game-Lib/Gameplay/Database/Item.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <MetaGen/Shared/ClientDB/ClientDB.h>

#include <Scripting/Zenith.h>

#include <entt/entt.hpp>

namespace Scripting::Database
{
    void Item::Register(Zenith* zenith)
    {
        LuaMethodTable::Set(zenith, itemGlobalFunctions, "Item");

        zenith->CreateTable("ItemEffectType");
        zenith->AddTableField("OnEquip", 1);
        zenith->AddTableField("OnUse", 2);
        zenith->AddTableField("OnProc", 3);
        zenith->AddTableField("OnLooted", 4);
        zenith->AddTableField("OnBound", 5);
        zenith->Pop();
    }

    namespace ItemMethods
    {
        i32 GetItemInfo(Zenith* zenith)
        {
            u32 itemID = zenith->CheckVal<u32>(1);

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->dbRegistry;
            auto& clientDBSingleton = registry->ctx().get<ECS::Singletons::ClientDBSingleton>();
            if (!clientDBSingleton.Has(ClientDBHash::Item))
                return 0;

            auto* db = clientDBSingleton.Get(ClientDBHash::Item);
            if (!db->Has(itemID))
                itemID = 0;

            const auto& itemInfo = db->Get<MetaGen::Shared::ClientDB::ItemRecord>(itemID);

            // Name, Icon, Description RequiredText, SpecialText
            const std::string& name = db->GetString(itemInfo.name);
            const std::string& description = db->GetString(itemInfo.description);

            zenith->CreateTable();
            zenith->AddTableField("DisplayID", itemInfo.displayID);
            zenith->AddTableField("Bind", itemInfo.bind);
            zenith->AddTableField("Rarity", itemInfo.rarity);
            zenith->AddTableField("Category", itemInfo.category);
            zenith->AddTableField("Type", itemInfo.categoryType);
            zenith->AddTableField("VirtualLevel", itemInfo.virtualLevel);
            zenith->AddTableField("RequiredLevel", itemInfo.requiredLevel);
            zenith->AddTableField("Durability", itemInfo.durability);
            zenith->AddTableField("IconID", itemInfo.iconID);
            zenith->AddTableField("Name", name.c_str());
            zenith->AddTableField("Description", description.c_str());
            zenith->AddTableField("Armor", itemInfo.armor);
            zenith->AddTableField("StatTemplateID", itemInfo.statTemplateID);
            zenith->AddTableField("ArmorTemplateID", itemInfo.armorTemplateID);
            zenith->AddTableField("WeaponTemplateID", itemInfo.weaponTemplateID);
            zenith->AddTableField("ShieldTemplateID", itemInfo.shieldTemplateID);

            return 1;
        }

        i32 GetItemStatInfo(Zenith* zenith)
        {
            u32 templateID = zenith->CheckVal<u32>(1);

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->dbRegistry;
            auto& clientDBSingleton = registry->ctx().get<ECS::Singletons::ClientDBSingleton>();
            if (!clientDBSingleton.Has(ClientDBHash::ItemStatTemplate))
                return 0;

            auto* db = clientDBSingleton.Get(ClientDBHash::ItemStatTemplate);
            if (!db->Has(templateID))
                templateID = 0;

            const auto& templateInfo = db->Get<MetaGen::Shared::ClientDB::ItemStatTemplateRecord>(templateID);

            zenith->CreateTable();
            u32 numStatsAdded = 0;
            for (u32 statIndex = 0; statIndex < 8; ++statIndex)
            {
                if (templateInfo.statTypeID[statIndex] == 0 || templateInfo.value[statIndex] == 0)
                    continue;

                zenith->CreateTable();
                zenith->AddTableField("ID", templateInfo.statTypeID[statIndex]);
                zenith->AddTableField("Value", templateInfo.value[statIndex]);

                zenith->SetTableKey(++numStatsAdded);
            }

            return 1;
        }

        i32 GetItemArmorInfo(Zenith* zenith)
        {
            u32 templateID = zenith->CheckVal<u32>(1);

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->dbRegistry;
            auto& clientDBSingleton = registry->ctx().get<ECS::Singletons::ClientDBSingleton>();
            if (!clientDBSingleton.Has(ClientDBHash::ItemArmorTemplate))
                return 0;

            auto* db = clientDBSingleton.Get(ClientDBHash::ItemArmorTemplate);
            if (!db->Has(templateID))
                templateID = 0;

            const auto& templateInfo = db->Get<MetaGen::Shared::ClientDB::ItemArmorTemplateRecord>(templateID);

            zenith->CreateTable();
            zenith->AddTableField("EquipType", (u32)templateInfo.equipType);
            zenith->AddTableField("BonusArmor", templateInfo.bonusArmor);

            return 1;
        }

        i32 GetItemWeaponInfo(Zenith* zenith)
        {
            u32 templateID = zenith->CheckVal<u32>(1);

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->dbRegistry;
            auto& clientDBSingleton = registry->ctx().get<ECS::Singletons::ClientDBSingleton>();
            if (!clientDBSingleton.Has(ClientDBHash::ItemWeaponTemplate))
                return 0;

            auto* db = clientDBSingleton.Get(ClientDBHash::ItemWeaponTemplate);
            if (!db->Has(templateID))
                templateID = 0;

            const auto& templateInfo = db->Get<MetaGen::Shared::ClientDB::ItemWeaponTemplateRecord>(templateID);

            zenith->CreateTable();
            zenith->AddTableField("WeaponStyle", (u32)templateInfo.weaponStyle);
            zenith->AddTableField("MinDamage", templateInfo.damageRange.x);
            zenith->AddTableField("MaxDamage", templateInfo.damageRange.y);
            zenith->AddTableField("Speed", templateInfo.speed);

            return 1;
        }

        i32 GetItemShieldInfo(Zenith* zenith)
        {
            u32 templateID = zenith->CheckVal<u32>(1);

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->dbRegistry;
            auto& clientDBSingleton = registry->ctx().get<ECS::Singletons::ClientDBSingleton>();
            if (!clientDBSingleton.Has(ClientDBHash::ItemShieldTemplate))
                return 0;

            auto* db = clientDBSingleton.Get(ClientDBHash::ItemShieldTemplate);
            if (!db->Has(templateID))
                templateID = 0;

            const auto& templateInfo = db->Get<MetaGen::Shared::ClientDB::ItemShieldTemplateRecord>(templateID);

            zenith->CreateTable();
            zenith->AddTableField("BonusArmor", templateInfo.bonusArmor);
            zenith->AddTableField("Block", templateInfo.block);

            return 1;
        }

        i32 GetItemDisplayInfo(Zenith* zenith)
        {
            u32 itemDisplayInfoID = zenith->CheckVal<u32>(1);

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->dbRegistry;
            auto& clientDBSingleton = registry->ctx().get<ECS::Singletons::ClientDBSingleton>();
            if (!clientDBSingleton.Has(ClientDBHash::ItemDisplayInfo))
                return 0;

            auto* db = clientDBSingleton.Get(ClientDBHash::ItemDisplayInfo);
            if (!db->Has(itemDisplayInfoID))
                itemDisplayInfoID = 0;

            const auto& itemDisplayInfo = db->Get<MetaGen::Shared::ClientDB::ItemDisplayInfoRecord>(itemDisplayInfoID);

            zenith->CreateTable();
            zenith->AddTableField("ItemRangedDisplayInfoID", itemDisplayInfo.itemRangedDisplayInfoID);
            zenith->AddTableField("Flags", itemDisplayInfo.flags);
            zenith->AddTableField("ModelResourcesID1", itemDisplayInfo.modelResourcesID[0]);
            zenith->AddTableField("ModelResourcesID2", itemDisplayInfo.modelResourcesID[1]);
            zenith->AddTableField("MaterialResourcesID1", itemDisplayInfo.modelMaterialResourcesID[0]);
            zenith->AddTableField("MaterialResourcesID2", itemDisplayInfo.modelMaterialResourcesID[1]);
            zenith->AddTableField("GeosetGroup1", itemDisplayInfo.modelGeosetGroups[0]);
            zenith->AddTableField("GeosetGroup2", itemDisplayInfo.modelGeosetGroups[1]);
            zenith->AddTableField("GeosetGroup3", itemDisplayInfo.modelGeosetGroups[2]);
            zenith->AddTableField("GeosetGroup4", itemDisplayInfo.modelGeosetGroups[3]);
            zenith->AddTableField("GeosetVisID1", itemDisplayInfo.modelGeosetVisIDs[0]);
            zenith->AddTableField("GeosetVisID2", itemDisplayInfo.modelGeosetVisIDs[1]);

            return 1;
        }

        i32 GetItemEffects(Zenith* zenith)
        {
            u32 itemID = zenith->CheckVal<u32>(1);

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->dbRegistry;
            auto& clientDBSingleton = registry->ctx().get<ECS::Singletons::ClientDBSingleton>();
            if (!clientDBSingleton.Has(ClientDBHash::Item) || !clientDBSingleton.Has(ClientDBHash::ItemEffects))
                return 0;

            auto* db = clientDBSingleton.Get(ClientDBHash::Item);
            if (!db->Has(itemID))
                return 0;

            auto& itemSingleton = registry->ctx().get<ECS::Singletons::ItemSingleton>();

            u32 effectCount = 0;
            const u32* effectIDs = ECSUtil::Item::GetItemEffectIDs(itemSingleton, itemID, effectCount);

            auto* itemEffectsStorage = clientDBSingleton.Get(ClientDBHash::ItemEffects);

            zenith->CreateTable();
            for (u32 i = 1; i <= effectCount; i++)
            {
                u32 effectID = effectIDs[i - 1];
                const auto& itemEffect = itemEffectsStorage->Get<MetaGen::Shared::ClientDB::ItemEffectRecord>(effectID);

                zenith->CreateTable();
                zenith->AddTableField("ItemID", itemEffect.itemID);
                zenith->AddTableField("Slot", itemEffect.effectSlot);
                zenith->AddTableField("Type", static_cast<u32>(itemEffect.effectType));
                zenith->AddTableField("SpellID", itemEffect.effectSpellID);

                zenith->SetTableKey(i);
            }

            return 1;
        }

        i32 GetIconInfo(Zenith* zenith)
        {
            u32 iconID = zenith->CheckVal<u32>(1);

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->dbRegistry;
            auto& clientDBSingleton = registry->ctx().get<ECS::Singletons::ClientDBSingleton>();
            if (!clientDBSingleton.Has(ClientDBHash::Icon))
                return 0;

            auto* db = clientDBSingleton.Get(ClientDBHash::Icon);
            if (!db->Has(iconID))
                iconID = 0;

            const auto& icon = db->Get<MetaGen::Shared::ClientDB::IconRecord>(iconID);
            const std::string& texture = db->GetString(icon.texture);

            zenith->CreateTable();
            zenith->AddTableField("Texture", texture.c_str());

            return 1;
        }
    }
}