#include "AssetWriter.h"

#include <Base/Memory/Bytebuffer.h>
#include <Base/Util/DebugHandler.h>

#include <algorithm>
#include <fstream>
#include <system_error>
#include <utility>

namespace fs = std::filesystem;

namespace Util
{
    bool AssetWriter::Init(const AssetWriterConfig& config)
    {
        _diskRoot = fs::absolute(config.diskRoot).make_preferred();
        _pactOverlayRoot = fs::absolute(config.pactOverlayRoot).make_preferred();
        _pactStorage = config.pactStorage;
        _pactOverlayHandle = config.pactOverlayHandle;

        std::error_code errorCode;
        fs::create_directories(_diskRoot, errorCode);
        if (errorCode)
        {
            NC_LOG_ERROR("AssetWriter : Failed to create Disk Root (\"{0}\")", _diskRoot.string());
            return false;
        }

        fs::create_directories(_pactOverlayRoot, errorCode);
        if (errorCode)
        {
            NC_LOG_ERROR("AssetWriter : Failed to create PACT Overlay Root (\"{0}\")", _pactOverlayRoot.string());
            return false;
        }

        return true;
    }

    bool AssetWriter::WriteBytes(std::string_view virtualPath, const void* data, size_t size, AssetWriteTarget target)
    {
        if (data == nullptr && size > 0)
            return false;

        std::string normalizedPath;
        if (!NormalizeVirtualPath(virtualPath, normalizedPath))
            return false;

        target = ResolveTarget(normalizedPath, target);

        fs::path writePath;
        if (!ResolvePath(normalizedPath, target, writePath))
            return false;

        std::error_code errorCode;
        fs::create_directories(writePath.parent_path(), errorCode);
        if (errorCode)
        {
            NC_LOG_ERROR("AssetWriter : Failed to create output directory for \"{0}\"", writePath.string());
            return false;
        }

        std::ofstream file(writePath, std::ios::binary | std::ios::trunc);
        if (!file)
        {
            NC_LOG_ERROR("AssetWriter : Failed to open \"{0}\" for writing", writePath.string());
            return false;
        }

        file.write(static_cast<const char*>(data), size);
        if (!file)
        {
            NC_LOG_ERROR("AssetWriter : Failed to write \"{0}\"", writePath.string());
            return false;
        }

        file.close();

        if (target == AssetWriteTarget::PactOverlay && !ReloadPactOverlay())
            return false;

        return true;
    }

    bool AssetWriter::WriteBytes(std::string_view virtualPath, const std::vector<u8>& data, AssetWriteTarget target)
    {
        return WriteBytes(virtualPath, data.data(), data.size(), target);
    }

    bool AssetWriter::WriteBytes(std::string_view virtualPath, Bytebuffer& buffer, AssetWriteTarget target)
    {
        return WriteBytes(virtualPath, buffer.GetDataPointer(), buffer.writtenData, target);
    }

    bool AssetWriter::Delete(std::string_view virtualPath, AssetWriteTarget target)
    {
        std::string normalizedPath;
        if (!NormalizeVirtualPath(virtualPath, normalizedPath))
            return false;

        target = ResolveTarget(normalizedPath, target);

        fs::path deletePath;
        if (!ResolvePath(normalizedPath, target, deletePath))
            return false;

        std::error_code errorCode;
        bool removed = fs::remove(deletePath, errorCode);
        if (errorCode)
        {
            NC_LOG_ERROR("AssetWriter : Failed to delete \"{0}\"", deletePath.string());
            return false;
        }

        if (target == AssetWriteTarget::PactOverlay && !ReloadPactOverlay())
            return false;

        return removed;
    }

    bool AssetWriter::ResolvePath(std::string_view virtualPath, AssetWriteTarget target, fs::path& outPath) const
    {
        std::string normalizedPath;
        if (!NormalizeVirtualPath(virtualPath, normalizedPath))
            return false;

        if (target == AssetWriteTarget::Auto)
            target = ResolveTarget(normalizedPath, target);

        switch (target)
        {
            case AssetWriteTarget::Disk:
            {
                outPath = (_diskRoot / fs::path(normalizedPath)).make_preferred();
                return true;
            }
            case AssetWriteTarget::PactOverlay:
            {
                outPath = (_pactOverlayRoot / fs::path(normalizedPath)).make_preferred();
                return true;
            }
            case AssetWriteTarget::Auto:
            default:
            {
                return false;
            }
        }
    }

    bool AssetWriter::NormalizeVirtualPath(std::string_view virtualPath, std::string& outPath) const
    {
        outPath.assign(virtualPath);
        std::replace(outPath.begin(), outPath.end(), '\\', '/');

        while (!outPath.empty() && outPath.front() == '/')
            outPath.erase(outPath.begin());

        if (outPath.empty() || outPath.find(':') != std::string::npos)
            return false;

        std::string normalized;
        size_t start = 0;
        while (start <= outPath.size())
        {
            size_t end = outPath.find('/', start);
            if (end == std::string::npos)
                end = outPath.size();

            std::string_view part(outPath.data() + start, end - start);
            if (part == "..")
                return false;

            if (!part.empty() && part != ".")
            {
                if (!normalized.empty())
                    normalized += '/';

                normalized.append(part);
            }

            if (end == outPath.size())
                break;

            start = end + 1;
        }

        if (normalized.empty())
            return false;

        outPath = std::move(normalized);
        return true;
    }

    AssetWriteTarget AssetWriter::ResolveTarget(const std::string& normalizedPath, AssetWriteTarget requestedTarget) const
    {
        if (requestedTarget != AssetWriteTarget::Auto)
            return requestedTarget;

        if (_pactStorage != nullptr && _pactStorage->FileExists(normalizedPath))
            return AssetWriteTarget::PactOverlay;

        return AssetWriteTarget::Disk;
    }

    bool AssetWriter::ReloadPactOverlay()
    {
        if (_pactStorage == nullptr || _pactOverlayHandle == PACT::MANIFEST_INVALID_ID)
            return false;

        if (!_pactStorage->ReloadOverlay(_pactOverlayHandle))
        {
            NC_LOG_ERROR("AssetWriter : Failed to reload PACT Overlay after write");
            return false;
        }

        return true;
    }
}
