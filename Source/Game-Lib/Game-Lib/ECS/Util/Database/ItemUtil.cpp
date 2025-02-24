#include "ItemUtil.h"

#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/ECS/Singletons/Database/ItemSingleton.h"
#include "Game-Lib/Gameplay/Database/Item.h"
#include "Game-Lib/Gameplay/Database/Spell.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Model/ModelLoader.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <entt/entt.hpp>

namespace ECS
{
    namespace Util::Database::Item
    {
        bool Refresh()
        {
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->dbRegistry;
            auto& ctx = registry->ctx();

            if (!ctx.contains<Singletons::Database::ItemSingleton>())
                ctx.emplace<Singletons::Database::ItemSingleton>();

            auto& itemSingleton = ctx.get<Singletons::Database::ItemSingleton>();
            
            itemSingleton.itemIDs.clear();
            itemSingleton.itemIDToStatTemplateID.clear();
            itemSingleton.itemIDToArmorTemplateID.clear();
            itemSingleton.itemIDToWeaponTemplateID.clear();
            itemSingleton.itemIDToShieldTemplateID.clear();
            itemSingleton.itemIDToEffectMapping.clear();
            itemSingleton.itemEffectIDs.clear();

            auto& clientDBSingleton = ctx.get<Singletons::Database::ClientDBSingleton>();
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

            auto* itemDisplayInfoStorage = clientDBSingleton.Get(ClientDBHash::ItemDisplayInfo);
            u32 numDisplayIDs = itemDisplayInfoStorage->GetNumRows();

            itemSingleton.helmModelResourcesIDToModelMapping.reserve(256);

            auto* modelFileDataStorage = clientDBSingleton.Get(ClientDBHash::ModelFileData);
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

            return true;
        }

        bool ItemHasStatTemplate(const Singletons::Database::ItemSingleton& itemSingleton, u32 itemID)
        {
            return itemSingleton.itemIDToStatTemplateID.contains(itemID);
        }
        u32 GetItemStatTemplateID(Singletons::Database::ItemSingleton& itemSingleton, u32 itemID)
        {
            if (!ItemHasStatTemplate(itemSingleton, itemID))
                return 0;

            return itemSingleton.itemIDToStatTemplateID[itemID];
        }

        bool ItemHasArmorTemplate(const Singletons::Database::ItemSingleton& itemSingleton, u32 itemID)
        {
            return itemSingleton.itemIDToArmorTemplateID.contains(itemID);
        }
        u32 GetItemArmorTemplateID(Singletons::Database::ItemSingleton& itemSingleton, u32 itemID)
        {
            if (!ItemHasArmorTemplate(itemSingleton, itemID))
                return 0;

            return itemSingleton.itemIDToArmorTemplateID[itemID];
        }

        bool ItemHasWeaponTemplate(const Singletons::Database::ItemSingleton& itemSingleton, u32 itemID)
        {
            return itemSingleton.itemIDToWeaponTemplateID.contains(itemID);
        }
        u32 GetItemWeaponTemplateID(Singletons::Database::ItemSingleton& itemSingleton, u32 itemID)
        {
            if (!ItemHasWeaponTemplate(itemSingleton, itemID))
                return 0;

            return itemSingleton.itemIDToWeaponTemplateID[itemID];
        }

        bool ItemHasShieldTemplate(const Singletons::Database::ItemSingleton& itemSingleton, u32 itemID)
        {
            return itemSingleton.itemIDToShieldTemplateID.contains(itemID);
        }
        u32 GetItemShieldTemplateID(Singletons::Database::ItemSingleton& itemSingleton, u32 itemID)
        {
            if (!ItemHasShieldTemplate(itemSingleton, itemID))
                return 0;

            return itemSingleton.itemIDToShieldTemplateID[itemID];
        }

        bool ItemHasAnyEffects(const Singletons::Database::ItemSingleton& itemSingleton, u32 itemID)
        {
            return itemSingleton.itemIDToEffectMapping.contains(itemID);
        }
        const u32* GetItemEffectIDs(Singletons::Database::ItemSingleton& itemSingleton, u32 itemID, u32& count)
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
        
        u32 GetModelHashForHelm(Singletons::Database::ItemSingleton& itemSingleton, u32 helmModelResourcesID, GameDefine::UnitRace race, GameDefine::Gender gender, u8& variant)
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
        void GetModelHashesForShoulders(Singletons::Database::ItemSingleton& itemSingleton, u32 shoulderModelResourcesID, u32& modelHashLeftShoulder, u32& modelHashRightShoulder)
        {
            modelHashLeftShoulder = std::numeric_limits<u32>().max();
            modelHashRightShoulder = std::numeric_limits<u32>().max();

            if (!itemSingleton.shoulderModelResourcesIDToModelMapping.contains(shoulderModelResourcesID))
                return;

            auto& shoulderMapping = itemSingleton.shoulderModelResourcesIDToModelMapping[shoulderModelResourcesID];
            
            modelHashLeftShoulder = shoulderMapping.sideToModelHash[0];
            modelHashRightShoulder = shoulderMapping.sideToModelHash[1];
        }
    }
}
