#pragma once
#include "Game-Lib/Rendering/Asset/RenderAssetHandles.h"
#include "ModelAssetReader.h"

#include <FileFormat/Novus/FileHeader.h>

#include <robinhood/robinhood.h>

namespace PACT
{
    class PactStorage;
}

namespace MaterialLoading
{
    class MaterialRegistry;
    class MaterialStorage;
}

namespace ModelLoading
{
    class ModelGeometryStorage;

    enum class EmbeddedModelLoadStatus : u8
    {
        Loaded,
        InvalidReference,
        MissingRenderableGeometry,
        Failed
    };

    struct ModelAssetRegistryStats
    {
        u32 residentModels = 0;
        u32 references = 0;
        u32 cacheHits = 0;
        u32 failures = 0;
        ModelAssetLimitations limitations;
    };

    // Maintains a CPU-side AssetID cache that loads model dependencies and allocates handles in GPU-backed geometry storage.
    // It avoids duplicate loads while resolving model dependencies to stable runtime handles.
    class ModelAssetRegistry
    {
      public:
        ModelAssetRegistry(PACT::PactStorage* pactStorage, ModelGeometryStorage* geometryStorage,
                           MaterialLoading::MaterialStorage* materialStorage,
                           MaterialLoading::MaterialRegistry* materialRegistry);

        bool InitializeFallback();
        void Reserve(u32 modelCount)
        {
            _entries.reserve(_entries.size() + modelCount);
            _missingEmbeddedModels.reserve(_missingEmbeddedModels.size() + modelCount);
        }
        RenderAssets::ModelHandle Load(FileFormat::AssetID assetID);
        EmbeddedModelLoadStatus LoadEmbedded(FileFormat::AssetID assetID, RenderAssets::ModelHandle& outHandle);
        RenderAssets::ModelHandle GetFallbackModel() const { return _fallbackModel; }
        ModelAssetRegistryStats GetStats() const;

      private:
        struct Entry
        {
            RenderAssets::ModelHandle handle;
            u32 referenceCount = 0;
            bool usedFallback = false;
            bool sourceFileMissing = false;
        };

        RenderAssets::ModelHandle RecordFailure(FileFormat::AssetID assetID, const char* reason,
                                                bool sourceFileMissing = false);
        RenderAssets::ModelHandle LoadPayload(FileFormat::AssetID assetID, std::span<const u8> payload);

        PACT::PactStorage* _pactStorage = nullptr;
        ModelGeometryStorage* _geometryStorage = nullptr;
        MaterialLoading::MaterialStorage* _materialStorage = nullptr;
        MaterialLoading::MaterialRegistry* _materialRegistry = nullptr;
        robin_hood::unordered_map<FileFormat::AssetID, Entry> _entries;
        robin_hood::unordered_set<FileFormat::AssetID> _missingEmbeddedModels;
        RenderAssets::ModelHandle _fallbackModel;
        u32 _cacheHits = 0;
        u32 _failures = 0;
        ModelAssetLimitations _limitations;
    };
} // namespace ModelLoading
