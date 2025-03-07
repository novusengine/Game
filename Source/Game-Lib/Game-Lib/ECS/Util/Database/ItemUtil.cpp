#include "ItemUtil.h"

#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/ECS/Singletons/Database/ItemSingleton.h"
#include "Game-Lib/ECS/Singletons/Database/TextureSingleton.h"
#include "Game-Lib/Gameplay/Database/Item.h"
#include "Game-Lib/Gameplay/Database/Spell.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Model/ModelLoader.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <entt/entt.hpp>

namespace ECSUtil::Item
{
    bool Refresh()
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->dbRegistry;
        auto& ctx = registry->ctx();

        if (!ctx.contains<ECS::Singletons::ItemSingleton>())
            ctx.emplace<ECS::Singletons::ItemSingleton>();

        auto& itemSingleton = ctx.get<ECS::Singletons::ItemSingleton>();
        
        itemSingleton.itemIDs.clear();
        itemSingleton.itemIDToStatTemplateID.clear();
        itemSingleton.itemIDToArmorTemplateID.clear();
        itemSingleton.itemIDToWeaponTemplateID.clear();
        itemSingleton.itemIDToShieldTemplateID.clear();
        itemSingleton.itemIDToEffectMapping.clear();
        itemSingleton.itemEffectIDs.clear();

        auto& clientDBSingleton = ctx.get<ECS::Singletons::ClientDBSingleton>();

        if (!clientDBSingleton.Has(ClientDBHash::Item))
            {
                clientDBSingleton.Register(ClientDBHash::Item, "Item");

                auto* storage = clientDBSingleton.Get(ClientDBHash::Item);
                storage->Initialize({
                    { "DisplayID",          ClientDB::FieldType::I32 },
                    { "Bind",               ClientDB::FieldType::I8 },
                    { "Rarity",             ClientDB::FieldType::I8 },
                    { "Category",           ClientDB::FieldType::I8 },
                    { "Type",               ClientDB::FieldType::I8 },
                    { "VirtualLevel",       ClientDB::FieldType::I16 },
                    { "RequiredLevel",      ClientDB::FieldType::I16 },
                    { "Durability",         ClientDB::FieldType::I32 },
                    { "IconID",             ClientDB::FieldType::I32 },
                    { "Name",               ClientDB::FieldType::StringRef },
                    { "Description",        ClientDB::FieldType::StringRef },
                    { "Armor",              ClientDB::FieldType::StringRef },
                    { "StatTemplateID",     ClientDB::FieldType::I32 },
                    { "ArmorTemplateID",    ClientDB::FieldType::I32 },
                    { "WeaponTemplateID",   ClientDB::FieldType::I32 },
                    { "ShieldTemplateID",   ClientDB::FieldType::I32 },
                });

                ::Database::Item::Item defaultItem =
                {
                    .displayID = 0,
                    .bind = 0,
                    .rarity = 1,
                    .category = 1,
                    .type = 1,
                    .virtualLevel = 0,
                    .requiredLevel = 0,
                    .durability = 0,
                    .iconID = 0,
                    .name = storage->AddString("Default"),
                    .description = storage->AddString(""),
                    .armor = storage->AddString(""),
                    .statTemplateID = 0,
                    .armorTemplateID = 0,
                    .weaponTemplateID = 0,
                    .shieldTemplateID = 0,
                };

                storage->Replace(0, defaultItem);
                storage->MarkDirty();
            }

