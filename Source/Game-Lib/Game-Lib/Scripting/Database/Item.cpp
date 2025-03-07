#include "Item.h"

#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/ECS/Singletons/Database/ItemSingleton.h"
#include "Game-Lib/ECS/Util/Database/ItemUtil.h"
#include "Game-Lib/Gameplay/Database/Item.h"
#include "Game-Lib/Scripting/LuaMethodTable.h"
#include "Game-Lib/Scripting/LuaState.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <FileFormat/Novus/ClientDB/Definitions.h>

#include <entt/entt.hpp>

namespace Scripting::Database
{
    static LuaMethod itemStaticFunctions[] =
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

    void Item::Register(lua_State* state)
    {
        LuaState ctx(state);

        LuaMethodTable::Set(state, itemStaticFunctions, "Item");

        ctx.CreateTableAndPopulate("ItemEffectType", [&]()
        {
            ctx.SetTable("OnEquip", 1);
            ctx.SetTable("OnUse", 2);
            ctx.SetTable("OnProc", 3);
            ctx.SetTable("OnLooted", 4);
            ctx.SetTable("OnBound", 5);
        });
    }

    namespace ItemMethods
    {
        i32 GetItemInfo(lua_State* state)
        {
            LuaState ctx(state);

            i32 itemID = ctx.Get(0);

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->dbRegistry;
            auto& clientDBSingleton = registry->ctx().get<ECS::Singletons::ClientDBSingleton>();
            if (!clientDBSingleton.Has(ClientDBHash::Item))
                return 0;

            auto* db = clientDBSingleton.Get(ClientDBHash::Item);
            if (!db->Has(itemID))
                itemID = 0;

            const auto& itemInfo = db->Get<::Database::Item::Item>(itemID);

            // Name, Icon, Description RequiredText, SpecialText
            const std::string& name = db->GetString(itemInfo.name);
            const std::string& description = db->GetString(itemInfo.description);

            ctx.CreateTableAndPopulate([&ctx, &itemInfo, &name, &description]()
            {
                ctx.SetTable("DisplayID", itemInfo.displayID);
                ctx.SetTable("Bind", itemInfo.bind);
                ctx.SetTable("Rarity", itemInfo.rarity);
                ctx.SetTable("Category", itemInfo.category);
                ctx.SetTable("Type", itemInfo.type);
                ctx.SetTable("VirtualLevel", itemInfo.virtualLevel);
                ctx.SetTable("RequiredLevel", itemInfo.requiredLevel);
                ctx.SetTable("Durability", itemInfo.durability);
                ctx.SetTable("IconID", itemInfo.iconID);
                ctx.SetTable("Name", name.c_str());
                ctx.SetTable("Description", description.c_str());

                ctx.SetTable("Armor", itemInfo.armor);
                ctx.SetTable("StatTemplateID", itemInfo.statTemplateID);
                ctx.SetTable("ArmorTemplateID", itemInfo.armorTemplateID);
                ctx.SetTable("WeaponTemplateID", itemInfo.weaponTemplateID);
                ctx.SetTable("ShieldTemplateID", itemInfo.shieldTemplateID);
            });

            return 1;
        }

        i32 GetItemStatInfo(lua_State* state)
        {
            LuaState ctx(state);

            i32 templateID = ctx.Get(0);

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->dbRegistry;
            auto& clientDBSingleton = registry->ctx().get<ECS::Singletons::ClientDBSingleton>();
            if (!clientDBSingleton.Has(ClientDBHash::ItemStatTemplate))
                return 0;

            auto* db = clientDBSingleton.Get(ClientDBHash::ItemStatTemplate);
            if (!db->Has(templateID))
                templateID = 0;

            const auto& templateInfo = db->Get<::Database::Item::ItemStatTemplate>(templateID);

            ctx.CreateTableAndPopulate([&ctx, &templateInfo]()
            {
                u32 numStatsAdded = 0;
                for (u32 i = 1; i <= 8; i++)
                {
                    u32 statID = templateInfo.statTypes[i - 1];
                    i32 statValue = templateInfo.statValues[i - 1];
                    if (statID == 0 || statValue == 0)
                        continue;

                    ctx.CreateTableAndPopulate([&ctx, statID, statValue]()
                    {
                        ctx.SetTable("ID", statID);
                        ctx.SetTable("Value", statValue);
                    });

                    ctx.SetTable(++numStatsAdded);
                }
            });

            return 1;
        }

