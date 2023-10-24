#pragma once
#include <Base/Types.h>

#include <FileFormat/Novus/ClientDB/ClientDB.h>
#include <FileFormat/Novus/ClientDB/Definitions.h>

#include <robinhood/robinhood.h>

namespace ECS::Singletons
{
	struct LiquidDB
	{
	public:
		LiquidDB() {}

		DB::Client::ClientDB<DB::Client::Definitions::LiquidType> liquidTypes;
	};
}