        if (!clientDBSingleton.Has(ClientDBHash::ItemStatTemplate))
            {
                clientDBSingleton.Register(ClientDBHash::ItemStatTemplate, "ItemStatTemplate");

                auto* storage = clientDBSingleton.Get(ClientDBHash::ItemStatTemplate);
                storage->Initialize({
                    { "StatTypeID1",    ClientDB::FieldType::I8 },
                    { "StatTypeID2",    ClientDB::FieldType::I8 },
                    { "StatTypeID3",    ClientDB::FieldType::I8 },
                    { "StatTypeID4",    ClientDB::FieldType::I8 },
                    { "StatTypeID5",    ClientDB::FieldType::I8 },
                    { "StatTypeID6",    ClientDB::FieldType::I8 },
                    { "StatTypeID7",    ClientDB::FieldType::I8 },
                    { "StatTypeID8",    ClientDB::FieldType::I8 },
                    { "StatValue1",     ClientDB::FieldType::I32 },
                    { "StatValue2",     ClientDB::FieldType::I32 },
                    { "StatValue3",     ClientDB::FieldType::I32 },
                    { "StatValue4",     ClientDB::FieldType::I32 },
                    { "StatValue5",     ClientDB::FieldType::I32 },
                    { "StatValue6",     ClientDB::FieldType::I32 },
                    { "StatValue7",     ClientDB::FieldType::I32 },
                    { "StatValue8",     ClientDB::FieldType::I32 },
                });

                storage->MarkDirty();
            }

        if (!clientDBSingleton.Has(ClientDBHash::ItemArmorTemplate))
            {
                clientDBSingleton.Register(ClientDBHash::ItemArmorTemplate, "ItemArmorTemplate");

                auto* storage = clientDBSingleton.Get(ClientDBHash::ItemArmorTemplate);
                storage->Initialize({
                    { "EquipType",      ClientDB::FieldType::I32 },
                    { "BonusArmor",     ClientDB::FieldType::I32 }
                });

                storage->MarkDirty();
            }

        if (!clientDBSingleton.Has(ClientDBHash::ItemWeaponTemplate))
            {
                clientDBSingleton.Register(ClientDBHash::ItemWeaponTemplate, "ItemWeaponTemplate");

                auto* storage = clientDBSingleton.Get(ClientDBHash::ItemWeaponTemplate);
                storage->Initialize({
                    { "WeaponStyle",    ClientDB::FieldType::I32 },
                    { "MinDamage",      ClientDB::FieldType::I32 },
                    { "MaxDamage",      ClientDB::FieldType::I32 },
                    { "Speed",          ClientDB::FieldType::F32 }
                });

                ::Database::Item::ItemWeaponTemplate defaultWeaponTemplate =
                {
                    .weaponStyle = ::Database::Item::ItemWeaponStyle::Unspecified,
                    .minDamage = 0,
                    .maxDamage = 0,
                    .speed = 1.0f
                };

                storage->Replace(0, defaultWeaponTemplate);
                storage->MarkDirty();
            }

        if (!clientDBSingleton.Has(ClientDBHash::ItemShieldTemplate))
            {
                clientDBSingleton.Register(ClientDBHash::ItemShieldTemplate, "ItemShieldTemplate");

                auto* storage = clientDBSingleton.Get(ClientDBHash::ItemShieldTemplate);
                storage->Initialize({
                    { "BonusArmor", ClientDB::FieldType::I32 },
                    { "Block",      ClientDB::FieldType::I32 }
                });

                storage->MarkDirty();
            }

        if (!clientDBSingleton.Has(ClientDBHash::ItemStatTypes))
            {
                clientDBSingleton.Register(ClientDBHash::ItemStatTypes, "ItemStatTypes");

                auto* storage = clientDBSingleton.Get(ClientDBHash::ItemStatTypes);
                storage->Initialize({
                    { "Name",     ClientDB::FieldType::StringRef },
                });

                ClientDB::StringRef defaultStatTypeName = storage->AddString("Unknown");
                storage->Replace(0, defaultStatTypeName);

                storage->MarkDirty();
            }

        if (!clientDBSingleton.Has(ClientDBHash::ItemEffects))
            {
                clientDBSingleton.Register(ClientDBHash::ItemEffects, "ItemEffects");

                auto* storage = clientDBSingleton.Get(ClientDBHash::ItemEffects);
                storage->Initialize({
                    { "ItemID",     ClientDB::FieldType::I32 },
                    { "Slot",       ClientDB::FieldType::I8 },
                    { "Type",       ClientDB::FieldType::I8 },
                    { "SpellID",    ClientDB::FieldType::I32 }
                });

                storage->MarkDirty();
            }

