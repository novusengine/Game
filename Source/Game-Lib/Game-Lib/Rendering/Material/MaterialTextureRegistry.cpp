#include "MaterialTextureRegistry.h"

#include "Game-Lib/Rendering/Asset/AssetValidation.h"

#include <Base/Util/DebugHandler.h>

#include <Filesystem/PactStorage.h>

#include <Renderer/RenderSettings.h>
#include <Renderer/Renderer.h>
#include <Renderer/Descriptors/TextureDesc.h>

#include <tracy/Tracy.hpp>

#include <array>

namespace
{
    constexpr u32 CHECKER_TILE_COUNT = 4;
    constexpr u32 CHECKER_TILE_SIZE = 16;
    constexpr u32 CHECKER_SIZE = CHECKER_TILE_COUNT * CHECKER_TILE_SIZE;

    constexpr std::array<u8, CHECKER_SIZE * CHECKER_SIZE * 4> MakeCheckerboard()
    {
        std::array<u8, CHECKER_SIZE * CHECKER_SIZE * 4> pixels = {};
        for (u32 y = 0; y < CHECKER_SIZE; ++y)
        {
            for (u32 x = 0; x < CHECKER_SIZE; ++x)
            {
                const bool purple = ((x / CHECKER_TILE_SIZE) + (y / CHECKER_TILE_SIZE)) % 2u == 0;
                const u32 offset = (y * CHECKER_SIZE + x) * 4u;
                pixels[offset + 0] = purple ? 190 : 255;
                pixels[offset + 1] = purple ? 0 : 255;
                pixels[offset + 2] = purple ? 255 : 255;
                pixels[offset + 3] = 255;
            }
        }
        return pixels;
    }

    constexpr auto CHECKERBOARD = MakeCheckerboard();

    const char* ToReason(PACT::PactReadResult result)
    {
        switch (result)
        {
        case PACT::PactReadResult::FileNotFound: return "file_not_found";
        case PACT::PactReadResult::FileAccessFailed: return "file_access_failed";
        case PACT::PactReadResult::FileReadFailed: return "file_read_failed";
        case PACT::PactReadResult::GenerationMismatch: return "generation_mismatch";
        case PACT::PactReadResult::Pending: return "unexpected_pending_read";
        default: return "pact_read_failed";
        }
    }
} // namespace

namespace MaterialLoading
{
    MaterialTextureRegistry::MaterialTextureRegistry(Renderer::Renderer* renderer, PACT::PactStorage* pactStorage)
        : _renderer(renderer), _pactStorage(pactStorage)
    {
    }

    bool MaterialTextureRegistry::Initialize()
    {
        Renderer::TextureArrayDesc arrayDesc;
        arrayDesc.size = Renderer::Settings::MAX_TEXTURES;
        _textureArray = _renderer->CreateTextureArray(arrayDesc);

        Renderer::DataTextureDesc textureDesc;
        textureDesc.width = CHECKER_SIZE;
        textureDesc.height = CHECKER_SIZE;
        textureDesc.format = Renderer::ImageFormat::R8G8B8A8_UNORM_SRGB;
        textureDesc.data = CHECKERBOARD.data();
        textureDesc.size = CHECKERBOARD.size();
        textureDesc.debugName = "Model Fallback Checkerboard";

        const Renderer::TextureID texture = _renderer->CreateDataTextureIntoArray(textureDesc, _textureArray, _fallbackTextureIndex);
        if (texture == Renderer::TextureID::Invalid())
        {
            NC_LOG_CRITICAL("MODEL_ASSET fallback_initialization_failed resource=texture reason=create_data_texture_failed");
            return false;
        }

        _descriptorsDirty = true;
        return true;
    }

    u32 MaterialTextureRegistry::Resolve(FileFormat::AssetID textureAssetID, FileFormat::AssetID ownerAssetID, bool optional)
    {
        ZoneScopedN("MaterialTextureRegistry::Resolve");

        const auto existing = _entries.find(textureAssetID);
        if (existing != _entries.end())
        {
            ++_stats.cacheHits;
            return existing->second.arrayIndex;
        }

        if (textureAssetID == FileFormat::INVALID_ASSET_ID)
            return RecordFailure(textureAssetID, ownerAssetID, "invalid_asset_id", optional);
        if (AssetLoading::ShouldInjectFailure(AssetLoading::FailureInjection::Texture))
            return RecordFailure(textureAssetID, ownerAssetID, "injected_failure", optional);

        PACT::PactFileHandle file;
        PACT::PactReadResult readResult;
        {
            ZoneScopedN("Read Texture From PACT");
            readResult = _pactStorage->ReadFile(textureAssetID, file);
        }
        if (readResult != PACT::PactReadResult::Success)
            return RecordFailure(textureAssetID, ownerAssetID, ToReason(readResult), optional);

        Renderer::DataTextureDesc textureDesc;
        textureDesc.hash = textureAssetID;
        textureDesc.data = reinterpret_cast<const u8*>(file.GetData());
        textureDesc.size = file.GetSize();
        textureDesc.debugName = "Model Texture";

        u32 arrayIndex = _fallbackTextureIndex;
        Renderer::TextureID texture;
        {
            ZoneScopedN("Decode And Create Texture");
            texture = _renderer->LoadDataTextureIntoArray(textureDesc, _textureArray, arrayIndex);
        }
        if (texture == Renderer::TextureID::Invalid())
            return RecordFailure(textureAssetID, ownerAssetID, "texture_decode_failed", optional);

        _entries[textureAssetID] = {arrayIndex, false};
        ++_stats.resolvedTextures;
        _descriptorsDirty = true;
        return arrayIndex;
    }

    void MaterialTextureRegistry::FlushDescriptors()
    {
        if (!_descriptorsDirty)
            return;

        _renderer->FlushTextureArrayDescriptors(_textureArray);
        _descriptorsDirty = false;
    }

    u32 MaterialTextureRegistry::RecordFailure(FileFormat::AssetID textureAssetID, FileFormat::AssetID ownerAssetID,
                                               const char* reason, bool optional)
    {
        NC_LOG_ERROR("MODEL_ASSET texture_fallback owner={} dependency={} optional={} reason={}", ownerAssetID, textureAssetID, optional, reason);
        _entries[textureAssetID] = {_fallbackTextureIndex, true};
        ++_stats.fallbackTextures;
        return _fallbackTextureIndex;
    }
} // namespace MaterialLoading
