#pragma once

#include <FileFormat/Novus/FileHeader.h>

#include <Renderer/Descriptors/TextureArrayDesc.h>
#include <Renderer/Descriptors/TextureDesc.h>

#include <robinhood/robinhood.h>

namespace PACT
{
    class PactStorage;
}

namespace Renderer
{
    class Renderer;
}

namespace MaterialLoading
{
    struct MaterialTextureRegistryStats
    {
        u32 resolvedTextures = 0;
        u32 fallbackTextures = 0;
        u32 cacheHits = 0;
    };

    // Maintains a CPU-side AssetID cache and populates the GPU-side bindless material texture array.
    // It converts portable texture AssetIDs into the runtime bindless indices required by shaders.
    class MaterialTextureRegistry
    {
      public:
        MaterialTextureRegistry(Renderer::Renderer* renderer, PACT::PactStorage* pactStorage);

        bool Initialize();
        u32 Resolve(FileFormat::AssetID textureAssetID, FileFormat::AssetID ownerAssetID, bool optional);
        u32 Resolve(Renderer::TextureID textureID);
        void FlushDescriptors();

        Renderer::TextureArrayID GetTextureArray() const { return _textureArray; }
        u32 GetFallbackTextureIndex() const { return _fallbackTextureIndex; }
        MaterialTextureRegistryStats GetStats() const { return _stats; }

      private:
        struct Entry
        {
            u32 arrayIndex = 0;
            bool usedFallback = false;
        };

        u32 RecordFailure(FileFormat::AssetID textureAssetID, FileFormat::AssetID ownerAssetID, const char* reason, bool optional);

        Renderer::Renderer* _renderer = nullptr;
        PACT::PactStorage* _pactStorage = nullptr;
        Renderer::TextureArrayID _textureArray;
        u32 _fallbackTextureIndex = 0;
        bool _descriptorsDirty = false;
        robin_hood::unordered_map<FileFormat::AssetID, Entry> _entries;
        MaterialTextureRegistryStats _stats;
    };
} // namespace MaterialLoading