        // TODO : Move this to SpellUtil (When we make it)
        if (!clientDBSingleton.Has(ClientDBHash::Spell))
            {
                clientDBSingleton.Register(ClientDBHash::Spell, "Spell");

                auto* storage = clientDBSingleton.Get(ClientDBHash::Spell);
                storage->Initialize({
                    { "Name",               ClientDB::FieldType::StringRef },
                    { "Description",        ClientDB::FieldType::StringRef },
                    { "AuraDescription",    ClientDB::FieldType::StringRef },
                });

                ::Database::Spell::Spell defaultSpell =
                {
                    .name = storage->AddString("Unused"),
                    .description = storage->AddString("Unused"),
                    .auraDescription = storage->AddString("Unused")
                };

                storage->Replace(0, defaultSpell);
                storage->MarkDirty();
            }

        auto* itemStorage = clientDBSingleton.Get(ClientDBHash::Item);
        auto* itemStatTemplateStorage = clientDBSingleton.Get(ClientDBHash::ItemStatTemplate);
        auto* itemArmorTemplateStorage = clientDBSingleton.Get(ClientDBHash::ItemArmorTemplate);
        auto* itemWeaponTemplateStorage = clientDBSingleton.Get(ClientDBHash::ItemWeaponTemplate);
        auto* itemShieldTemplateStorage = clientDBSingleton.Get(ClientDBHash::ItemShieldTemplate);

        auto* spellStorage = clientDBSingleton.Get(ClientDBHash::Spell);
        auto* itemEffectsStorage = clientDBSingleton.Get(ClientDBHash::ItemEffects);

        u32 numItems = itemStorage->GetNumRows();
        u32 numItemEffects = itemEffectsStorage->GetNumRows();

        itemSingleton.itemIDs.reserve(numItems);
        itemSingleton.itemIDToStatTemplateID.reserve(numItems);
        itemSingleton.itemIDToArmorTemplateID.reserve(numItems);
        itemSingleton.itemIDToWeaponTemplateID.reserve(numItems);
        itemSingleton.itemIDToShieldTemplateID.reserve(numItems);
        itemSingleton.itemIDToEffectMapping.reserve(numItems);
        itemSingleton.itemEffectIDs.reserve(numItemEffects);

        itemStorage->Each([&](u32 id, ::Database::Item::Item& item) -> bool
        {
            itemSingleton.itemIDs.insert(id);

            if (item.statTemplateID > 0 && itemStatTemplateStorage->Has(item.statTemplateID))
                itemSingleton.itemIDToStatTemplateID[id] = item.statTemplateID;

            if (item.armorTemplateID > 0 && itemArmorTemplateStorage->Has(item.armorTemplateID))
                itemSingleton.itemIDToArmorTemplateID[id] = item.armorTemplateID;

            if (item.weaponTemplateID > 0 && itemWeaponTemplateStorage->Has(item.weaponTemplateID))
                itemSingleton.itemIDToWeaponTemplateID[id] = item.weaponTemplateID;

            if (item.shieldTemplateID > 0 && itemShieldTemplateStorage->Has(item.shieldTemplateID))
                itemSingleton.itemIDToShieldTemplateID[id] = item.shieldTemplateID;

            return true;
        });

        std::map<u32, std::vector<u32>> itemIDToEffectIDs;
        itemEffectsStorage->Each([&](u32 id, ::Database::Item::ItemEffect& itemEffect) -> bool
        {
            if (itemEffect.spellID == 0)
                return true;

            itemIDToEffectIDs[itemEffect.itemID].push_back(id);
            return true;
        });

