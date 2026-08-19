#pragma once
#include "Game-Lib/Rendering/Material/MaterialRegistry.h"
#include "Game-Lib/Rendering/Material/MaterialAnimator.h"
#include "Game-Lib/Rendering/Material/MaterialProgramLibrary.h"
#include "Game-Lib/Rendering/Material/MaterialResourceBindings.h"
#include "Game-Lib/Rendering/Material/MaterialStorage.h"
#include "Game-Lib/Rendering/Material/MaterialTextureRegistry.h"
#include "Game-Lib/Rendering/Model/Asset/ModelAssetRegistry.h"
#include "Game-Lib/Rendering/Model/Asset/ModelGeometryStorage.h"

namespace PACT
{
    class PactStorage;
}

namespace Renderer
{
    class Renderer;
    class RenderGraph;
}

namespace Map
{
    struct ModelResourceAllocationHints;
}

namespace RenderAssets
{
    struct RenderAssetResourceStats
    {
        ModelLoading::ModelGeometryStorageStats modelGeometry;
        ModelLoading::ModelAssetRegistryStats models;
        MaterialLoading::MaterialStorageStats materialStorage;
        MaterialLoading::MaterialProgramLibraryStats materialPrograms;
        MaterialLoading::MaterialResourceBindingStats materialBindings;
        MaterialLoading::MaterialRegistryStats materials;
        MaterialLoading::MaterialTextureRegistryStats textures;
    };

    // Coordinates the CPU-side registries and GPU-side storage for model, material, and texture assets.
    // It keeps dependent asset loading and GPU publication behind one renderer-owned synchronization point.
    class RenderAssetResources
    {
      public:
        RenderAssetResources(Renderer::Renderer* renderer, PACT::PactStorage* pactStorage, Renderer::DescriptorSet* materialDescriptorSet, bool validateTransfers);

        bool Initialize();
        void Update(f32 deltaTime);
        void ReserveModelResources(const Map::ModelResourceAllocationHints& hints);
        void SyncToGPU();
        void AddCapturePass(Renderer::RenderGraph& renderGraph);

        ModelHandle LoadModel(FileFormat::AssetID assetID) { return _modelRegistry.Load(assetID); }
        ModelLoading::ModelLoadStatus BeginModelLoad(FileFormat::AssetID assetID, ModelHandle& outHandle)
        {
            return _modelRegistry.BeginLoad(assetID, outHandle);
        }
        ModelLoading::ModelLoadStatus PollModelLoad(FileFormat::AssetID assetID, ModelHandle& outHandle)
        {
            return _modelRegistry.PollLoad(assetID, outHandle);
        }
        bool ReleaseModel(FileFormat::AssetID assetID) { return _modelRegistry.Release(assetID); }
        MaterialInstanceHandle LoadMaterialInstance(FileFormat::AssetID assetID)
        {
            return _materialRegistry.LoadMaterialInstance(assetID);
        }
        MaterialInstanceHandle DeriveMaterialInstance(MaterialInstanceHandle base, std::span<const MaterialLoading::MaterialTextureAssetOverride> overrides, FileFormat::AssetID ownerAssetID)
        {
            return _materialRegistry.DeriveMaterialInstance(base, overrides, ownerAssetID);
        }
        MaterialInstanceHandle DeriveMaterialInstance(MaterialInstanceHandle base, std::span<const MaterialLoading::MaterialTextureRuntimeOverride> overrides)
        {
            return _materialRegistry.DeriveMaterialInstance(base, overrides);
        }
        u32 ResolveTexture(FileFormat::AssetID textureAssetID, FileFormat::AssetID ownerAssetID)
        {
            return _textureRegistry.Resolve(textureAssetID, ownerAssetID, false);
        }
        ModelLoading::EmbeddedModelLoadStatus LoadEmbeddedModel(FileFormat::AssetID assetID, ModelHandle& outHandle)
        {
            return _modelRegistry.LoadEmbedded(assetID, outHandle);
        }
        ModelHandle GetFallbackModel() const { return _modelRegistry.GetFallbackModel(); }
        MaterialHandle GetFallbackMaterial() const { return _materialStorage.GetFallbackMaterial(); }
        MaterialInstanceHandle GetFallbackMaterialInstance() const { return _materialStorage.GetFallbackMaterialInstance(); }

        const ModelLoading::ModelGeometryStorage& GetModelGeometryStorage() const { return _geometryStorage; }
        const MaterialLoading::MaterialStorage& GetMaterialStorage() const { return _materialStorage; }
        const MaterialLoading::MaterialProgramLibrary& GetMaterialProgramLibrary() const
        {
            return _materialProgramLibrary;
        }
        const MaterialLoading::MaterialTextureRegistry& GetTextureRegistry() const { return _textureRegistry; }
        RenderAssetResourceStats GetStats() const;

      private:
        Renderer::Renderer* _renderer = nullptr;
        MaterialLoading::MaterialTextureRegistry _textureRegistry;
        MaterialLoading::MaterialStorage _materialStorage;
        MaterialLoading::MaterialAnimator _materialAnimator;
        MaterialLoading::MaterialResourceBindings _materialBindings;
        MaterialLoading::MaterialProgramLibrary _materialProgramLibrary;
        MaterialLoading::MaterialRegistry _materialRegistry;
        ModelLoading::ModelGeometryStorage _geometryStorage;
        ModelLoading::ModelAssetRegistry _modelRegistry;
        Renderer::GPUVector<u32> _captureScratch;
        bool _initialized = false;
    };
} // namespace RenderAssets
