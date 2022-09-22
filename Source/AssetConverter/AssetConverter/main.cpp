#include "Runtime.h"
#include "Casc/CascLoader.h"
#include "Extractors/ClientDBExtractor.h"
#include "Extractors/MapExtractor.h"
#include "Extractors/MapObjectExtractor.h"
#include "Extractors/ComplexModelExtractor.h"
#include "Util/ServiceLocator.h"

#include <Base/Types.h>
#include <Base/Util/JsonUtils.h>
#include <Base/Util/DebugHandler.h>

#include <filesystem>
#include <FileFormat/Warcraft/M2/M2.h>
#include <FileFormat/Warcraft/Parsers/M2Parser.h>
#include <FileFormat/Novus/Model/ComplexModel.h>
namespace fs = std::filesystem;

i32 main()
{
	Runtime* runtime = ServiceLocator::SetRuntime(new Runtime());

	// Setup Runtime
	{
		// Setup Paths
		{
			Runtime::Paths& paths = runtime->paths;

			paths.executable = fs::current_path();
			paths.data = paths.executable / "Data";
			paths.clientDB = paths.data / "ClientDB";
			paths.texture = paths.data / "Texture";
			paths.map = paths.data / "Map";
			paths.mapObject = paths.data / "MapObject";
			paths.complexModel = paths.data / "ComplexModel";

			fs::create_directory(paths.data);
			fs::create_directory(paths.clientDB);
			fs::create_directory(paths.texture);
			fs::create_directory(paths.map);
			fs::create_directory(paths.mapObject);
			fs::create_directory(paths.complexModel);
		}

		// Setup Json
		{
			static const std::string CONFIG_VERSION = "0.1";
			static const std::string CONFIG_NAME = "AssetConverterConfig.json";

			nlohmann::ordered_json fallbackJson;

			// Create Default
			{
				fallbackJson["General"] =
				{
					{ "Version",		CONFIG_VERSION },
					{ "DebugMode",		false }
				};

				fallbackJson["Casc"] =
				{
					{ "ListFile",		"listfile.csv" }
				};

				fallbackJson["Extraction"] =
				{
					{ "Enabled",		true },
					{ "ClientDB",
						{
							{ "Enabled",	true }
						}
					},
					{ "Map",		
						{
							{ "Enabled",	true },
							{ "BlendMaps",	true }
						}
					},
					{ "MapObject",		
						{
							{ "Enabled",	true }
						}
					},
					{ "ComplexModel",		
						{
							{ "Enabled",	true }
						}
					}
				};
			}

			fs::path configPath = runtime->paths.executable / CONFIG_NAME;
			std::string absolutePath = fs::absolute(configPath).string();

			if (!JsonUtils::LoadFromPathOrCreate(runtime->json, fallbackJson, configPath))
			{
				DebugHandler::PrintFatal("[Runtime] Failed to Load %s from %s", CONFIG_NAME, absolutePath.c_str());
			}

			std::string currentVersion = runtime->json["General"]["Version"];
			if (currentVersion != CONFIG_VERSION)
			{
				DebugHandler::PrintFatal("[Runtime] Attempted to load outdated %s. (Config Version : %s, Expected Version : %s)", CONFIG_NAME.c_str(), currentVersion.c_str(), CONFIG_VERSION.c_str());
			}

			runtime->isInDebugMode = runtime->json["General"]["DebugMode"];
		}
	}

	// Setup CascLoader
	{
		std::string listFile = runtime->json["Casc"]["ListFile"];
		ServiceLocator::SetCascLoader(new CascLoader(listFile));
	}

	// Run Extractors
	{
		CascLoader* cascLoader = ServiceLocator::GetCascLoader();

		CascLoader::Result result = cascLoader->Load();
		switch (result)
		{
			case CascLoader::Result::Success:
			{
				DebugHandler::Print("");

				bool isExtractingEnabled = runtime->json["Extraction"]["Enabled"];
				if (isExtractingEnabled)
				{
					DebugHandler::Print("[AssetConverter] Processing Extractors...");

					// DB2
					bool isDB2Enabled = runtime->json["Extraction"]["ClientDB"]["Enabled"];
					if (isDB2Enabled)
					{
						DebugHandler::Print("[AssetConverter] Processing ClientDB Extractor...");
						ClientDBExtractor::Process();
						DebugHandler::Print("[AssetConverter] ClientDB Extractor Finished\n");
					}

					// Map
					bool isMapEnabled = runtime->json["Extraction"]["Map"]["Enabled"];
					if (isMapEnabled)
					{
						DebugHandler::Print("[AssetConverter] Processing Map Extractor...");
						MapExtractor::Process();
						DebugHandler::Print("[AssetConverter] Map Extractor Finished\n");
					}

					// Map Object
					bool isMapObjectEnabled = runtime->json["Extraction"]["MapObject"]["Enabled"];
					if (isMapObjectEnabled)
					{
						DebugHandler::Print("[AssetConverter] Processing MapObject Extractor...");
						MapObjectExtractor::Process();
						DebugHandler::Print("[AssetConverter] MapObject Extractor Finished\n");
					}

					// Complex Model
					bool isComplexModelEnabled = runtime->json["Extraction"]["ComplexModel"]["Enabled"];
					if (isComplexModelEnabled)
					{
						DebugHandler::Print("[AssetConverter] Processing ComplexModel Extractor...");
						ComplexModelExtractor::Process();
						DebugHandler::Print("[AssetConverter] ComplexModel Extractor Finished\n");
					}
				}

				cascLoader->Close();
				break;
			}

			case CascLoader::Result::MissingCasc:
			{
				DebugHandler::PrintError("[CascLoader] Could not load Casc. Failed to find Installation");
				break;
			}

			case CascLoader::Result::MissingListFile:
			{
				DebugHandler::PrintError("[CascLoader] Could not load Casc. Failed to find Listfile");
				break;
			}

			case CascLoader::Result::AlreadyInitialized:
			{
				DebugHandler::PrintError("[CascLoader] Could not load Casc. Already Initialized.");
				break;
			}

			default:
			{
				DebugHandler::PrintError("[CascLoader] Could not load Casc. Unknown Result.");
				break;
			}
		}
	}

	DebugHandler::Print("");
	DebugHandler::PrintSuccess("Finished... Press 'Enter' to exit");
	std::cin.get();

	return 0;
}