        robin_hood::unordered_set<u8> itemEffectSlotSeen;
        for (auto& pair : itemIDToEffectIDs)
        {
            u32 itemID = pair.first;

            // Sort ItemEffectID based on Slot (Ascending)
            {
                std::ranges::sort(pair.second, [&itemEffectsStorage](const u32 itemEffectIDA, const u32 itemEffectIDB)
                {
                    const auto& itemEffectA = itemEffectsStorage->Get<::Database::Item::ItemEffect>(itemEffectIDA);
                    const auto& itemEffectB = itemEffectsStorage->Get<::Database::Item::ItemEffect>(itemEffectIDB);

                    return itemEffectA.slot < itemEffectB.slot;
                });
            }

            // Check for Invalid ItemEffects
            {
                itemEffectSlotSeen.clear();

                std::erase_if(pair.second, [&itemEffectsStorage, &spellStorage, &itemEffectSlotSeen](const u32 itemEffectID)
                {
                    const auto& itemEffect = itemEffectsStorage->Get<::Database::Item::ItemEffect>(itemEffectID);
                    bool spellDoesNotExist = !spellStorage->Has(itemEffect.spellID);
                    if (spellDoesNotExist)
                        return true;

                    bool effectSlotInUse = itemEffectSlotSeen.contains(itemEffect.slot);
                    if (effectSlotInUse)
                        return true;

                    itemEffectSlotSeen.insert(itemEffect.slot);
                    return false;
                });
            }

            u32 numEffectsToAdd = static_cast<u32>(pair.second.size());
            if (numEffectsToAdd == 0)
                continue;

            u32 effectIndex = static_cast<u32>(itemSingleton.itemEffectIDs.size());
            itemSingleton.itemIDToEffectMapping[itemID] = { .indexIntoMap = effectIndex, .count = numEffectsToAdd };

            itemSingleton.itemEffectIDs.resize(effectIndex + numEffectsToAdd);
            for (u32 i = 0; i < numEffectsToAdd; i++)
            {
                itemSingleton.itemEffectIDs[effectIndex + i] = pair.second[i];
            }
        }

        auto* modelFileDataStorage = clientDBSingleton.Get(ClientDBHash::ModelFileData);
        itemSingleton.helmModelResourcesIDToModelMapping.reserve(256);
        modelFileDataStorage->Each([&](u32 id, const ClientDB::Definitions::ModelFileData& modelFileData) -> bool
            {
                static constexpr const char* HelmPathPrefix = "item/objectcomponents/head/";
                static constexpr const char* ShoulderPathPrefix = "item/objectcomponents/shoulder/";
                static constexpr u32 ShoulderPathSubStrIndex = sizeof("item/objectcomponents/shoulder/") - 1;

                const std::string& modelPath = modelFileDataStorage->GetString(modelFileData.modelPath);

                if (StringUtils::BeginsWith(modelPath, HelmPathPrefix))
                {
                    static constexpr const char* HelmRacePrefix[(u32)GameDefine::UnitRace::Count] =
                    {
                        "hu",
                        "or",
                        "dw",
                        "ni",
                        "sc",
                        "ta",
                        "gn",
                        "tr"
                    };

                    u32 modelExtensionLength = static_cast<u32>(Model::FILE_EXTENSION.size());
                    u32 modelPathLength = static_cast<u32>(modelPath.size());
                    u32 modelHelmIdentifierSize = 3;
                    u32 modelHelmIdentifierRaceSize = 2;
                    u32 modelPathCmpIndex = modelPathLength - modelExtensionLength - modelHelmIdentifierSize;

                    for (u32 i = 0; i < (u32)GameDefine::UnitRace::Count; i++)
                    {
                        const char* racePrefix = HelmRacePrefix[i];
                        if (strncmp(modelPath.c_str() + modelPathCmpIndex, racePrefix, modelHelmIdentifierRaceSize) != 0)
                            continue;

                        char genderIdentifier = *(modelPath.c_str() + modelPathCmpIndex + modelHelmIdentifierRaceSize);
                        bool isFemale = genderIdentifier == 'f';
                        u8 raceGenderMapping = (i * 2) + isFemale;

                        auto& helmMapping = itemSingleton.helmModelResourcesIDToModelMapping[modelFileData.modelResourcesID];
                        u32 modelHash = StringUtils::fnv1a_32(modelPath.c_str(), modelPathLength);

                        helmMapping.raceGenderToModelHash[raceGenderMapping] = modelHash;
                        break;
                    }
                }
                else if (StringUtils::BeginsWith(modelPath, "item/objectcomponents/shoulder/"))
                {
                    char sideIdentifier = *(modelPath.c_str() + ShoulderPathSubStrIndex);

                    auto& shoulderMapping = itemSingleton.shoulderModelResourcesIDToModelMapping[modelFileData.modelResourcesID];

                    bool isRightSide = sideIdentifier == 'r';
                    u32 modelHash = StringUtils::fnv1a_32(modelPath.c_str(), modelPath.length());

                    shoulderMapping.sideToModelHash[isRightSide] = modelHash;
                }

                return true;
            });