        i32 GetItemArmorInfo(lua_State* state)
        {
            LuaState ctx(state);

            i32 templateID = ctx.Get(0);

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->dbRegistry;
            auto& clientDBSingleton = registry->ctx().get<ECS::Singletons::ClientDBSingleton>();
            if (!clientDBSingleton.Has(ClientDBHash::ItemArmorTemplate))
                return 0;

            auto* db = clientDBSingleton.Get(ClientDBHash::ItemArmorTemplate);
            if (!db->Has(templateID))
                templateID = 0;

            const auto& templateInfo = db->Get<::Database::Item::ItemArmorTemplate>(templateID);

            ctx.CreateTableAndPopulate([&ctx, &templateInfo]()
            {
                ctx.SetTable("EquipType", (u32)templateInfo.equipType);
                ctx.SetTable("BonusArmor", templateInfo.bonusArmor);
            });

            return 1;
        }

        i32 GetItemWeaponInfo(lua_State* state)
        {
            LuaState ctx(state);

            i32 templateID = ctx.Get(0);

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->dbRegistry;
            auto& clientDBSingleton = registry->ctx().get<ECS::Singletons::ClientDBSingleton>();
            if (!clientDBSingleton.Has(ClientDBHash::ItemWeaponTemplate))
                return 0;

            auto* db = clientDBSingleton.Get(ClientDBHash::ItemWeaponTemplate);
            if (!db->Has(templateID))
                templateID = 0;

            const auto& templateInfo = db->Get<::Database::Item::ItemWeaponTemplate>(templateID);

            ctx.CreateTableAndPopulate([&ctx, &templateInfo]()
            {
                ctx.SetTable("WeaponStyle", (u32)templateInfo.weaponStyle);
                ctx.SetTable("MinDamage", templateInfo.minDamage);
                ctx.SetTable("MaxDamage", templateInfo.maxDamage);
                ctx.SetTable("Speed", templateInfo.speed);
            });

            return 1;
        }

        i32 GetItemShieldInfo(lua_State* state)
        {
            LuaState ctx(state);

            i32 templateID = ctx.Get(0);

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->dbRegistry;
            auto& clientDBSingleton = registry->ctx().get<ECS::Singletons::ClientDBSingleton>();
            if (!clientDBSingleton.Has(ClientDBHash::ItemShieldTemplate))
                return 0;

            auto* db = clientDBSingleton.Get(ClientDBHash::ItemShieldTemplate);
            if (!db->Has(templateID))
                templateID = 0;

            const auto& templateInfo = db->Get<::Database::Item::ItemShieldTemplate>(templateID);

            ctx.CreateTableAndPopulate([&ctx, &templateInfo]()
            {
                ctx.SetTable("BonusArmor", templateInfo.bonusArmor);
                ctx.SetTable("Block", templateInfo.block);
            });

            return 1;
        }

