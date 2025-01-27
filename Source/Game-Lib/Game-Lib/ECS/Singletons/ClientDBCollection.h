#pragma once
#include <Base/Types.h>
#include <Base/Util/StringUtils.h>

#include <FileFormat/Novus/ClientDB/ClientDB.h>
#include <FileFormat/Novus/ClientDB/Definitions.h>

#include <robinhood/robinhood.h>

class Application;
class ClientDBLoader;

namespace ECS::Singletons
{
    // This exist because without it, MSVC will complain about requiring a narrow conversion
    constexpr u32 GetHash(u32 hash)
    {
        return hash;
    }

    enum class ClientDBHash : u32
    {
        TextureFileData                     = GetHash("TextureFileData"_h),
        ModelFileData                       = GetHash("ModelFileData"_h),
        Map					                = GetHash("Map"_h),
        LiquidObject		                = GetHash("LiquidObject"_h),
        LiquidType			                = GetHash("LiquidType"_h),
        LiquidMaterial		                = GetHash("LiquidMaterial"_h),
        CinematicCamera		                = GetHash("CinematicCamera"_h),
        CinematicSequence	                = GetHash("CinematicSequence"_h),
        CameraSave			                = GetHash("CameraSave"_h),
        Cursor				                = GetHash("Cursor"_h),
        AnimationData		                = GetHash("AnimationData"_h),
        CreatureDisplayInfo                 = GetHash("CreatureDisplayInfo"_h),
        CreatureDisplayInfoExtra            = GetHash("CreatureDisplayInfoExtra"_h),
        CreatureModelData                   = GetHash("CreatureModelData"_h),
        Item                                = GetHash("Item"_h),
        ItemDisplayInfo                     = GetHash("ItemDisplayInfo"_h),
        ItemDisplayMaterialResources        = GetHash("ItemDisplayInfoMaterialRes"_h),
        ItemDisplayModelMaterialResources   = GetHash("ItemDisplayInfoModelMatRes"_h),
        Light                               = GetHash("Light"_h),
        LightData                           = GetHash("LightData"_h),
        LightParams                         = GetHash("LightParams"_h),
        LightSkybox                         = GetHash("LightSkybox"_h)
    };

    struct ClientDBCollection
    {
    public:
        ClientDBCollection() { }

        void Reserve(u32 numDBs)
        {
            _dbs.reserve(numDBs);
            _dbHashToIndex.reserve(numDBs);
            _dbIndexToHash.reserve(numDBs);
            _dbHashToName.reserve(numDBs);
        }

        bool Register(ClientDBHash hash, const std::string& dbName)
        {
            if (Has(hash))
                return true;

            if (dbName.length() == 0)
            {
                NC_LOG_ERROR("ClientDBCollection : Attempted to register ClientDB with no dbName.");
                return false;
            }

            ClientDBHash dbFileNameHash = static_cast<ClientDBHash>(StringUtils::fnv1a_32(dbName.c_str(), dbName.size()));
            if (dbFileNameHash != hash)
            {
                NC_LOG_ERROR("ClientDBCollection : Attempted to register ClientDB \"{0}\" with mismatching ClientDBHash", dbName);
                return false;
            }

            auto* cdb = new ClientDB::Data();

            u32 index = static_cast<u32>(_dbs.size());

            _dbs.push_back(cdb);
            _dbHashToIndex[hash] = index;
            _dbIndexToHash[index] = hash;
            _dbHashToName[hash] = dbName;

            NC_LOG_INFO("ClientDBCollection : Registered '{0}{1}'", dbName, ClientDB::FILE_EXTENSION);
            return true;
        }

        bool Remove(ClientDBHash hash)
        {
            if (!Has(hash))
                return false;

            u32 index = _dbHashToIndex[hash];
            std::string name = _dbHashToName[hash];

            _dbHashToIndex.erase(hash);
            _dbIndexToHash.erase(index);
            _dbHashToName.erase(hash);

            std::filesystem::path absolutePath = std::filesystem::absolute("Data/ClientDB").make_preferred();
            std::filesystem::path savePath = std::filesystem::path(absolutePath / name).replace_extension(ClientDB::FILE_EXTENSION);

            if (!std::filesystem::exists(savePath))
                return false;

            std::filesystem::remove(savePath);
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
                NC_LOG_CRITICAL("ClientDBCollection : Storage does not exist in the lookup table, meaning it was not loaded.");
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
                NC_LOG_CRITICAL("ClientDBCollection : Storage does not exist in the lookup table, meaning it was not loaded.");
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