        auto& textureSingleton = ctx.get<ECS::Singletons::TextureSingleton>();
        itemSingleton.itemDisplayInfoMaterialResourcesKeyToID.clear();

        {

            auto* itemDisplayInfoStorage = clientDBSingleton.Get(ClientDBHash::ItemDisplayInfo);
            auto* itemDisplayMaterialResourcesStorage = clientDBSingleton.Get(ClientDBHash::ItemDisplayMaterialResources);
            u32 numDisplayIDs = itemDisplayInfoStorage->GetNumRows();
            u32 numRecords = itemDisplayMaterialResourcesStorage->GetNumRows();

            itemSingleton.itemDisplayInfoToComponentSectionData.clear();
            itemSingleton.itemDisplayInfoToComponentSectionData.reserve(numDisplayIDs);

            itemSingleton.itemDisplayInfoMaterialResourcesKeyToID.clear();
            itemSingleton.itemDisplayInfoMaterialResourcesKeyToID.reserve(itemSingleton.itemDisplayInfoMaterialResourcesKeyToID.size() + numRecords);

            itemDisplayMaterialResourcesStorage->Each([&itemSingleton, &textureSingleton, &itemDisplayMaterialResourcesStorage](u32 id, const ClientDB::Definitions::ItemDisplayMaterialResources& row)
            {
                if (id == 0) return true;

                u64 key = CreateItemDisplayMaterialResourcesKey(row.displayID, row.componentSection, row.materialResourcesID);
                if (itemSingleton.itemDisplayInfoMaterialResourcesKeyToID.contains(key))
                {
                    const auto& existingRow = itemDisplayMaterialResourcesStorage->Get<ClientDB::Definitions::ItemDisplayMaterialResources>(itemSingleton.itemDisplayInfoMaterialResourcesKeyToID[key]);

                    // Skip Duplicate Data
                    if (existingRow.materialResourcesID == row.materialResourcesID && existingRow.displayID == row.displayID && existingRow.componentSection == row.componentSection)
                        return true;

                    NC_LOG_ERROR("Encountered Key Collision for ItemDisplayMaterialResources: {0}", key);
                    NC_LOG_ERROR("Entry 1: (MaterialResourcesID : {1}, DisplayID : {2}, ComponentSection : {3})", key, existingRow.materialResourcesID, existingRow.displayID, existingRow.componentSection);
                    NC_LOG_ERROR("Entry 2: (MaterialResourcesID : {1}, DisplayID : {2}, ComponentSection : {3})", key, row.materialResourcesID, row.displayID, row.componentSection);
                }

                itemSingleton.itemDisplayInfoMaterialResourcesKeyToID[key] = id;

                auto& componentSectionData = itemSingleton.itemDisplayInfoToComponentSectionData[row.displayID];
                if (textureSingleton.materialResourcesIDToTextureHashes.contains(row.materialResourcesID))
                {
                    u32 textureHash = textureSingleton.materialResourcesIDToTextureHashes[row.materialResourcesID][0];
                    if (textureSingleton.textureHashToPath.contains(textureHash))
                    {
                        componentSectionData.componentSectionToTextureHash[row.componentSection] = textureHash;
                    }
                }

                return true;
            });
        }

