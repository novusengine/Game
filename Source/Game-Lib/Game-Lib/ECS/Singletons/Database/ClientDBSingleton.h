#pragma once
#include <Base/Types.h>
#include <Base/Util/StringUtils.h>
#include <FileFormat/Novus/ClientDB/ClientDB.h>

#include <robinhood/robinhood.h>
#include <xxhash/xxhash64.h>

#include <array>
#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

class Application;
class ClientDBLoader;

enum class ClientDBHash : u64
{
    TextureFileData                     = XXHash64::hashLiteral("clientdb/texturefiledata.cdb"),
    ModelFileData                       = XXHash64::hashLiteral("clientdb/modelfiledata.cdb"),
    Map                                 = XXHash64::hashLiteral("clientdb/map.cdb"),
    LiquidObject                        = XXHash64::hashLiteral("clientdb/liquidobject.cdb"),
    LiquidType                          = XXHash64::hashLiteral("clientdb/liquidtype.cdb"),
    LiquidMaterial                      = XXHash64::hashLiteral("clientdb/liquidmaterial.cdb"),
    CinematicCamera                     = XXHash64::hashLiteral("clientdb/cinematiccamera.cdb"),
    CinematicSequences                  = XXHash64::hashLiteral("clientdb/cinematicsequences.cdb"),
    CameraSave                          = XXHash64::hashLiteral("clientdb/camerasave.cdb"),
    Cursor                              = XXHash64::hashLiteral("clientdb/cursor.cdb"),
    AnimationData                       = XXHash64::hashLiteral("clientdb/animationdata.cdb"),
    CreatureDisplayInfo                 = XXHash64::hashLiteral("clientdb/creaturedisplayinfo.cdb"),
    CreatureDisplayInfoExtra            = XXHash64::hashLiteral("clientdb/creaturedisplayinfoextra.cdb"),
    CreatureModelData                   = XXHash64::hashLiteral("clientdb/creaturemodeldata.cdb"),
    Item                                = XXHash64::hashLiteral("clientdb/item.cdb"),
    ItemStatTypes                       = XXHash64::hashLiteral("clientdb/itemstattypes.cdb"),
    ItemStatTemplate                    = XXHash64::hashLiteral("clientdb/itemstattemplate.cdb"),
    ItemArmorTemplate                   = XXHash64::hashLiteral("clientdb/itemarmortemplate.cdb"),
    ItemWeaponTemplate                  = XXHash64::hashLiteral("clientdb/itemweapontemplate.cdb"),
    ItemShieldTemplate                  = XXHash64::hashLiteral("clientdb/itemshieldtemplate.cdb"),
    ItemEffects                         = XXHash64::hashLiteral("clientdb/itemeffects.cdb"),
    ItemDisplayInfo                     = XXHash64::hashLiteral("clientdb/itemdisplayinfo.cdb"),
    ItemDisplayMaterialResources        = XXHash64::hashLiteral("clientdb/itemdisplayinfomaterialres.cdb"),
    ItemDisplayModelMaterialResources   = XXHash64::hashLiteral("clientdb/itemdisplayinfomodelmatres.cdb"),
    UnitRace                            = XXHash64::hashLiteral("clientdb/unitrace.cdb"),
    UnitTextureSection                  = XXHash64::hashLiteral("clientdb/unittexturesection.cdb"),
    UnitCustomizationOption             = XXHash64::hashLiteral("clientdb/unitcustomizationoption.cdb"),
    UnitCustomizationGeoset             = XXHash64::hashLiteral("clientdb/unitcustomizationgeoset.cdb"),
    UnitCustomizationMaterial           = XXHash64::hashLiteral("clientdb/unitcustomizationmaterial.cdb"),
    UnitRaceCustomizationChoice         = XXHash64::hashLiteral("clientdb/unitracecustomizationchoice.cdb"),
    Spell                               = XXHash64::hashLiteral("clientdb/spell.cdb"),
    SpellEffects                        = XXHash64::hashLiteral("clientdb/spelleffects.cdb"),
    SpellProcData                       = XXHash64::hashLiteral("clientdb/spellprocdata.cdb"),
    SpellProcLink                       = XXHash64::hashLiteral("clientdb/spellproclink.cdb"),
    Light                               = XXHash64::hashLiteral("clientdb/light.cdb"),
    LightData                           = XXHash64::hashLiteral("clientdb/lightdata.cdb"),
    LightParams                         = XXHash64::hashLiteral("clientdb/lightparams.cdb"),
    LightSkybox                         = XXHash64::hashLiteral("clientdb/lightskybox.cdb"),
    Icon                                = XXHash64::hashLiteral("clientdb/icon.cdb")
};

struct ClientDBDefinition
{
public:
    ClientDBHash hash;
    std::string_view debugName;
};