        i32 GetItemDisplayInfo(lua_State* state)
        {
            LuaState ctx(state);

            i32 itemDisplayInfoID = ctx.Get(0);

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->dbRegistry;
            auto& clientDBSingleton = registry->ctx().get<ECS::Singletons::ClientDBSingleton>();
            if (!clientDBSingleton.Has(ClientDBHash::ItemDisplayInfo))
                return 0;

            auto* db = clientDBSingleton.Get(ClientDBHash::ItemDisplayInfo);
            if (!db->Has(itemDisplayInfoID))
                itemDisplayInfoID = 0;

            const auto& itemDisplayInfo = db->Get<ClientDB::Definitions::ItemDisplayInfo>(itemDisplayInfoID);

            ctx.CreateTableAndPopulate([&ctx, &itemDisplayInfo]()
            {
                ctx.SetTable("ItemVisual", itemDisplayInfo.itemVisual);
                ctx.SetTable("ParticleColorID", itemDisplayInfo.particleColorID);
                ctx.SetTable("ItemRangedDisplayInfoID", itemDisplayInfo.itemRangedDisplayInfoID);
                ctx.SetTable("OverrideSwooshSoundKitID", itemDisplayInfo.overrideSwooshSoundKitID);
                ctx.SetTable("SheatheTransformMatrixID", itemDisplayInfo.sheatheTransformMatrixID);
                ctx.SetTable("StateSpellVisualKitID", itemDisplayInfo.stateSpellVisualKitID);
                ctx.SetTable("SheathedSpellVisualKitID", itemDisplayInfo.sheathedSpellVisualKitID);
                ctx.SetTable("UnsheathedSpellVisualKitID", itemDisplayInfo.unsheathedSpellVisualKitID);
                ctx.SetTable("Flags", itemDisplayInfo.flags);
                ctx.SetTable("ModelResourcesID1", itemDisplayInfo.modelResourcesID[0]);
                ctx.SetTable("ModelResourcesID2", itemDisplayInfo.modelResourcesID[1]);
                ctx.SetTable("MaterialResourcesID1", itemDisplayInfo.materialResourcesID[0]);
                ctx.SetTable("MaterialResourcesID2", itemDisplayInfo.materialResourcesID[1]);
                ctx.SetTable("ModelType1", itemDisplayInfo.modelType[0]);
                ctx.SetTable("ModelType2", itemDisplayInfo.modelType[1]);
                ctx.SetTable("GeosetGroup1", itemDisplayInfo.geosetGroup[0]);
                ctx.SetTable("GeosetGroup2", itemDisplayInfo.geosetGroup[1]);
                ctx.SetTable("GeosetGroup3", itemDisplayInfo.geosetGroup[2]);
                ctx.SetTable("GeosetGroup4", itemDisplayInfo.geosetGroup[3]);
                ctx.SetTable("GeosetGroup5", itemDisplayInfo.geosetGroup[4]);
                ctx.SetTable("GeosetGroup6", itemDisplayInfo.geosetGroup[5]);
                ctx.SetTable("GeosetAttachmentGroup1", itemDisplayInfo.geosetAttachmentGroup[0]);
                ctx.SetTable("GeosetAttachmentGroup2", itemDisplayInfo.geosetAttachmentGroup[1]);
                ctx.SetTable("GeosetAttachmentGroup3", itemDisplayInfo.geosetAttachmentGroup[2]);
                ctx.SetTable("GeosetAttachmentGroup4", itemDisplayInfo.geosetAttachmentGroup[3]);
                ctx.SetTable("GeosetAttachmentGroup5", itemDisplayInfo.geosetAttachmentGroup[4]);
                ctx.SetTable("GeosetAttachmentGroup6", itemDisplayInfo.geosetAttachmentGroup[5]);
                ctx.SetTable("GeosetHelmetVis1", itemDisplayInfo.geosetHelmetVis[0]);
                ctx.SetTable("GeosetHelmetVis2", itemDisplayInfo.geosetHelmetVis[1]);
            });

            return 1;
        }

        i32 GetItemEffects(lua_State* state)
        {
            LuaState ctx(state);

            i32 itemID = ctx.Get(0);

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
            ctx.CreateTableAndPopulate([&ctx, &itemEffectsStorage, itemID, effectIDs, effectCount]()
            {
                for (u32 i = 1; i <= effectCount; i++)
                {
                    u32 effectID = effectIDs[i - 1];
                    const auto& itemEffect = itemEffectsStorage->Get<::Database::Item::ItemEffect>(effectID);

                    ctx.CreateTableAndPopulate([&ctx, &itemEffect]()
                    {
                        ctx.SetTable("ItemID", itemEffect.itemID);
                        ctx.SetTable("Slot", itemEffect.slot);
                        ctx.SetTable("Type", static_cast<u32>(itemEffect.type));
                        ctx.SetTable("SpellID", itemEffect.spellID);
                    });

                    ctx.SetTable(i);
                }
            });

            return 1;
        }

        i32 GetIconInfo(lua_State* state)
        {
            LuaState ctx(state);

            i32 iconID = ctx.Get(0);

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->dbRegistry;
            auto& clientDBSingleton = registry->ctx().get<ECS::Singletons::ClientDBSingleton>();
            if (!clientDBSingleton.Has(ClientDBHash::Icon))
                return 0;

            auto* db = clientDBSingleton.Get(ClientDBHash::Icon);
            if (!db->Has(iconID))
                iconID = 0;

            const auto& icon = db->Get<::Database::Shared::Icon>(iconID);
            const std::string& texture = db->GetString(icon.texture);

            ctx.CreateTableAndPopulate([&ctx, &texture]()
            {
                ctx.SetTable("Texture", texture.c_str());
            });

            return 1;
        }
    }
}