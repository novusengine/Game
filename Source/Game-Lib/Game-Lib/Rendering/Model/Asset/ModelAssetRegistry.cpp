#include "ModelAssetRegistry.h"

#include "FallbackModel.h"
#include "ModelGeometryStorage.h"

#include "Game-Lib/Rendering/Asset/AssetDiagnostic.h"
#include "Game-Lib/Rendering/Asset/AssetValidation.h"
#include "Game-Lib/Rendering/Material/MaterialRegistry.h"
#include "Game-Lib/Rendering/Material/MaterialStorage.h"

#include <Base/Util/DebugHandler.h>

#include <Filesystem/PactStorage.h>

#include <array>
#include <chrono>
#include <future>
#include <string>

namespace
{
    const char* ToReason(PACT::PactReadResult result)
    {
        switch (result)
        {
        case PACT::PactReadResult::FileNotFound:
            return "file_not_found";
        case PACT::PactReadResult::FileAccessFailed:
            return "file_access_failed";
        case PACT::PactReadResult::FileReadFailed:
            return "file_read_failed";
        case PACT::PactReadResult::GenerationMismatch:
            return "generation_mismatch";
        case PACT::PactReadResult::Pending:
            return "unexpected_pending_read";
        default:
            return "pact_read_failed";
        }
    }
} // namespace

namespace ModelLoading
{
    struct ModelAssetRegistry::PendingLoad
    {
        struct Result
        {
            PACT::PactReadResult readResult = PACT::PactReadResult::Failed;
            PACT::PactFileHandle file;
            ModelAssetReadResult decoded;
        };

        std::future<Result> future;
        u32 referenceCount = 1;
    };

    ModelAssetRegistry::ModelAssetRegistry(PACT::PactStorage* pactStorage, ModelGeometryStorage* geometryStorage,
                                           MaterialLoading::MaterialStorage* materialStorage,
                                           MaterialLoading::MaterialRegistry* materialRegistry)
        : _pactStorage(pactStorage), _geometryStorage(geometryStorage), _materialStorage(materialStorage),
          _materialRegistry(materialRegistry)
    {
    }

    ModelAssetRegistry::~ModelAssetRegistry() = default;

    ModelLoadStatus ModelAssetRegistry::BeginLoad(FileFormat::AssetID assetID, RenderAssets::ModelHandle& outHandle)
    {
        const auto resident = _entries.find(assetID);
        if (resident != _entries.end())
        {
            ++resident->second.referenceCount;
            ++_cacheHits;
            outHandle = resident->second.handle;
            return ModelLoadStatus::Ready;
        }

        const auto pending = _pendingLoads.find(assetID);
        if (pending != _pendingLoads.end())
        {
            ++pending->second->referenceCount;
            return PollLoad(assetID, outHandle);
        }

        if (assetID == FileFormat::INVALID_ASSET_ID || AssetLoading::ShouldInjectFailure(AssetLoading::FailureInjection::Model))
        {
            outHandle = RecordFailure(assetID, assetID == FileFormat::INVALID_ASSET_ID ? "invalid_asset_id" : "injected_failure");
            return ModelLoadStatus::Ready;
        }

        auto load = std::make_unique<PendingLoad>();
        load->future = std::async(std::launch::async, [storage = _pactStorage, assetID]() mutable {
            PendingLoad::Result result;
            result.readResult = storage->ReadFile(assetID, result.file);
            if (result.readResult == PACT::PactReadResult::Success)
            {
                const std::span<const u8> payload(reinterpret_cast<const u8*>(result.file.GetData()), result.file.GetSize());
                result.decoded = ModelAssetReader::Read(payload);
            }
            return result;
        });
        _pendingLoads.emplace(assetID, std::move(load));
        outHandle = RenderAssets::ModelHandle::Invalid();
        return ModelLoadStatus::Pending;
    }

