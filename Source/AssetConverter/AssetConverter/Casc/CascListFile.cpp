#include "CascListFile.h"

#include <Base/Memory/FileReader.h>

#include <filesystem>
namespace fs = std::filesystem;

bool CascListFile::Initialize()
{
	fs::path listFilePath = _listPath;
	if (!fs::exists(listFilePath))
		return false;

	const std::string pathAsString = listFilePath.string();
	const std::string fileNameAsString = listFilePath.filename().string();

	FileReader reader(pathAsString, fileNameAsString);
	if (!reader.Open())
		return false;

	_fileBuffer = new Bytebuffer(nullptr, reader.Length());
	reader.Read(_fileBuffer, _fileBuffer->size);

	ParseListFile();

	return true;
}

void CascListFile::ParseListFile()
{
	char* buffer = reinterpret_cast<char*>(_fileBuffer->GetDataPointer());
	size_t bufferSize = _fileBuffer->size;

	while (_fileBuffer->GetReadSpace())
	{
		u32 fileID = 0;
		std::string filePath = "";

		// Read fileID
		{
			size_t numberStartIndex = _fileBuffer->readData;

			char c = 0;
			for (size_t i = numberStartIndex; i < bufferSize; i++)
			{
				if (!_fileBuffer->Get<char>(c))
					return;

				if (c == ';')
					break;
			}

			size_t numberEndIndex = _fileBuffer->readData - 1;
			std::string numberStr = std::string(&buffer[numberStartIndex], (numberEndIndex - numberStartIndex));
			fileID = std::stoi(numberStr);
		}

		// Read fileID
		{
			size_t pathStartIndex = _fileBuffer->readData;

			char c = 0;
			for (size_t i = pathStartIndex; i < bufferSize; i++)
			{
				if (!_fileBuffer->Get<char>(c))
					return;

				if (c == '\n')
					break;
			}

			size_t pathEndIndex = _fileBuffer->readData - 1;
			size_t strSize = pathEndIndex - pathStartIndex;
			filePath = std::string(&buffer[pathStartIndex], strSize);
		}

		_fileIDToPath[fileID] = filePath;
		_filePathToID[filePath] = fileID;
	}
}