        {
            auto* itemDisplayModelMaterialResourcesStorage = clientDBSingleton.Get(ClientDBHash::ItemDisplayModelMaterialResources);
            u32 numRecords = itemDisplayModelMaterialResourcesStorage->GetNumRows();
            itemSingleton.itemDisplayInfoMaterialResourcesKeyToID.reserve(itemSingleton.itemDisplayInfoMaterialResourcesKeyToID.size() + numRecords);

            itemDisplayModelMaterialResourcesStorage->Each([&itemSingleton, &textureSingleton, &itemDisplayModelMaterialResourcesStorage](u32 id, const ClientDB::Definitions::ItemDisplayModelMaterialResources& row)
            {
                if (id == 0) return true;

                u64 key = CreateItemDisplayModelMaterialResourcesKey(row.displayID, row.modelIndex, row.textureType, row.materialResourcesID);
                if (itemSingleton.itemDisplayInfoMaterialResourcesKeyToID.contains(key))
                {
                    const auto& existingRow = itemDisplayModelMaterialResourcesStorage->Get<ClientDB::Definitions::ItemDisplayModelMaterialResources>(itemSingleton.itemDisplayInfoMaterialResourcesKeyToID[key]);

                    // Skip Duplicate Data
                    if (existingRow.materialResourcesID == row.materialResourcesID && existingRow.displayID == row.displayID && existingRow.textureType == row.textureType && existingRow.modelIndex == row.modelIndex)
                        return true;

                    NC_LOG_ERROR("Encountered Key Collision for ItemDisplayModelMaterialResources: {0}", key);
                    NC_LOG_ERROR("Entry 1: (MaterialResourcesID : {1}, DisplayID : {2}, TextureType : {3}, ModelIndex : {4})", existingRow.materialResourcesID, existingRow.displayID, existingRow.textureType, existingRow.modelIndex);
                    NC_LOG_ERROR("Entry 2: (MaterialResourcesID : {1}, DisplayID : {2}, TextureType : {3}, ModelIndex : {4})", row.materialResourcesID, row.displayID, row.textureType, row.modelIndex);
                }

                itemSingleton.itemDisplayInfoMaterialResourcesKeyToID[key] = id;
                return true;
            });
        }

