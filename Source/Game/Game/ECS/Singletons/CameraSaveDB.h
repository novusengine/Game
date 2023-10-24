#pragma once
#include <Base/Types.h>

#include <FileFormat/Novus/ClientDB/ClientDB.h>
#include <FileFormat/Novus/ClientDB/Definitions.h>

#include <robinhood/robinhood.h>

namespace ECS::Singletons
{
	struct CameraSaveDB
	{
	public:
		CameraSaveDB() {}

		DB::Client::ClientDB<DB::Client::Definitions::CameraSave> entries;
		robin_hood::unordered_map<u32, u32> cameraSaveNameHashToID;

		bool IsDirty() { return _isDirty; }
		void SetDirty() { _isDirty = true; }
		void ClearDirty() { _isDirty = false; }

	private:
		bool _isDirty = false;
	};
}