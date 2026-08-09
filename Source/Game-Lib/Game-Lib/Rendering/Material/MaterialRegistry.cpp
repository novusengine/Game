#include "MaterialRegistry.h"

#include "Game-Lib/Rendering/Asset/AssetDiagnostic.h"
#include "Game-Lib/Rendering/Asset/AssetValidation.h"
#include "MaterialInstancePatcher.h"
#include "MaterialProgramLibrary.h"
#include "MaterialStorage.h"
#include "MaterialTextureRegistry.h"

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
    MaterialRegistry::MaterialRegistry(PACT::PactStorage* pactStorage, MaterialStorage* storage, MaterialProgramLibrary* programLibrary, MaterialTextureRegistry* textureRegistry)
        : _pactStorage(pactStorage), _storage(storage), _programLibrary(programLibrary), _textureRegistry(textureRegistry)
    {
    }

    RenderAssets::MaterialHandle MaterialRegistry::LoadMaterial(FileFormat::AssetID assetID)
    {
        ZoneScopedN("MaterialRegistry::LoadMaterial");

        const auto existing = _materials.find(assetID);
        if (existing != _materials.end())
        {
            ++existing->second.referenceCount;
            ++_cacheHits;
            return existing->second.handle;
        }

        if (assetID == FileFormat::INVALID_ASSET_ID)
            return RecordMaterialFailure(assetID, "invalid_asset_id");
        if (AssetLoading::ShouldInjectFailure(AssetLoading::FailureInjection::Material))
            return RecordMaterialFailure(assetID, "injected_failure");

        PACT::PactFileHandle file;
        PACT::PactReadResult readResult;
        {
            ZoneScopedN("Read Material From PACT");
            readResult = _pactStorage->ReadFile(assetID, file);
        }
        if (readResult != PACT::PactReadResult::Success)
            return RecordMaterialFailure(assetID, ToReason(readResult));

        const std::span<const u8> payload(reinterpret_cast<const u8*>(file.GetData()), file.GetSize());
        MaterialAssetReadResult<MaterialAssetView> result;
        {
            ZoneScopedN("Decode Material Asset");
            result = MaterialAssetReader::ReadMaterial(payload);
        }
        if (!result)
        {
            const std::string reason = AssetLoading::Describe(result.diagnostic);
            return RecordMaterialFailure(assetID, reason.c_str());
        }

        const FileFormat::Material::MaterialProgramRecord* program = _programLibrary->Resolve(result.view);
        if (program == nullptr)
            return RecordMaterialFailure(assetID, "material_program_resolution_failed");

        RenderAssets::MaterialHandle handle;
        if (!_storage->AppendMaterial(result.view, *program, handle))
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
        ZoneScopedN("MaterialRegistry::LoadMaterialInstance");

        const auto existing = _materialInstances.find(assetID);
        if (existing != _materialInstances.end())
        {
            ++existing->second.referenceCount;
            ++_cacheHits;
            return existing->second.handle;
        }

        if (assetID == FileFormat::INVALID_ASSET_ID)
            return RecordMaterialInstanceFailure(assetID, FileFormat::INVALID_ASSET_ID, "invalid_asset_id");
        if (AssetLoading::ShouldInjectFailure(AssetLoading::FailureInjection::MaterialInstance))
            return RecordMaterialInstanceFailure(assetID, FileFormat::INVALID_ASSET_ID, "injected_failure");

        PACT::PactFileHandle file;
        PACT::PactReadResult readResult;
        {
            ZoneScopedN("Read Material Instance From PACT");
            readResult = _pactStorage->ReadFile(assetID, file);
        }
        if (readResult != PACT::PactReadResult::Success)
            return RecordMaterialInstanceFailure(assetID, FileFormat::INVALID_ASSET_ID, ToReason(readResult));

        const std::span<const u8> payload(reinterpret_cast<const u8*>(file.GetData()), file.GetSize());
        MaterialAssetReadResult<MaterialInstanceAssetView> decoded;
        {
            ZoneScopedN("Decode Material Instance");
            decoded = MaterialAssetReader::DecodeMaterialInstance(payload);
        }
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

        MaterialAssetReadResult<MaterialInstanceAssetView> result;
        {
            ZoneScopedN("Validate Material Instance");
            result = MaterialAssetReader::ReadMaterialInstance(payload, GetMaterialView(materialEntry->second));
        }
        if (!result)
        {
            const std::string reason = AssetLoading::Describe(result.diagnostic);
            return RecordMaterialInstanceFailure(assetID, materialAssetID, reason.c_str());
        }

        std::vector<u8> parameters;
        std::vector<u32> textureIndices;
        std::vector<u32> samplerIDs;
        bool patched;
        {
            ZoneScopedN("Patch Material Resources");
            patched = MaterialInstancePatcher::Patch(result.view, [this, assetID](FileFormat::AssetID textureAssetID, bool optional) {
                    return _textureRegistry->Resolve(textureAssetID, assetID, optional);
                }, materialEntry->second.root.textureSlotCount, _textureRegistry->GetFallbackTextureIndex(), parameters, textureIndices, samplerIDs);
        }
        if (!patched)
            return RecordMaterialInstanceFailure(assetID, materialAssetID, "resource_patch_failed");

        RenderAssets::MaterialInstanceHandle handle;
        if (!_storage->AppendMaterialInstance(material, result.view.root, parameters, textureIndices, samplerIDs, handle))
            return RecordMaterialInstanceFailure(assetID, materialAssetID, "storage_append_failed");

        _materialInstances.emplace(assetID, MaterialInstanceEntry{handle, 1, false});
        return handle;
    }

    RenderAssets::MaterialInstanceHandle MaterialRegistry::DeriveMaterialInstance(RenderAssets::MaterialInstanceHandle base, std::span<const MaterialTextureAssetOverride> overrides, FileFormat::AssetID ownerAssetID)
    {
        std::vector<MaterialTextureOverride> resolved;
        resolved.reserve(overrides.size());
        for (const MaterialTextureAssetOverride& overrideValue : overrides)
        {
            resolved.push_back({.textureSlot = overrideValue.textureSlot, .textureIndex = _textureRegistry->Resolve(overrideValue.textureAssetID, ownerAssetID, false)});
        }
        RenderAssets::MaterialInstanceHandle handle;
        return _storage->DeriveMaterialInstance(base, resolved, handle) ? handle :
            _storage->GetFallbackMaterialInstance();
    }

    RenderAssets::MaterialInstanceHandle MaterialRegistry::DeriveMaterialInstance(RenderAssets::MaterialInstanceHandle base, std::span<const MaterialTextureRuntimeOverride> overrides)
    {
        std::vector<MaterialTextureOverride> resolved;
        resolved.reserve(overrides.size());
        for (const MaterialTextureRuntimeOverride& overrideValue : overrides)
            resolved.push_back({.textureSlot = overrideValue.textureSlot, .textureIndex = _textureRegistry->Resolve(overrideValue.textureID)});

        RenderAssets::MaterialInstanceHandle handle;
        return _storage->DeriveMaterialInstance(base, resolved, handle) ? handle : _storage->GetFallbackMaterialInstance();
    }

    bool MaterialRegistry::AppendDefaultMaterialTable(std::span<const FileFormat::Model::MaterialSlot> materialSlots, u32& outOffset)
    {
        ZoneScopedN("MaterialRegistry::AppendDefaultMaterialTable");

        std::vector<RenderAssets::MaterialInstanceHandle> handles;
        handles.reserve(materialSlots.size());
        for (const FileFormat::Model::MaterialSlot& slot : materialSlots)
        {
            handles.push_back(slot.defaultMaterialInstanceAssetID == FileFormat::INVALID_ASSET_ID
                ? _storage->GetFallbackMaterialInstance()
                : LoadMaterialInstance(slot.defaultMaterialInstanceAssetID));
        }

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

    RenderAssets::MaterialInstanceHandle MaterialRegistry::RecordMaterialInstanceFailure(FileFormat::AssetID assetID, FileFormat::AssetID dependencyAssetID, const char* reason)
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