    ModelLoadStatus ModelAssetRegistry::PollLoad(FileFormat::AssetID assetID, RenderAssets::ModelHandle& outHandle)
    {
        const auto resident = _entries.find(assetID);
        if (resident != _entries.end())
        {
            outHandle = resident->second.handle;
            return ModelLoadStatus::Ready;
        }

        const auto pending = _pendingLoads.find(assetID);
        if (pending == _pendingLoads.end())
        {
            outHandle = _fallbackModel;
            return ModelLoadStatus::Ready;
        }
        if (pending->second->future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return ModelLoadStatus::Pending;

        const u32 references = pending->second->referenceCount;
        PendingLoad::Result result = pending->second->future.get();
        _pendingLoads.erase(pending);
        if (result.readResult != PACT::PactReadResult::Success)
        {
            outHandle = RecordFailure(assetID, ToReason(result.readResult), result.readResult == PACT::PactReadResult::FileNotFound);
        }
        else if (!result.decoded)
        {
            const std::string reason = AssetLoading::Describe(result.decoded.diagnostic);
            outHandle = RecordFailure(assetID, reason.c_str());
        }
        else
        {
            u32 materialTableOffset = 0;
            if (!_materialRegistry->AppendDefaultMaterialTable(result.decoded.view.materialSlots, materialTableOffset) ||
                !_geometryStorage->Append(result.decoded.view, materialTableOffset, static_cast<u32>(result.decoded.view.materialSlots.size()), outHandle))
            {
                outHandle = RecordFailure(assetID, "commit_failed");
            }
            else
            {
                _limitations.invalidSkeletonReferences += result.decoded.limitations.invalidSkeletonReferences;
                _limitations.invalidAnimationBoundsReferences += result.decoded.limitations.invalidAnimationBoundsReferences;
                _limitations.invalidCollisionReferences += result.decoded.limitations.invalidCollisionReferences;
                _limitations.invalidEmbeddedModelReferences += result.decoded.limitations.invalidEmbeddedModelReferences;
                _entries.emplace(assetID, Entry{outHandle, references, false, false});
                return ModelLoadStatus::Ready;
            }
        }
        _entries[assetID].referenceCount = references;
        return ModelLoadStatus::Ready;
    }

    bool ModelAssetRegistry::InitializeFallback()
    {
        const std::array materials = {_materialStorage->GetFallbackMaterialInstance()};
        u32 materialTableOffset = 0;
        if (!_materialStorage->AppendMaterialTable(materials, materialTableOffset))
            return false;

        const ModelAssetView fallback = GetFallbackModelAssetView();
        if (!_geometryStorage->Append(fallback, materialTableOffset, static_cast<u32>(materials.size()),
                                      _fallbackModel))
            return false;

        return static_cast<RenderAssets::ModelHandle::type>(_fallbackModel) == 0;
    }

    RenderAssets::ModelHandle ModelAssetRegistry::Load(FileFormat::AssetID assetID)
    {
        ZoneScopedN("ModelAssetRegistry::Load");

        const auto existing = _entries.find(assetID);
        if (existing != _entries.end())
        {
            ++existing->second.referenceCount;
            ++_cacheHits;
            return existing->second.handle;
        }

        if (assetID == FileFormat::INVALID_ASSET_ID)
            return RecordFailure(assetID, "invalid_asset_id");
        if (AssetLoading::ShouldInjectFailure(AssetLoading::FailureInjection::Model))
            return RecordFailure(assetID, "injected_failure");

        PACT::PactFileHandle file;
        PACT::PactReadResult readResult;
        {
            ZoneScopedN("Read Model From PACT");
            readResult = _pactStorage->ReadFile(assetID, file);
        }
        if (readResult != PACT::PactReadResult::Success)
            return RecordFailure(assetID, ToReason(readResult), readResult == PACT::PactReadResult::FileNotFound);

        const std::span<const u8> payload(reinterpret_cast<const u8*>(file.GetData()), file.GetSize());
        return LoadPayload(assetID, payload);
    }

    bool ModelAssetRegistry::Release(FileFormat::AssetID assetID)
    {
        const auto existing = _entries.find(assetID);
        if (existing == _entries.end() || existing->second.referenceCount == 0)
        {
            ++_releaseUnderflows;
            NC_LOG_ERROR("MODEL_ASSET release_rejected asset={} reason=not_acquired", assetID);
            return false;
        }

        --existing->second.referenceCount;
        // Residency and stable geometry handles are intentionally retained until eviction is implemented.
        return true;
    }

    EmbeddedModelLoadStatus ModelAssetRegistry::LoadEmbedded(FileFormat::AssetID assetID,
                                                             RenderAssets::ModelHandle& outHandle)
    {
        ZoneScopedN("ModelAssetRegistry::LoadEmbedded");

        outHandle = RenderAssets::ModelHandle::Invalid();
        if (assetID == FileFormat::INVALID_ASSET_ID)
            return EmbeddedModelLoadStatus::InvalidReference;

        const auto existing = _entries.find(assetID);
        if (existing != _entries.end())
        {
            ++_cacheHits;
            ++existing->second.referenceCount;
            outHandle = existing->second.handle;
            return existing->second.usedFallback ? EmbeddedModelLoadStatus::Failed : EmbeddedModelLoadStatus::Loaded;
        }

        const ModelLoadStatus loadStatus = _pendingLoads.contains(assetID)
            ? PollLoad(assetID, outHandle)
            : BeginLoad(assetID, outHandle);
        if (loadStatus == ModelLoadStatus::Pending)
            return EmbeddedModelLoadStatus::Pending;
        const auto loaded = _entries.find(assetID);
        return loaded != _entries.end() && !loaded->second.usedFallback
            ? EmbeddedModelLoadStatus::Loaded
            : EmbeddedModelLoadStatus::Failed;
    }

    RenderAssets::ModelHandle ModelAssetRegistry::LoadPayload(FileFormat::AssetID assetID, std::span<const u8> payload)
    {
        ZoneScopedN("ModelAssetRegistry::LoadPayload");
        ModelAssetReadResult result;
        {
            ZoneScopedN("Decode Model Asset");
            result = ModelAssetReader::Read(payload);
        }
        if (!result)
        {
            const std::string reason = AssetLoading::Describe(result.diagnostic);
            return RecordFailure(assetID, reason.c_str());
        }

        u32 materialTableOffset = 0;
        {
            ZoneScopedN("Resolve Model Material Table");
            if (!_materialRegistry->AppendDefaultMaterialTable(result.view.materialSlots, materialTableOffset))
                return RecordFailure(assetID, "material_table_append_failed");
        }

        RenderAssets::ModelHandle handle;
        {
            ZoneScopedN("Append Model Geometry");
            if (!_geometryStorage->Append(result.view, materialTableOffset,
                                          static_cast<u32>(result.view.materialSlots.size()), handle))
                return RecordFailure(assetID, "geometry_storage_append_failed");
        }

        _limitations.invalidSkeletonReferences += result.limitations.invalidSkeletonReferences;
        _limitations.invalidAnimationBoundsReferences += result.limitations.invalidAnimationBoundsReferences;
        _limitations.invalidCollisionReferences += result.limitations.invalidCollisionReferences;
        _limitations.invalidEmbeddedModelReferences += result.limitations.invalidEmbeddedModelReferences;
        _entries.emplace(assetID, Entry{handle, 1, false, false});
        return handle;
    }

    ModelAssetRegistryStats ModelAssetRegistry::GetStats() const
    {
        ModelAssetRegistryStats stats;
        stats.residentModels = static_cast<u32>(_entries.size()) + 1;
        for (const auto& [assetID, entry] : _entries)
            stats.references += entry.referenceCount;
        stats.cacheHits = _cacheHits;
        stats.failures = _failures;
        stats.limitations = _limitations;
        return stats;
    }

    RenderAssets::ModelHandle ModelAssetRegistry::RecordFailure(FileFormat::AssetID assetID, const char* reason,
                                                                bool sourceFileMissing)
    {
        NC_LOG_ERROR("MODEL_ASSET model_fallback asset={} reason={}", assetID, reason);
        _entries.emplace(assetID, Entry{_fallbackModel, 1, true, sourceFileMissing});
        ++_failures;
        return _fallbackModel;
    }
} // namespace ModelLoading
