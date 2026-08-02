#pragma once
#include "Game-Lib/Rendering/Material/MaterialRegistry.h"
#include "Game-Lib/Rendering/Material/MaterialStorage.h"
#include "Game-Lib/Rendering/Material/ModelTextureResolver.h"
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

namespace RenderAssets
{
    struct RenderAssetResourceStats
    {
        ModelLoading::ModelGeometryStorageStats geometry;
        ModelLoading::ModelAssetRegistryStats models;
        MaterialLoading::MaterialStorageStats materialStorage;
        MaterialLoading::MaterialRegistryStats materials;
        MaterialLoading::ModelTextureResolverStats textures;
    };

    // Coordinates the CPU-side registries and GPU-side storage for model, material, and texture assets.
    // It keeps dependent asset loading and GPU publication behind one renderer-owned synchronization point.
    class RenderAssetResources
    {
      public:
        RenderAssetResources(Renderer::Renderer* renderer, PACT::PactStorage* pactStorage, bool validateTransfers);

        bool Initialize();
        void SyncToGPU();
        void AddCapturePass(Renderer::RenderGraph& renderGraph);

        ModelHandle LoadModel(FileFormat::AssetID assetID) { return _modelRegistry.Load(assetID); }
        ModelHandle GetFallbackModel() const { return _modelRegistry.GetFallbackModel(); }
        MaterialHandle GetFallbackMaterial() const { return _materialStorage.GetFallbackMaterial(); }
        MaterialInstanceHandle GetFallbackMaterialInstance() const { return _materialStorage.GetFallbackMaterialInstance(); }

        const ModelLoading::ModelGeometryStorage& GetGeometryStorage() const { return _geometryStorage; }
        const MaterialLoading::MaterialStorage& GetMaterialStorage() const { return _materialStorage; }
        const MaterialLoading::ModelTextureResolver& GetTextureResolver() const { return _textureResolver; }
        RenderAssetResourceStats GetStats() const;

      private:
        Renderer::Renderer* _renderer = nullptr;
        MaterialLoading::ModelTextureResolver _textureResolver;
        MaterialLoading::MaterialStorage _materialStorage;
        MaterialLoading::MaterialRegistry _materialRegistry;
        ModelLoading::ModelGeometryStorage _geometryStorage;
        ModelLoading::ModelAssetRegistry _modelRegistry;
        Renderer::GPUVector<u32> _captureScratch;
        bool _initialized = false;
    };
} // namespace RenderAssets
