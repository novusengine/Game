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
    ModelAssetRegistry::ModelAssetRegistry(PACT::PactStorage* pactStorage, ModelGeometryStorage* geometryStorage,
                                           MaterialLoading::MaterialStorage* materialStorage,
                                           MaterialLoading::MaterialRegistry* materialRegistry)
        : _pactStorage(pactStorage), _geometryStorage(geometryStorage), _materialStorage(materialStorage),
          _materialRegistry(materialRegistry)
    {
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

        if (_missingEmbeddedModels.erase(assetID) != 0)
            return RecordFailure(assetID, "file_not_found", true);

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

    EmbeddedModelLoadStatus ModelAssetRegistry::LoadEmbedded(FileFormat::AssetID assetID,
                                                             RenderAssets::ModelHandle& outHandle)
    {
        ZoneScopedN("ModelAssetRegistry::LoadEmbedded");

        outHandle = RenderAssets::ModelHandle::Invalid();
        const auto existing = _entries.find(assetID);
        if (existing != _entries.end())
        {
            ++_cacheHits;
            if (existing->second.sourceFileMissing)
                return EmbeddedModelLoadStatus::MissingRenderableGeometry;
            ++existing->second.referenceCount;
            outHandle = existing->second.handle;
            return existing->second.usedFallback ? EmbeddedModelLoadStatus::Failed : EmbeddedModelLoadStatus::Loaded;
        }

        if (assetID == FileFormat::INVALID_ASSET_ID)
            return EmbeddedModelLoadStatus::InvalidReference;
        if (_missingEmbeddedModels.contains(assetID))
        {
            ++_cacheHits;
            return EmbeddedModelLoadStatus::MissingRenderableGeometry;
        }
        if (AssetLoading::ShouldInjectFailure(AssetLoading::FailureInjection::Model))
        {
            outHandle = RecordFailure(assetID, "injected_failure");
            return EmbeddedModelLoadStatus::Failed;
        }

        PACT::PactFileHandle file;
        PACT::PactReadResult readResult;
        {
            ZoneScopedN("Read Embedded Model From PACT");
            readResult = _pactStorage->ReadFile(assetID, file);
        }
        if (readResult == PACT::PactReadResult::FileNotFound)
        {
            _missingEmbeddedModels.insert(assetID);
            return EmbeddedModelLoadStatus::MissingRenderableGeometry;
        }
        if (readResult != PACT::PactReadResult::Success)
        {
            outHandle = RecordFailure(assetID, ToReason(readResult));
            return EmbeddedModelLoadStatus::Failed;
        }

        const std::span<const u8> payload(reinterpret_cast<const u8*>(file.GetData()), file.GetSize());
        outHandle = LoadPayload(assetID, payload);
        return outHandle == _fallbackModel ? EmbeddedModelLoadStatus::Failed : EmbeddedModelLoadStatus::Loaded;
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