        return true;
    }

    bool ItemHasStatTemplate(const ECS::Singletons::ItemSingleton& itemSingleton, u32 itemID)
    {
        return itemSingleton.itemIDToStatTemplateID.contains(itemID);
    }
    u32 GetItemStatTemplateID(ECS::Singletons::ItemSingleton& itemSingleton, u32 itemID)
    {
        if (!ItemHasStatTemplate(itemSingleton, itemID))
            return 0;

        return itemSingleton.itemIDToStatTemplateID[itemID];
    }

    bool ItemHasArmorTemplate(const ECS::Singletons::ItemSingleton& itemSingleton, u32 itemID)
    {
        return itemSingleton.itemIDToArmorTemplateID.contains(itemID);
    }
    u32 GetItemArmorTemplateID(ECS::Singletons::ItemSingleton& itemSingleton, u32 itemID)
    {
        if (!ItemHasArmorTemplate(itemSingleton, itemID))
            return 0;

        return itemSingleton.itemIDToArmorTemplateID[itemID];
    }

    bool ItemHasWeaponTemplate(const ECS::Singletons::ItemSingleton& itemSingleton, u32 itemID)
    {
        return itemSingleton.itemIDToWeaponTemplateID.contains(itemID);
    }
    u32 GetItemWeaponTemplateID(ECS::Singletons::ItemSingleton& itemSingleton, u32 itemID)
    {
        if (!ItemHasWeaponTemplate(itemSingleton, itemID))
            return 0;

        return itemSingleton.itemIDToWeaponTemplateID[itemID];
    }

    bool ItemHasShieldTemplate(const ECS::Singletons::ItemSingleton& itemSingleton, u32 itemID)
    {
        return itemSingleton.itemIDToShieldTemplateID.contains(itemID);
    }
    u32 GetItemShieldTemplateID(ECS::Singletons::ItemSingleton& itemSingleton, u32 itemID)
    {
        if (!ItemHasShieldTemplate(itemSingleton, itemID))
            return 0;

        return itemSingleton.itemIDToShieldTemplateID[itemID];
    }

    bool ItemHasAnyEffects(const ECS::Singletons::ItemSingleton& itemSingleton, u32 itemID)
    {
        return itemSingleton.itemIDToEffectMapping.contains(itemID);
    }
    const u32* GetItemEffectIDs(ECS::Singletons::ItemSingleton& itemSingleton, u32 itemID, u32& count)
    {
        count = 0;

        if (!ItemHasAnyEffects(itemSingleton, itemID))
            return nullptr;

        const auto& itemEffectMapping = itemSingleton.itemIDToEffectMapping[itemID];
        if (itemEffectMapping.count == 0)
            return nullptr;

        count = itemEffectMapping.count;
        return &itemSingleton.itemEffectIDs[itemEffectMapping.indexIntoMap];
    }
    
    u32 GetModelHashForHelm(ECS::Singletons::ItemSingleton& itemSingleton, u32 helmModelResourcesID, GameDefine::UnitRace race, GameDefine::Gender gender, u8& variant)
    {
        variant = 0;

        if (race == GameDefine::UnitRace::None || gender == GameDefine::Gender::None || !itemSingleton.helmModelResourcesIDToModelMapping.contains(helmModelResourcesID))
            return std::numeric_limits<u32>().max();

        auto& helmMapping = itemSingleton.helmModelResourcesIDToModelMapping[helmModelResourcesID];

        u32 raceIndex = ((u32)race - 1) * 2;
        bool isFemale = gender == GameDefine::Gender::Female;

        u32 identifier = raceIndex + isFemale;
        u32 modelHash = helmMapping.raceGenderToModelHash[identifier];

        variant = identifier;
        if (modelHash == 0)
            return std::numeric_limits<u32>().max();

        return modelHash;
    }
    void GetModelHashesForShoulders(ECS::Singletons::ItemSingleton& itemSingleton, u32 shoulderModelResourcesID, u32& modelHashLeftShoulder, u32& modelHashRightShoulder)
    {
        modelHashLeftShoulder = std::numeric_limits<u32>().max();
        modelHashRightShoulder = std::numeric_limits<u32>().max();

        if (!itemSingleton.shoulderModelResourcesIDToModelMapping.contains(shoulderModelResourcesID))
            return;

        auto& shoulderMapping = itemSingleton.shoulderModelResourcesIDToModelMapping[shoulderModelResourcesID];
        
        modelHashLeftShoulder = shoulderMapping.sideToModelHash[0];
        modelHashRightShoulder = shoulderMapping.sideToModelHash[1];
    }

    u64 CreateItemDisplayMaterialResourcesKey(u32 displayID, u8 componentSection, u32 materialResourcesID)
    {
        u64 key = (materialResourcesID & 0xFFFFFF) | (static_cast<u64>(displayID & 0xFFFFFF) << 24) | (static_cast<u64>(componentSection & 0xF) << 58);
        return key;
    }

    u64 CreateItemDisplayModelMaterialResourcesKey(u32 displayID, u8 modelIndex, u8 textureType, u32 materialResourcesID)
    {
        u64 key = (materialResourcesID & 0xFFFFFF) | (static_cast<u64>(displayID & 0xFFFFFF) << 24) | (static_cast<u64>(textureType & 0xF) << 48) | (static_cast<u64>(modelIndex & 0xFF) << 52);
        return key;
    }
}
