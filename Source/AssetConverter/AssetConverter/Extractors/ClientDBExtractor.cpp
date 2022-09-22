#include "ClientDBExtractor.h"
#include "AssetConverter/Runtime.h"
#include "AssetConverter/Casc/CascLoader.h"
#include "AssetConverter/Util/ServiceLocator.h"

#include <Base/Container/StringTable.h>
#include <Base/Memory/FileWriter.h>
#include <Base/Util/DebugHandler.h>
#include <Base/Util/StringUtils.h>

#include <FileFormat/Warcraft/DB2/DB2Definitions.h>
#include <FileFormat/Warcraft/DB2/Wdc3.h>
#include <FileFormat/Warcraft/Parsers/Wdc3Parser.h>
#include <FileFormat/Novus/ClientDB/ClientDB.h>
#include <FileFormat/Novus/ClientDB/Definitions.h>

#include <filesystem>
namespace fs = std::filesystem;

std::vector<ClientDBExtractor::ExtractionEntry> ClientDBExtractor::_extractionEntries =
{
	{ "Map.db2", "A collection of all maps", ClientDBExtractor::ExtractMap }
};

Client::ClientDB<Client::Definitions::Map> ClientDBExtractor::maps;

void ClientDBExtractor::Process()
{
	for (u32 i = 0; i < _extractionEntries.size(); i++)
	{
		const ExtractionEntry& entry = _extractionEntries[i];

		if (entry.function())
		{
			DebugHandler::PrintSuccess("[ClientDBExtractor] Extracted (\"%s\" : \"%s\")", entry.name.c_str(), entry.description.c_str());
		}
		else
		{
			DebugHandler::PrintWarning("[ClientDBExtractor] Failed to extract (\"%s\" : \"%s\")", entry.name.c_str(), entry.description.c_str());
		}
	}
}

u32 GetStringIndexFromRecordIndex(DB2::WDC3::Layout& layout, DB2::WDC3::Parser& db2Parser, u32 recordIndex, u32 fieldIndex, StringTable& stringTable)
{
	std::string value = db2Parser.GetString(layout, recordIndex, fieldIndex);

	if (value.length() == 0)
		return std::numeric_limits<u32>().max();

	if (StringUtils::EndsWith(value, ".mdx"))
	{
		value = value.substr(0, value.length() - 4) + ".cmodel";
	}
	else if (StringUtils::EndsWith(value, ".m2"))
	{
		value = value.substr(0, value.length() - 3) + ".cmodel";
	}
	else if (StringUtils::EndsWith(value, ".blp"))
	{
		value = value.substr(0, value.length() - 4) + ".dds";
		std::transform(value.begin(), value.end(), value.begin(), ::tolower);
	}

	return stringTable.AddString(value);
}

bool ClientDBExtractor::ExtractMap()
{
	CascLoader* cascLoader = ServiceLocator::GetCascLoader();

	DB2::WDC3::Layout layout = { };
	DB2::WDC3::Parser db2Parser = { };

	std::shared_ptr<Bytebuffer> buffer = cascLoader->GetFileByListFilePath("dbfilesclient/map.db2");
	if (!buffer || !db2Parser.TryParse(buffer, layout))
		return false;

	const DB2::WDC3::Layout::Header& header = layout.header;

	maps.data.clear();
	maps.data.reserve(header.recordCount);

	maps.stringTable.Clear();
	maps.stringTable.Reserve(static_cast<u64>(header.recordCount) * 2);

	/*
		{ "Directory",					0,	32	},
		{ "MapName_lang",				1,	32	},
		{ "MapDescription0_lang",		2,	32	},
		{ "MapDescription1_lang",		3,	32	},
		{ "PvpShortDescription_lang",	4,	32	},
		{ "PvpLongDescription_lang",	5,	32	},
		{ "MapType",					6,	8	},
		{ "InstanceType",				7,	8	},
		{ "ExpansionID",				8,	8	},
		{ "AreaTableID",				9,	16	},
		{ "LoadingScreenID",			10, 16	},
		{ "TimeOfDayOverride",			11, 16	},
		{ "ParentMapID",				12, 16	},
		{ "CosmeticParentMapID",		13, 16	},
		{ "TimeOffset",					14, 8	},
		{ "MinimapIconScale",			15, 32	},
		{ "RaidOffset",					16, 32	},
		{ "CorpseMapID",				17, 16	},
		{ "MaxPlayers",					18, 8	},
		{ "WindSettingsID",				19, 16	},
		{ "ZmpFileDataID",				20, 32	},
		{ "Flags",						21, 32	}
	*/

	for (u32 db2RecordIndex = 0; db2RecordIndex < header.recordCount; db2RecordIndex++)
	{
		u32 sectionID = 0;
		u32 recordID = 0;
		u8* recordData = nullptr;

		if (!db2Parser.TryReadRecord(layout, db2RecordIndex, sectionID, recordID, recordData))
			continue;

		std::string internalName = db2Parser.GetString(layout, db2RecordIndex, 0);
		std::string cascPath = "world/maps/" + internalName + "/" + internalName + ".wdt";
		std::transform(cascPath.begin(), cascPath.end(), cascPath.begin(), ::tolower);

		u32 fileID = cascLoader->GetFileIDFromListFilePath(cascPath);

		bool hasWDTFile = fileID > 0 && cascLoader->FileExistsInCasc(fileID);
		if (hasWDTFile)
		{
			DB::Client::Definitions::Map& map = maps.data.emplace_back();

			map.id = db2Parser.GetRecordIDFromIndex(layout, db2RecordIndex);
			map.name = GetStringIndexFromRecordIndex(layout, db2Parser, db2RecordIndex, 1, maps.stringTable);
			map.internalName = GetStringIndexFromRecordIndex(layout, db2Parser, db2RecordIndex, 0, maps.stringTable);
			map.instanceType = *db2Parser.GetField<u8>(layout, sectionID, recordID, recordData, 7);

			const u32* flags = db2Parser.GetField<u32>(layout, sectionID, recordID, recordData, 21);
			map.flags = flags[0];

			map.expansion = *db2Parser.GetField<u8>(layout, sectionID, recordID, recordData, 8);
			map.maxPlayers = *db2Parser.GetField<u8>(layout, sectionID, recordID, recordData, 18);
		}
	}

	size_t size = maps.GetSerializedSize();
	std::shared_ptr<Bytebuffer> resultBuffer = Bytebuffer::BorrowRuntime(size);

	fs::path path = ServiceLocator::GetRuntime()->paths.clientDB / "Map.cdb";
	FileWriter fileWriter(path, resultBuffer);

	if (!maps.Write(resultBuffer))
		return false;

	if (!fileWriter.Write())
		return false;

	return true;
}
