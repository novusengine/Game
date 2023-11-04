#pragma once
#include <Base/Types.h>

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
		Map					= GetHash("Map.cdb"_h),
		LiquidObject		= GetHash("LiquidObject.cdb"_h),
		LiquidType			= GetHash("LiquidType.cdb"_h),
		LiquidMaterial		= GetHash("LiquidMaterial.cdb"_h),
		CinematicCamera		= GetHash("CinematicCamera.cdb"_h),
		CinematicSequence	= GetHash("CinematicSequence.cdb"_h),
		CameraSave			= GetHash("CameraSave.cdb"_h),
		Cursor				= GetHash("Cursor.cdb"_h)
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
			{
				u32 index = _dbHashToIndex[hash];

				ClientDB::StorageRaw* rawDB = _dbs[index];
				if (!rawDB->Init(STORAGE_SIZE))
				{
					DebugHandler::PrintFatal("ClientDBCollection : Failed to Initialize Storage. The Register call may have passed an incorrect Type with a size of 0. Size must be greater than 0.");
				}

				return true;
			}

			if (dbName.length() == 0)
			{
				DebugHandler::PrintError("ClientDBCollection : Attempted to register ClientDB with no dbName.");
				return false;
			}

			std::string dbFileName = dbName + ClientDB::FILE_EXTENSION;
			ClientDBHash dbFileNameHash = static_cast<ClientDBHash>(StringUtils::fnv1a_32(dbFileName.c_str(), dbFileName.size()));

			if (dbFileNameHash != hash)
			{
				DebugHandler::PrintError("ClientDBCollection : Attempted to register ClientDB \"{0}\" with mismatching ClientDBHash", dbName);
				return false;
			}

			auto* rawDB = new ClientDB::StorageRaw(dbName);
			if (!rawDB->Init(STORAGE_SIZE))
			{
				DebugHandler::PrintFatal("ClientDBCollection : Failed to Initialize Storage. The Register call may have passed an incorrect Type with a size of 0. Size must be greater than 0.");
			}

			u32 index = static_cast<u32>(_dbs.size());

			_dbs.push_back(rawDB);
			_dbHashToIndex[hash] = index;

			rawDB->MarkDirty();
			DebugHandler::Print("ClientDBCollection : Registered {0}", dbName);
			return true;
		}

		template <typename T>
		ClientDB::Storage<T> Get(ClientDBHash hash)
		{
			if (!_dbHashToIndex.contains(hash))
			{
				DebugHandler::PrintFatal("ClientDBCollection : Storage does not exist in the lookup table, meaning it was not loaded.");
			}

			u32 index = _dbHashToIndex[hash];
			ClientDB::StorageRaw* storage = _dbs[index];

			if (!storage->IsInitialized())
			{
				DebugHandler::PrintFatal("ClientDBCollection : Failed to Get Storage. The Storage was not registered with the Register function.");
			}

			if (storage->GetSizeOfElement() != sizeof(T))
			{
				DebugHandler::PrintFatal("ClientDBCollection : Failed to Get Storage. The Storage does not contain elements of the passed type.");
			}

			return ClientDB::Storage<T>(storage);
		}

	private:
		struct Entry
		{
		public:
			u32 hash;
			std::string name;
		};

		friend class Application;
		friend class ClientDBLoader;

		std::vector<ClientDB::StorageRaw*> _dbs;
		robin_hood::unordered_map<ClientDBHash, u32> _dbHashToIndex;
	};
}