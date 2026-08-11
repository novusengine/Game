#pragma once
#include "Game-Lib/Rendering/Asset/RenderAssetHandles.h"
#include "MaterialAssetReader.h"

#include <FileFormat/Novus/FileHeader.h>
#include <FileFormat/Novus/Model/Model.h>

#include <Renderer/Descriptors/TextureDesc.h>

#include <robinhood/robinhood.h>

#include <vector>

namespace PACT
{
    class PactStorage;
}

namespace MaterialLoading
{
    class MaterialStorage;
    class MaterialAnimator;
    class MaterialProgramLibrary;
    class MaterialTextureRegistry;

    struct MaterialTextureAssetOverride
    {
        u32 textureSlot = 0;
        FileFormat::AssetID textureAssetID = FileFormat::INVALID_ASSET_ID;
    };

    struct MaterialTextureRuntimeOverride
    {
        u32 textureSlot = 0;
        Renderer::TextureID textureID = Renderer::TextureID::Invalid();
    };

    struct MaterialRegistryStats
    {
        u32 residentMaterials = 0;
        u32 residentMaterialInstances = 0;
        u32 materialReferences = 0;
        u32 materialInstanceReferences = 0;
        u32 cacheHits = 0;
        u32 materialFailures = 0;
        u32 materialInstanceFailures = 0;
    };

    // Maintains CPU-side AssetID caches that load materials and allocate handles in GPU-backed material storage.
    // It avoids duplicate loads while resolving asset dependencies to stable runtime handles.
    class MaterialRegistry
    {
      public:
        MaterialRegistry(PACT::PactStorage* pactStorage, MaterialStorage* storage, MaterialProgramLibrary* programLibrary,
                         MaterialTextureRegistry* textureRegistry, MaterialAnimator* animator = nullptr);

        RenderAssets::MaterialHandle LoadMaterial(FileFormat::AssetID assetID);
        RenderAssets::MaterialInstanceHandle LoadMaterialInstance(FileFormat::AssetID assetID);
        RenderAssets::MaterialInstanceHandle DeriveMaterialInstance(RenderAssets::MaterialInstanceHandle base, std::span<const MaterialTextureAssetOverride> overrides, FileFormat::AssetID ownerAssetID);
        RenderAssets::MaterialInstanceHandle DeriveMaterialInstance(RenderAssets::MaterialInstanceHandle base, std::span<const MaterialTextureRuntimeOverride> overrides);
        bool AppendDefaultMaterialTable(std::span<const FileFormat::Model::MaterialSlot> materialSlots, u32& outOffset);

        MaterialRegistryStats GetStats() const;

      private:
        struct MaterialEntry
        {
            RenderAssets::MaterialHandle handle;
            FileFormat::Material::MaterialAsset root;
            std::vector<FileFormat::Material::ParameterDefinition> parameters;
            u32 referenceCount = 0;
            bool usedFallback = false;
        };

        struct MaterialInstanceEntry
        {
            RenderAssets::MaterialInstanceHandle handle;
            u32 referenceCount = 0;
            bool usedFallback = false;
        };

        RenderAssets::MaterialHandle RecordMaterialFailure(FileFormat::AssetID assetID, const char* reason);
        RenderAssets::MaterialInstanceHandle RecordMaterialInstanceFailure(FileFormat::AssetID assetID, FileFormat::AssetID dependencyAssetID, const char* reason);
        MaterialAssetView GetMaterialView(const MaterialEntry& entry) const;

        PACT::PactStorage* _pactStorage = nullptr;
        MaterialStorage* _storage = nullptr;
        MaterialProgramLibrary* _programLibrary = nullptr;
        MaterialTextureRegistry* _textureRegistry = nullptr;
        MaterialAnimator* _animator = nullptr;
        robin_hood::unordered_map<FileFormat::AssetID, MaterialEntry> _materials;
        robin_hood::unordered_map<FileFormat::AssetID, MaterialInstanceEntry> _materialInstances;
        u32 _cacheHits = 0;
        u32 _materialFailures = 0;
        u32 _materialInstanceFailures = 0;
    };
} // namespace MaterialLoading
