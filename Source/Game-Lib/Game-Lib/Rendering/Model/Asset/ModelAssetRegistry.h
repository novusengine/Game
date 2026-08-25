#pragma once
#include "Game-Lib/Rendering/Asset/RenderAssetHandles.h"
#include "ModelAssetReader.h"

#include <FileFormat/Novus/FileHeader.h>

#include <robinhood/robinhood.h>

#include <memory>

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
        Pending,
        Loaded,
        InvalidReference,
        // Reserved for an explicit converter-authored non-renderable dependency contract.
        // A missing runtime .model file is a failure and must use the visible fallback.
        MissingRenderableGeometry,
        Failed
    };

    enum class ModelLoadStatus : u8
    {
        Pending,
        Ready
    };

    struct ModelAssetRegistryStats
    {
        u32 residentModels = 0;
        u32 references = 0;
        u32 cacheHits = 0;
        u32 failures = 0;
        u32 pendingLoads = 0;
        u32 activeJobs = 0;
        u32 peakActiveJobs = 0;
        u32 peakPendingLoads = 0;
        u32 coalescedRequests = 0;
        u32 completedReads = 0;
        u32 committedLoads = 0;
        u32 cancelledLoads = 0;
        u64 decodedBytes = 0;
        u64 peakDecodedBytes = 0;
        u64 totalCommitTimeUS = 0;
        u64 maxCommitTimeUS = 0;
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
        ~ModelAssetRegistry();

        bool InitializeFallback();
        void Update();
        void Reserve(u32 modelCount)
        {
            _entries.reserve(_entries.size() + modelCount);
        }
        RenderAssets::ModelHandle Load(FileFormat::AssetID assetID);
        ModelLoadStatus BeginLoad(FileFormat::AssetID assetID, RenderAssets::ModelHandle& outHandle);
        ModelLoadStatus PollLoad(FileFormat::AssetID assetID, RenderAssets::ModelHandle& outHandle);
        bool Release(FileFormat::AssetID assetID);
        EmbeddedModelLoadStatus LoadEmbedded(FileFormat::AssetID assetID, bool acquireReference,
                                             RenderAssets::ModelHandle& outHandle);
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
        struct AsyncState;

        RenderAssets::ModelHandle RecordFailure(FileFormat::AssetID assetID, const char* reason,
                                                bool sourceFileMissing = false, u32 references = 1);
        void PumpCompletedLoads();
        void DispatchQueuedLoads();
        void CancelAndDrainLoads();
        bool CommitPendingLoad(FileFormat::AssetID assetID, RenderAssets::ModelHandle& outHandle);
        void WaitForPendingLoad(FileFormat::AssetID assetID);

        PACT::PactStorage* _pactStorage = nullptr;
        ModelGeometryStorage* _geometryStorage = nullptr;
        MaterialLoading::MaterialStorage* _materialStorage = nullptr;
        MaterialLoading::MaterialRegistry* _materialRegistry = nullptr;
        robin_hood::unordered_map<FileFormat::AssetID, Entry> _entries;
        std::unique_ptr<AsyncState> _async;
        RenderAssets::ModelHandle _fallbackModel;
        u32 _cacheHits = 0;
        u32 _failures = 0;
        u32 _releaseUnderflows = 0;
        ModelAssetLimitations _limitations;
    };
} // namespace ModelLoading
