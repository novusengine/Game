#include "MapObjectExtractor.h"
#include "AssetConverter/Runtime.h"
#include "AssetConverter/Casc/CascLoader.h"
#include "AssetConverter/Util/ServiceLocator.h"

#include <Base/Util/DebugHandler.h>
#include <Base/Util/StringUtils.h>

#include <FileFormat/Novus/Model/MapObject.h>
#include <FileFormat/Warcraft/Shared.h>
#include <FileFormat/Warcraft/WMO/Wmo.h>
#include <FileFormat/Warcraft/Parsers/WmoParser.h>

#include <filesystem>
namespace fs = std::filesystem;

void MapObjectExtractor::Process()
{
	Runtime* runtime = ServiceLocator::GetRuntime();
	CascLoader* cascLoader = ServiceLocator::GetCascLoader();

	const CascListFile& listFile = cascLoader->GetListFile();
	const robin_hood::unordered_map<std::string, u32>& filePathToIDMap = listFile.GetFilePathToIDMap();

	struct FileListEntry
	{
		u32 fileID = 0;
		std::string fileName;
		std::string path;
	};

	std::vector<FileListEntry> fileList = { };
	fileList.reserve(filePathToIDMap.size());

	for (auto& itr : filePathToIDMap)
	{
		if (!StringUtils::EndsWith(itr.first, ".wmo"))
			continue;

		// Determine if the wmo is a root file
		{
			u32 bytesToSkip = sizeof(u32) + sizeof(u32) + sizeof(MVER);
			u32 bytesToRead = bytesToSkip + sizeof(u32);

			std::shared_ptr<Bytebuffer> buffer = cascLoader->GetFilePartialByID(itr.second, bytesToRead);
			if (buffer == nullptr)
				continue;

			buffer->SkipRead(bytesToSkip);

			u32 chunkToken = 0;
			if (!buffer->GetU32(chunkToken))
				continue;

			if (chunkToken != 'MOHD')
				continue;
		}

		std::string wmoPathStr = itr.first;
		std::transform(wmoPathStr.begin(), wmoPathStr.end(), wmoPathStr.begin(), ::tolower);

		fs::path outputPath = (runtime->paths.mapObject / wmoPathStr).replace_extension("mapobject");
		fs::create_directories(outputPath.parent_path());

		FileListEntry& fileListEntry = fileList.emplace_back();
		fileListEntry.fileID = itr.second;
		fileListEntry.fileName = outputPath.filename().string();
		fileListEntry.path = outputPath.string();
	}

	u32 numFiles = static_cast<u32>(fileList.size());
	u16 progressFlags = 0;
	DebugHandler::Print("[MapObject Extractor] Processing %u files", numFiles);

	Wmo::Parser wmoParser = { };
	for (u32 i = 0; i < numFiles; i++)
	{
		const FileListEntry& fileListEntry = fileList[i];

		Wmo::Layout wmo = { };
		std::shared_ptr<Bytebuffer> rootBuffer = cascLoader->GetFileByID(fileListEntry.fileID);
		if (!wmoParser.TryParse(Wmo::Parser::ParseType::Root, rootBuffer, wmo))
			continue;

		for (u32 i = 0; i < wmo.mohd.groupCount; i++)
		{
			u32 fileID = wmo.gfid.data[i].fileID;
			if (fileID == 0)
				continue;

			std::shared_ptr<Bytebuffer> groupBuffer = cascLoader->GetFileByID(fileID);
			if (!groupBuffer)
				continue;

			if (!wmoParser.TryParse(Wmo::Parser::ParseType::Group, groupBuffer, wmo))
				continue;
		}

		Model::MapObject mapObject = { };
		if (!Model::MapObject::FromWMO(wmo, mapObject))
			continue;

		// Post Processing
		{
			std::string pathAsString = "";

			// Convert Material FileIDs to TextureHash
			for (u32 i = 0; i < mapObject.materials.size(); i++)
			{
				Model::MapObject::Material& material = mapObject.materials[i];

				for (u32 j = 0; j < 3; j++)
				{
					u32 textureFileID = material.textureID[j];
					if (textureFileID == std::numeric_limits<u32>().max())
						continue;

					const std::string& cascFilePath = cascLoader->GetFilePathFromListFileID(textureFileID);
					if (cascFilePath.size() == 0)
					{
						material.textureID[j] = std::numeric_limits<u32>().max();
						continue;
					}

					fs::path texturePath = cascFilePath;
					texturePath.replace_extension("dds").make_preferred();

					pathAsString = texturePath.string();
					std::transform(pathAsString.begin(), pathAsString.end(), pathAsString.begin(), ::tolower);

					material.textureID[j] = StringUtils::fnv1a_32(pathAsString.c_str(), pathAsString.length());
				}
			}

			// Convert Decoration FileIDs to PathHash
			{
				for (u32 i = 0; i < mapObject.decorations.size(); i++)
				{
					Model::MapObject::Decoration& decoration = mapObject.decorations[i];

					u32 decorationFileID = decoration.nameID;
					if (decorationFileID == std::numeric_limits<u32>().max())
						continue;

					const std::string& cascFilePath = cascLoader->GetFilePathFromListFileID(decorationFileID);
					if (cascFilePath.size() == 0)
					{
						decoration.nameID = std::numeric_limits<u32>().max();
						continue;
					}

					fs::path cmodelPath = cascFilePath;
					cmodelPath.replace_extension("cmodel");

					pathAsString = cmodelPath.string();
					std::transform(pathAsString.begin(), pathAsString.end(), pathAsString.begin(), ::tolower);

					decoration.nameID = StringUtils::fnv1a_32(pathAsString.c_str(), pathAsString.length());
				}
			}
		}

		bool result = mapObject.Save(fileListEntry.path);
		if (runtime->isInDebugMode)
		{
			if (result)
			{
				DebugHandler::PrintSuccess("[MapObject Extractor] Extracted %s", fileListEntry.fileName.c_str());
			}
			else
			{
				DebugHandler::PrintWarning("[MapObject Extractor] Failed to extract %s", fileListEntry.fileName.c_str());
			}
		}

		f32 progress = (static_cast<f32>(i) / static_cast<f32>(numFiles - 1)) * 10.0f;
		u32 bitToCheck = static_cast<u32>(progress);
		u32 bitMask = 1 << bitToCheck;

		bool reportStatus = (progressFlags & bitMask) == 0;
		if (reportStatus)
		{
			progressFlags |= bitMask;
			DebugHandler::PrintSuccess("[MapObject Extractor] Progress Status (%.2f%% / 100%%)", progress * 10.0f);
		}
	}
}