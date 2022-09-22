#pragma once
#include "CascListFile.h"

#include <Base/Types.h>
#include <Base/Memory/Bytebuffer.h>

#include <CascLib.h>

class CascLoader
{
public:
	enum class Result
	{
		Success,
		AlreadyInitialized,
		MissingCasc,
		MissingListFile
	};

public:
	CascLoader(std::string listPath) : _listFile(listPath) { }
	~CascLoader() { }

	CascLoader::Result Load();
	void Close();

	std::shared_ptr<Bytebuffer> GetFileByID(u32 fileID);
	std::shared_ptr<Bytebuffer> GetFilePartialByID(u32 fileID, u32 size);
	std::shared_ptr<Bytebuffer> GetFileByPath(std::string filePath);
	std::shared_ptr<Bytebuffer> GetFileByListFilePath(const std::string& filePath);
	bool FileExistsInCasc(u32 fileID);

	bool ListFileContainsID(u32 fileID) { return _listFile.HasFileWithID(fileID); }
	const std::string& GetFilePathFromListFileID(u32 fileID)
	{
		return _listFile.GetFilePathFromID(fileID);
	}

	bool ListFileContainsPath(const std::string& filePath) { return _listFile.HasFileWithPath(filePath); }
	u32 GetFileIDFromListFilePath(const std::string& filePath)
	{
		if (!ListFileContainsPath(filePath))
			return 0;

		return _listFile.GetFileIDFromPath(filePath);
	}

	const CascListFile& GetListFile() { return _listFile; }

private:
	static bool LoadingCallback(void* ptrUserParam, LPCSTR szWork, LPCSTR szObject, DWORD currentValue, DWORD totalValue);
	std::shared_ptr<Bytebuffer> GetFileByHandle(void* handle);
	std::shared_ptr<Bytebuffer> GetFilePartialByHandle(void* handle, u32 size);

private:
	void* _storageHandle = nullptr;
	CascListFile _listFile;
	static bool _isLoadingIndexFiles;
};