#include "MaterialRegistry.h"

#include "Game-Lib/Rendering/Asset/AssetDiagnostic.h"
#include "MaterialInstancePatcher.h"
#include "MaterialStorage.h"
#include "ModelTextureResolver.h"

#include <Base/Util/DebugHandler.h>

#include <Filesystem/PactStorage.h>

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

namespace MaterialLoading
{
    MaterialRegistry::MaterialRegistry(PACT::PactStorage* pactStorage, MaterialStorage* storage, ModelTextureResolver* textureResolver)
        : _pactStorage(pactStorage), _storage(storage), _textureResolver(textureResolver)
    {
    }

    RenderAssets::MaterialHandle MaterialRegistry::LoadMaterial(FileFormat::AssetID assetID)
    {
        const auto existing = _materials.find(assetID);
        if (existing != _materials.end())
        {
            ++existing->second.referenceCount;
            ++_cacheHits;
            return existing->second.handle;
        }

        if (assetID == FileFormat::INVALID_ASSET_ID)
            return RecordMaterialFailure(assetID, "invalid_asset_id");

        PACT::PactFileHandle file;
        const PACT::PactReadResult readResult = _pactStorage->ReadFile(assetID, file);
        if (readResult != PACT::PactReadResult::Success)
            return RecordMaterialFailure(assetID, ToReason(readResult));

        const std::span<const u8> payload(reinterpret_cast<const u8*>(file.GetData()), file.GetSize());
        const MaterialAssetReadResult<MaterialAssetView> result = MaterialAssetReader::ReadMaterial(payload);
        if (!result)
        {
            const std::string reason = AssetLoading::Describe(result.diagnostic);
            return RecordMaterialFailure(assetID, reason.c_str());
        }

        RenderAssets::MaterialHandle handle;
        if (!_storage->AppendMaterial(result.view, handle))
            return RecordMaterialFailure(assetID, "storage_append_failed");

        MaterialEntry entry;
        entry.handle = handle;
        entry.root = result.view.root;
        entry.parameters.assign(result.view.parameters.begin(), result.view.parameters.end());
        entry.referenceCount = 1;
        _materials.emplace(assetID, std::move(entry));
        return handle;
    }

    RenderAssets::MaterialInstanceHandle MaterialRegistry::LoadMaterialInstance(FileFormat::AssetID assetID)
    {
        const auto existing = _materialInstances.find(assetID);
        if (existing != _materialInstances.end())
        {
            ++existing->second.referenceCount;
            ++_cacheHits;
            return existing->second.handle;
        }

        if (assetID == FileFormat::INVALID_ASSET_ID)
            return RecordMaterialInstanceFailure(assetID, FileFormat::INVALID_ASSET_ID, "invalid_asset_id");

        PACT::PactFileHandle file;
        const PACT::PactReadResult readResult = _pactStorage->ReadFile(assetID, file);
        if (readResult != PACT::PactReadResult::Success)
            return RecordMaterialInstanceFailure(assetID, FileFormat::INVALID_ASSET_ID, ToReason(readResult));

        const std::span<const u8> payload(reinterpret_cast<const u8*>(file.GetData()), file.GetSize());
        const MaterialAssetReadResult<MaterialInstanceAssetView> decoded = MaterialAssetReader::DecodeMaterialInstance(payload);
        if (!decoded)
        {
            const std::string reason = AssetLoading::Describe(decoded.diagnostic);
            return RecordMaterialInstanceFailure(assetID, FileFormat::INVALID_ASSET_ID, reason.c_str());
        }

        const FileFormat::AssetID materialAssetID = decoded.view.root.materialAssetID;
        const RenderAssets::MaterialHandle material = LoadMaterial(materialAssetID);
        const auto materialEntry = _materials.find(materialAssetID);
        if (materialEntry == _materials.end() || materialEntry->second.usedFallback)
            return RecordMaterialInstanceFailure(assetID, materialAssetID, "material_dependency_failed");

        const MaterialAssetReadResult<MaterialInstanceAssetView> result =
            MaterialAssetReader::ReadMaterialInstance(payload, GetMaterialView(materialEntry->second));
        if (!result)
        {
            const std::string reason = AssetLoading::Describe(result.diagnostic);
            return RecordMaterialInstanceFailure(assetID, materialAssetID, reason.c_str());
        }

        std::vector<u8> parameters;
        const bool patched = MaterialInstancePatcher::Patch(result.view,
            [this, assetID](FileFormat::AssetID textureAssetID, bool optional) {
                return _textureResolver->Resolve(textureAssetID, assetID, optional);
            },
            parameters);
        if (!patched)
            return RecordMaterialInstanceFailure(assetID, materialAssetID, "resource_patch_failed");

        RenderAssets::MaterialInstanceHandle handle;
        if (!_storage->AppendMaterialInstance(material, parameters, handle))
            return RecordMaterialInstanceFailure(assetID, materialAssetID, "storage_append_failed");

        _materialInstances.emplace(assetID, MaterialInstanceEntry{handle, 1, false});
        return handle;
    }

    bool MaterialRegistry::AppendDefaultMaterialTable(std::span<const FileFormat::Model::MaterialSlot> materialSlots, u32& outOffset)
    {
        std::vector<RenderAssets::MaterialInstanceHandle> handles;
        handles.reserve(materialSlots.size());
        for (const FileFormat::Model::MaterialSlot& slot : materialSlots)
            handles.push_back(LoadMaterialInstance(slot.defaultMaterialInstanceAssetID));

        return _storage->AppendMaterialTable(handles, outOffset);
    }

    MaterialRegistryStats MaterialRegistry::GetStats() const
    {
        MaterialRegistryStats stats;
        stats.residentMaterials = static_cast<u32>(_materials.size()) + 1;
        stats.residentMaterialInstances = static_cast<u32>(_materialInstances.size()) + 1;
        for (const auto& [assetID, entry] : _materials)
            stats.materialReferences += entry.referenceCount;
        for (const auto& [assetID, entry] : _materialInstances)
            stats.materialInstanceReferences += entry.referenceCount;
        stats.cacheHits = _cacheHits;
        stats.materialFailures = _materialFailures;
        stats.materialInstanceFailures = _materialInstanceFailures;
        return stats;
    }

    RenderAssets::MaterialHandle MaterialRegistry::RecordMaterialFailure(FileFormat::AssetID assetID, const char* reason)
    {
        NC_LOG_ERROR("MODEL_ASSET material_fallback asset={} reason={}", assetID, reason);
        const RenderAssets::MaterialHandle fallback = _storage->GetFallbackMaterial();
        _materials.emplace(assetID, MaterialEntry{fallback, {}, {}, 1, true});
        ++_materialFailures;
        return fallback;
    }

    RenderAssets::MaterialInstanceHandle MaterialRegistry::RecordMaterialInstanceFailure(FileFormat::AssetID assetID,
                                                                                          FileFormat::AssetID dependencyAssetID,
                                                                                          const char* reason)
    {
        NC_LOG_ERROR("MODEL_ASSET material_instance_fallback asset={} dependency={} reason={}", assetID, dependencyAssetID, reason);
        const RenderAssets::MaterialInstanceHandle fallback = _storage->GetFallbackMaterialInstance();
        _materialInstances.emplace(assetID, MaterialInstanceEntry{fallback, 1, true});
        ++_materialInstanceFailures;
        return fallback;
    }

    MaterialAssetView MaterialRegistry::GetMaterialView(const MaterialEntry& entry) const
    {
        MaterialAssetView view;
        view.root = entry.root;
        view.parameters = entry.parameters;
        return view;
    }
} // namespace MaterialLoading
