#include "ModelAssetRegistry.h"

#include "FallbackModel.h"
#include "ModelGeometryStorage.h"

#include "Game-Lib/Rendering/Asset/AssetDiagnostic.h"
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
        case PACT::PactReadResult::FileNotFound: return "file_not_found";
        case PACT::PactReadResult::FileAccessFailed: return "file_access_failed";
        case PACT::PactReadResult::FileReadFailed: return "file_read_failed";
        case PACT::PactReadResult::GenerationMismatch: return "generation_mismatch";
        case PACT::PactReadResult::Pending: return "unexpected_pending_read";
        default: return "pact_read_failed";
        }
    }
} // namespace

namespace ModelLoading
{
    ModelAssetRegistry::ModelAssetRegistry(PACT::PactStorage* pactStorage, ModelGeometryStorage* geometryStorage,
                                           MaterialLoading::MaterialStorage* materialStorage,
                                           MaterialLoading::MaterialRegistry* materialRegistry)
        : _pactStorage(pactStorage), _geometryStorage(geometryStorage), _materialStorage(materialStorage), _materialRegistry(materialRegistry)
    {
    }

    bool ModelAssetRegistry::InitializeFallback()
    {
        const std::array materials = {_materialStorage->GetFallbackMaterialInstance()};
        u32 materialTableOffset = 0;
        if (!_materialStorage->AppendMaterialTable(materials, materialTableOffset))
            return false;

        const ModelAssetView fallback = GetFallbackModelAssetView();
        if (!_geometryStorage->Append(fallback, materialTableOffset, static_cast<u32>(materials.size()), _fallbackModel))
            return false;

        return static_cast<RenderAssets::ModelHandle::type>(_fallbackModel) == 0;
    }

    RenderAssets::ModelHandle ModelAssetRegistry::Load(FileFormat::AssetID assetID)
    {
        const auto existing = _entries.find(assetID);
        if (existing != _entries.end())
        {
            ++existing->second.referenceCount;
            ++_cacheHits;
            return existing->second.handle;
        }

        if (assetID == FileFormat::INVALID_ASSET_ID)
            return RecordFailure(assetID, "invalid_asset_id");

        PACT::PactFileHandle file;
        const PACT::PactReadResult readResult = _pactStorage->ReadFile(assetID, file);
        if (readResult != PACT::PactReadResult::Success)
            return RecordFailure(assetID, ToReason(readResult));

        const std::span<const u8> payload(reinterpret_cast<const u8*>(file.GetData()), file.GetSize());
        const ModelAssetReadResult result = ModelAssetReader::Read(payload);
        if (!result)
        {
            const std::string reason = AssetLoading::Describe(result.diagnostic);
            return RecordFailure(assetID, reason.c_str());
        }

        u32 materialTableOffset = 0;
        if (!_materialRegistry->AppendDefaultMaterialTable(result.view.materialSlots, materialTableOffset))
            return RecordFailure(assetID, "material_table_append_failed");

        RenderAssets::ModelHandle handle;
        if (!_geometryStorage->Append(result.view, materialTableOffset, static_cast<u32>(result.view.materialSlots.size()), handle))
            return RecordFailure(assetID, "geometry_storage_append_failed");

        _limitations.invalidSkeletonReferences += result.limitations.invalidSkeletonReferences;
        _limitations.invalidAnimationBoundsReferences += result.limitations.invalidAnimationBoundsReferences;
        _limitations.invalidCollisionReferences += result.limitations.invalidCollisionReferences;
        _limitations.invalidEmbeddedModelReferences += result.limitations.invalidEmbeddedModelReferences;
        _entries.emplace(assetID, Entry{handle, 1, false});
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

    RenderAssets::ModelHandle ModelAssetRegistry::RecordFailure(FileFormat::AssetID assetID, const char* reason)
    {
        NC_LOG_ERROR("MODEL_ASSET model_fallback asset={} reason={}", assetID, reason);
        _entries.emplace(assetID, Entry{_fallbackModel, 1, true});
        ++_failures;
        return _fallbackModel;
    }
} // namespace ModelLoading
