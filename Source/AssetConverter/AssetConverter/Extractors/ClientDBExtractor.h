#pragma once
#include <FileFormat/Novus/ClientDB/ClientDB.h>
#include <FileFormat/Novus/ClientDB/Definitions.h>

#include <string>
#include <functional>
#include <vector>

using namespace DB;
class ClientDBExtractor
{
public:
	static void Process();

private:
	static bool ExtractMap();

public:
	static Client::ClientDB<Client::Definitions::Map> maps;

private:
	struct ExtractionEntry
	{
	public:
		ExtractionEntry(std::string inName, std::string inDescription, std::function<bool()> inFunction) : name(inName), description(inDescription), function(inFunction) { }
		
		const std::string name;
		const std::string description;
		const std::function<bool()> function;
	};

	static std::vector<ExtractionEntry> _extractionEntries;
};