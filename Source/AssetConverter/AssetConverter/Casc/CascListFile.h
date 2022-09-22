#pragma once
#include <Base/Types.h>

#include <robinhood/robinhood.h>

class Bytebuffer;
struct CascListFile
{
public:
	CascListFile(std::string listPath) : _listPath(listPath) { }

	bool Initialize();

	bool HasFileWithID(u32 fileID) { return _fileIDToPath.find(fileID) != _fileIDToPath.end(); }
	const std::string& GetFilePathFromID(u32 fileID) { return _fileIDToPath[fileID]; }

	bool HasFileWithPath(const std::string& filePath) { return _filePathToID.find(filePath) != _filePathToID.end(); }
	u32 GetFileIDFromPath(const std::string& filePath) { return _filePathToID[filePath]; }

	const robin_hood::unordered_map<std::string, u32>& GetFilePathToIDMap() const { return _filePathToID; }

private:
	void ParseListFile();

private:
	std::string _listPath = "";
	Bytebuffer* _fileBuffer = nullptr;

	robin_hood::unordered_map<u32, std::string> _fileIDToPath = { };
	robin_hood::unordered_map<std::string, u32> _filePathToID = { };
};