inline constexpr std::array<ClientDBDefinition, 39> ClientDBHashes =
{{
    { ClientDBHash::TextureFileData, "TextureFileData" },
    { ClientDBHash::ModelFileData, "ModelFileData" },
    { ClientDBHash::Map, "Map" },
    { ClientDBHash::LiquidObject, "LiquidObject" },
    { ClientDBHash::LiquidType, "LiquidType" },
    { ClientDBHash::LiquidMaterial, "LiquidMaterial" },
    { ClientDBHash::CinematicCamera, "CinematicCamera" },
    { ClientDBHash::CinematicSequences, "CinematicSequences" },
    { ClientDBHash::CameraSave, "CameraSave" },
    { ClientDBHash::Cursor, "Cursor" },
    { ClientDBHash::AnimationData, "AnimationData" },
    { ClientDBHash::CreatureDisplayInfo, "CreatureDisplayInfo" },
    { ClientDBHash::CreatureDisplayInfoExtra, "CreatureDisplayInfoExtra" },
    { ClientDBHash::CreatureModelData, "CreatureModelData" },
    { ClientDBHash::Item, "Item" },
    { ClientDBHash::ItemStatTypes, "ItemStatTypes" },
    { ClientDBHash::ItemStatTemplate, "ItemStatTemplate" },
    { ClientDBHash::ItemArmorTemplate, "ItemArmorTemplate" },
    { ClientDBHash::ItemWeaponTemplate, "ItemWeaponTemplate" },
    { ClientDBHash::ItemShieldTemplate, "ItemShieldTemplate" },
    { ClientDBHash::ItemEffects, "ItemEffects" },
    { ClientDBHash::ItemDisplayInfo, "ItemDisplayInfo" },
    { ClientDBHash::ItemDisplayMaterialResources, "ItemDisplayInfoMaterialRes" },
    { ClientDBHash::ItemDisplayModelMaterialResources, "ItemDisplayInfoModelMatRes" },
    { ClientDBHash::UnitRace, "UnitRace" },
    { ClientDBHash::UnitTextureSection, "UnitTextureSection" },
    { ClientDBHash::UnitCustomizationOption, "UnitCustomizationOption" },
    { ClientDBHash::UnitCustomizationGeoset, "UnitCustomizationGeoset" },
    { ClientDBHash::UnitCustomizationMaterial, "UnitCustomizationMaterial" },
    { ClientDBHash::UnitRaceCustomizationChoice, "UnitRaceCustomizationChoice" },
    { ClientDBHash::Spell, "Spell" },
    { ClientDBHash::SpellEffects, "SpellEffects" },
    { ClientDBHash::SpellProcData, "SpellProcData" },
    { ClientDBHash::SpellProcLink, "SpellProcLink" },
    { ClientDBHash::Light, "Light" },
    { ClientDBHash::LightData, "LightData" },
    { ClientDBHash::LightParams, "LightParams" },
    { ClientDBHash::LightSkybox, "LightSkybox" },
    { ClientDBHash::Icon, "Icon" }
}};

inline bool HasClientDBRecordSuffix(std::string_view dbName)
{
    constexpr std::string_view recordSuffix = "Record";
    return dbName.size() >= recordSuffix.size()
        && std::equal(recordSuffix.begin(), recordSuffix.end(), dbName.end() - recordSuffix.size(), [](unsigned char lhs, unsigned char rhs)
        {
            return std::tolower(lhs) == std::tolower(rhs);
        });
}

inline std::string GetClientDBVirtualPath(std::string_view dbName)
{
    std::string virtualPath = "clientdb/";
    virtualPath += dbName;
    virtualPath += ClientDB::FILE_EXTENSION;
    std::transform(virtualPath.begin(), virtualPath.end(), virtualPath.begin(), [](unsigned char character)
    {
        return static_cast<char>(std::tolower(character));
    });
    return virtualPath;
}

inline ClientDBHash GetClientDBHash(std::string_view dbName)
{
    const std::string virtualPath = GetClientDBVirtualPath(dbName);
    return static_cast<ClientDBHash>(XXHash64::hash(virtualPath.data(), virtualPath.size(), 0));
}

inline bool IsBuiltinClientDBHash(ClientDBHash hash)
{
    return std::ranges::any_of(ClientDBHashes, [hash](const ClientDBDefinition& definition)
    {
        return definition.hash == hash;
    });
}

namespace ECS
{
    namespace Singletons
    {
        struct ClientDBSingleton
        {
        public:
            ClientDBSingleton() { }

            void Reserve(u32 numDBs)
            {
                _dbs.reserve(numDBs);
                _dbHashToIndex.reserve(numDBs);
                _dbIndexToHash.reserve(numDBs);
                _dbHashToName.reserve(numDBs);
            }

