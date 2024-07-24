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
        Map					        = GetHash("Map.cdb"_h),
        LiquidObject		        = GetHash("LiquidObject.cdb"_h),
        LiquidType			        = GetHash("LiquidType.cdb"_h),
        LiquidMaterial		        = GetHash("LiquidMaterial.cdb"_h),
        CinematicCamera		        = GetHash("CinematicCamera.cdb"_h),
        CinematicSequence	        = GetHash("CinematicSequence.cdb"_h),
        CameraSave			        = GetHash("CameraSave.cdb"_h),
        Cursor				        = GetHash("Cursor.cdb"_h),
        AnimationData		        = GetHash("AnimationData.cdb"_h),
        CreatureDisplayInfo         = GetHash("CreatureDisplayInfo.cdb"_h),
        CreatureDisplayInfoExtra    = GetHash("CreatureDisplayInfoExtra.cdb"_h),
        CreatureModelData           = GetHash("CreatureModelData.cdb"_h),
        CharSection                 = GetHash("CharSection.cdb"_h),
    };

    struct ClientDBCollection
    {
    public:
        ClientDBCollection() { }

        template <typename T>
        bool Register(ClientDBHash hash, const std::string& dbName)
        {
            constexpr size_t STORAGE_SIZE = sizeof(T);

            if (_dbHashToIndex.contains(hash))
                return true;

            if (dbName.length() == 0)
            {
                NC_LOG_ERROR("ClientDBCollection : Attempted to register ClientDB with no dbName.");
                return false;
            }

            std::string dbFileName = dbName + ClientDB::FILE_EXTENSION;
            ClientDBHash dbFileNameHash = static_cast<ClientDBHash>(StringUtils::fnv1a_32(dbFileName.c_str(), dbFileName.size()));

            if (dbFileNameHash != hash)
            {
                NC_LOG_ERROR("ClientDBCollection : Attempted to register ClientDB \"{0}\" with mismatching ClientDBHash", dbName);
                return false;
            }

            auto* rawDB = new ClientDB::Storage<ClientDB::Definitions::Empty>(dbName);

            u32 index = static_cast<u32>(_dbs.size());

            _dbs.push_back(rawDB);
            _dbHashToIndex[hash] = index;

            rawDB->SetDirty();

            NC_LOG_INFO("ClientDBCollection : Registered '{0}{1}'", dbName, ClientDB::FILE_EXTENSION);
            return true;
        }

        template <typename T>
        ClientDB::Storage<T>* Get(ClientDBHash hash)
        {
            if (!_dbHashToIndex.contains(hash))
            {
                NC_LOG_CRITICAL("ClientDBCollection : Storage does not exist in the lookup table, meaning it was not loaded.");
            }

            u32 index = _dbHashToIndex[hash];
            ClientDB::Storage<T>* storage = reinterpret_cast<ClientDB::Storage<T>*>(_dbs[index]);

            return storage;
        }

    private:
        struct Entry
        {
        public:
            u32 hash;
            std::string name;
        };

        friend class ::Application;
        friend class ::ClientDBLoader;

        std::vector<ClientDB::Storage<ClientDB::Definitions::Empty>*> _dbs;
        robin_hood::unordered_map<ClientDBHash, u32> _dbHashToIndex;
    };
}
