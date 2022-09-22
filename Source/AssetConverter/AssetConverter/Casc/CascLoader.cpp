#include "CascLoader.h"

#include <Base/Util/DebugHandler.h>
#include <Base/Util/StringUtils.h>

#include <glm/glm.hpp>
#include <filesystem>
namespace fs = std::filesystem;

bool CascLoader::_isLoadingIndexFiles = false;

CascLoader::Result CascLoader::Load()
{
    if (_storageHandle)
        return Result::AlreadyInitialized;

    fs::path currentPath = fs::current_path();
    std::string pathString = currentPath.string();

    CASC_OPEN_STORAGE_ARGS args = { };
    args.Size = sizeof(CASC_OPEN_STORAGE_ARGS);
    args.szLocalPath = pathString.c_str();
    args.szCodeName = "wow_classic";
    args.szRegion = "eu";
    args.dwLocaleMask = CASC_LOCALE_ENUS;
    args.PfnProgressCallback = LoadingCallback;

    if (!CascOpenStorageEx(nullptr, &args, false, &_storageHandle))
        return Result::MissingCasc;

    DebugHandler::PrintSuccess("[CascLoader] : Loading ListFile");
    if (!_listFile.Initialize())
        return Result::MissingListFile;

    return Result::Success;
}

void CascLoader::Close()
{
    if (!_storageHandle)
        return;

    CascCloseStorage(_storageHandle);
    _storageHandle = nullptr;
    _isLoadingIndexFiles = false;
}

std::shared_ptr<Bytebuffer> CascLoader::GetFileByID(u32 fileID)
{
    void* fileHandle = nullptr;
    if (!CascOpenFile(_storageHandle, CASC_FILE_DATA_ID(fileID), 0xFFFFFFFF, CASC_OPEN_BY_FILEID | CASC_OVERCOME_ENCRYPTED, &fileHandle))
        return nullptr;

    return GetFileByHandle(fileHandle);
}

std::shared_ptr<Bytebuffer> CascLoader::GetFilePartialByID(u32 fileID, u32 size)
{
    void* fileHandle = nullptr;
    if (!CascOpenFile(_storageHandle, CASC_FILE_DATA_ID(fileID), 0xFFFFFFFF, CASC_OPEN_BY_FILEID | CASC_OVERCOME_ENCRYPTED, &fileHandle))
        return nullptr;

    return GetFilePartialByHandle(fileHandle, size);
}

std::shared_ptr<Bytebuffer> CascLoader::GetFileByPath(std::string filePath)
{
    void* fileHandle = nullptr;
    if (!CascOpenFile(_storageHandle, filePath.c_str(), 0xFFFFFFFF, CASC_OPEN_BY_NAME | CASC_OVERCOME_ENCRYPTED, &fileHandle))
        return nullptr;

    return GetFileByHandle(fileHandle);
}

std::shared_ptr<Bytebuffer> CascLoader::GetFileByListFilePath(const std::string& filePath)
{
    u32 fileID = GetFileIDFromListFilePath(filePath);
    return GetFileByID(fileID);
}

bool CascLoader::FileExistsInCasc(u32 fileID)
{
    void* fileHandle = nullptr;
    if (!CascOpenFile(_storageHandle, CASC_FILE_DATA_ID(fileID), 0xFFFFFFFF, CASC_OPEN_BY_FILEID | CASC_OVERCOME_ENCRYPTED, &fileHandle))
        return false;

    DWORD fileSize = CascGetFileSize(fileHandle, nullptr);
    if (fileSize == CASC_INVALID_SIZE)
        return false;

    CascCloseFile(fileHandle);
    return true;
}

bool CascLoader::LoadingCallback(void* ptrUserParam, LPCSTR szWork, LPCSTR szObject, DWORD currentValue, DWORD totalValue)
{
    if (StringUtils::BeginsWith(szWork, "Loading index files"))
    {
        if (_isLoadingIndexFiles)
            return false;

        _isLoadingIndexFiles = true;
    }

    DebugHandler::PrintSuccess("[CascLoader] : %s", szWork);
    return false;
}

std::shared_ptr<Bytebuffer> CascLoader::GetFileByHandle(void* handle)
{
    DWORD fileSize = CascGetFileSize(handle, nullptr);
    if (fileSize == CASC_INVALID_SIZE)
        return nullptr;

    std::shared_ptr<Bytebuffer> buffer = Bytebuffer::BorrowRuntime(fileSize);
    if (!CascReadFile(handle, buffer->GetDataPointer(), fileSize, nullptr))
        return nullptr;

    CascCloseFile(handle);

    buffer->writtenData = fileSize;
    return buffer;
}

std::shared_ptr<Bytebuffer> CascLoader::GetFilePartialByHandle(void* handle, u32 size)
{
    DWORD fileSize = CascGetFileSize(handle, nullptr);
    if (fileSize == CASC_INVALID_SIZE)
        return nullptr;

    size_t calculatedSize = glm::min(static_cast<u32>(fileSize), size);
    std::shared_ptr<Bytebuffer> buffer = Bytebuffer::BorrowRuntime(calculatedSize);
    if (!CascReadFile(handle, buffer->GetDataPointer(), calculatedSize, nullptr))
        return nullptr;

    CascCloseFile(handle);

    buffer->writtenData = calculatedSize;
    return buffer;
}