            // Preferred overload if T meets the requirements.
            template <typename T> requires ClientDB::ValidClientDB<T>
            bool Register()
            {
                std::string dbName = T::NAME;
                StringUtils::ToLower(dbName);
                const ClientDBHash hash = GetClientDBHash(dbName);
                if (!Register(hash, dbName))
                    return false;

                auto* storage = Get(hash);
                if (storage->IsInitialized())
                    return true;

                storage->Initialize<T>();
                storage->MarkDirty();
                return true;
            }

            // Fallback overload to produce a custom error message when T is invalid.
            template <typename T, typename = std::enable_if_t<!ClientDB::ValidClientDB<T>>>
            bool Register()
            {
                static_assert(ClientDB::ValidClientDB<T>, "T must be a struct or class with static members Name of type std::string, NameHash of type u32, FieldInfo of type std::vector<FieldInfo>");
                return false;
            }

            bool Register(ClientDBHash hash, const std::string& dbgName)
            {
                if (Has(hash))
                    return true;

                if (dbgName.length() == 0)
                {
                    NC_LOG_ERROR("ClientDBSingleton : Attempted to register ClientDB with no debug name.");
                    return false;
                }

                auto* cdb = new ClientDB::Data();

                u32 index = static_cast<u32>(_dbs.size());

                _dbs.push_back(cdb);
                _dbHashToIndex[hash] = index;
                _dbIndexToHash[index] = hash;
                _dbHashToName[hash] = dbgName;

                NC_LOG_INFO("ClientDBSingleton : Registered '{0}{1}'", dbgName, ClientDB::FILE_EXTENSION);
                return true;
            }

            bool Replace(ClientDBHash hash, ClientDB::Data* newStorage)
            {
                if (!Has(hash))
                    return false;

                u32 index = _dbHashToIndex[hash];

                ClientDB::Data* oldStorage = _dbs[index];
                oldStorage->Clear();
                delete oldStorage;

                _dbs[index] = newStorage;
                return true;
            }

            bool Remove(ClientDBHash hash)
            {
                if (!Has(hash))
                    return false;

                u32 index = _dbHashToIndex[hash];

                ClientDB::Data* storage = _dbs[index];
                if (storage)
                {
                    storage->Clear();
                    delete storage;
                    _dbs[index] = nullptr;
                }

                _dbHashToIndex.erase(hash);
                _dbIndexToHash.erase(index);
                _dbHashToName.erase(hash);
                return true;
            }

            u32 Count()
            {
                u32 numDBs = static_cast<u32>(_dbHashToIndex.size());
                return numDBs;
            }

            bool Has(ClientDBHash hash)
            {
                return _dbHashToIndex.contains(hash);
            }

            ClientDB::Data* Get(ClientDBHash hash)
            {
                if (!_dbHashToIndex.contains(hash))
                {
                    NC_LOG_CRITICAL("ClientDBSingleton : Storage does not exist in the lookup table, meaning it was not loaded.");
                    return nullptr;
                }

                u32 index = _dbHashToIndex[hash];
                return _dbs[index];
            }

            void Each(std::function<void(ClientDBHash dbHash, ClientDB::Data* db)> callback)
            {
                u32 numDBs = static_cast<u32>(_dbs.size());

                for (u32 i = 0; i < numDBs; i++)
                {
                    if (!_dbIndexToHash.contains(i))
                        continue;

                    ClientDBHash hash = _dbIndexToHash[i];
                    callback(hash, _dbs[i]);
                }
            }

            void EachInRange(u32 startIndex, u32 count, std::function<void(ClientDBHash dbHash, ClientDB::Data* db)> callback)
            {
                u32 numDBs = Count();
                if (startIndex + count >= numDBs)
                    return;

                for (u32 i = 0; i <= count; i++)
                {
                    if (!_dbIndexToHash.contains(startIndex + i))
                        continue;

                    ClientDBHash hash = _dbIndexToHash[startIndex + i];
                    callback(hash, _dbs[startIndex + i]);
                }
            }

            const std::string& GetDBName(ClientDBHash hash)
            {
                if (!_dbHashToName.contains(hash))
                {
                    NC_LOG_CRITICAL("ClientDBSingleton : Storage does not exist in the lookup table, meaning it was not loaded.");
                }

                return _dbHashToName[hash];
            }

        private:
            struct Entry
            {
            public:
                u32 hash;
                std::string name;
            };

            std::vector<ClientDB::Data*> _dbs;
            robin_hood::unordered_map<ClientDBHash, u32> _dbHashToIndex;
            robin_hood::unordered_map<u32, ClientDBHash> _dbIndexToHash;
            robin_hood::unordered_map<ClientDBHash, std::string> _dbHashToName;
        };
    }
}
