#pragma once

#include <Base/Types.h>

#include <Filesystem/PactStorage.h>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class Bytebuffer;

namespace Util
{
    enum class AssetWriteTarget : u8
    {
        Auto,
        Disk,
        PactOverlay
    };

    struct AssetWriterConfig
    {
    public:
        std::filesystem::path diskRoot;
        std::filesystem::path pactOverlayRoot;
        PACT::PactStorage* pactStorage = nullptr;
        PACT::PactManifestHandle pactOverlayHandle = PACT::MANIFEST_INVALID_ID;
    };

    class AssetWriter
    {
    public:
        bool Init(const AssetWriterConfig& config);

        bool WriteBytes(std::string_view virtualPath, const void* data, size_t size, AssetWriteTarget target = AssetWriteTarget::Auto);
        bool WriteBytes(std::string_view virtualPath, const std::vector<u8>& data, AssetWriteTarget target = AssetWriteTarget::Auto);
        bool WriteBytes(std::string_view virtualPath, Bytebuffer& buffer, AssetWriteTarget target = AssetWriteTarget::Auto);

        bool Delete(std::string_view virtualPath, AssetWriteTarget target = AssetWriteTarget::Auto);

        bool ReloadPactOverlay();
        bool ResolvePath(std::string_view virtualPath, AssetWriteTarget target, std::filesystem::path& outPath) const;

    private:
        bool NormalizeVirtualPath(std::string_view virtualPath, std::string& outPath) const;
        AssetWriteTarget ResolveTarget(const std::string& normalizedPath, AssetWriteTarget requestedTarget) const;

    private:
        std::filesystem::path _diskRoot;
        std::filesystem::path _pactOverlayRoot;
        PACT::PactStorage* _pactStorage = nullptr;
        PACT::PactManifestHandle _pactOverlayHandle = PACT::MANIFEST_INVALID_ID;
    };
}
