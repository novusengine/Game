#include "RenderAssetResources.h"

#include <Base/Util/DebugHandler.h>

#include <FileFormat/Novus/Map/Map.h>

#include <Renderer/RenderGraph.h>

#include <algorithm>
#include <array>
#include <limits>

namespace RenderAssets
{
    RenderAssetResources::RenderAssetResources(Renderer::Renderer* renderer, PACT::PactStorage* pactStorage, Renderer::DescriptorSet* materialDescriptorSet, bool validateTransfers)
        : _renderer(renderer), _textureRegistry(renderer, pactStorage), _materialStorage(validateTransfers),
          _materialAnimator(pactStorage, &_materialStorage),
          _materialBindings(renderer, materialDescriptorSet),
          _materialRegistry(pactStorage, &_materialStorage, &_materialProgramLibrary, &_textureRegistry, &_materialAnimator),
          _geometryStorage(validateTransfers),
          _modelRegistry(pactStorage, &_geometryStorage, &_materialStorage, &_materialRegistry), _captureScratch(validateTransfers)
    {
        _captureScratch.SetDebugName("Render Asset Capture Scratch");
        _captureScratch.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
        _captureScratch.AddCount(18 * 16);
    }

    void RenderAssetResources::Update(f32 deltaTime)
    {
        _materialAnimator.Update(deltaTime);
    }

    bool RenderAssetResources::Initialize()
    {
        if (_initialized)
            return false;

        std::string materialPackError;
        if (!_materialProgramLibrary.Load("Data/Shaders/Materials.matpack", materialPackError))
        {
            NC_LOG_CRITICAL("MATERIAL_PACK load_failed path=Data/Shaders/Materials.matpack reason={}", materialPackError);
            return false;
        }

        if (!_textureRegistry.Initialize() ||
            !_materialStorage.InitializeFallback(_textureRegistry.GetFallbackTextureIndex()) ||
            !_modelRegistry.InitializeFallback())
        {
            NC_LOG_CRITICAL("MODEL_ASSET fallback_initialization_failed resource=render_assets reason=bootstrap_failed");
            return false;
        }

        _initialized = true;
        SyncToGPU();
        return true;
    }

    void RenderAssetResources::ReserveModelResources(const Map::ModelResourceAllocationHints& hints)
    {
        if (hints.models > std::numeric_limits<u32>::max())
        {
            NC_LOG_WARNING("MODEL_ALLOCATION_HINT ignored resource=model_registry count={} max={}", hints.models, std::numeric_limits<u32>::max());
        }
        else
        {
            _modelRegistry.Reserve(static_cast<u32>(hints.models));
        }
        _geometryStorage.Reserve(hints);
    }

    void RenderAssetResources::SyncToGPU()
    {
        ZoneScopedN("RenderAssetResources::SyncToGPU");

        if (!_initialized)
            return;

        _materialStorage.SyncToGPU(_renderer);
        _geometryStorage.SyncToGPU(_renderer);
        _captureScratch.SyncToGPU(_renderer);
        _textureRegistry.FlushDescriptors();
        _materialBindings.Upload(_materialStorage, _textureRegistry);
    }

    void RenderAssetResources::AddCapturePass(Renderer::RenderGraph& renderGraph)
    {
        struct CapturePassData
        {
            std::array<Renderer::BufferResource, 18> sources;
            std::array<u32, 18> sizes = {};
            Renderer::BufferMutableResource scratch;
            u32 numSources = 0;
        };

        renderGraph.AddPass<CapturePassData>("Render Asset Capture Buffers",
            [this](CapturePassData& data, Renderer::RenderGraphBuilder& builder) {
                using BufferUsage = Renderer::BufferPassUsage;
                auto Track = [&builder, &data](const auto& buffer) {
                    if (buffer.UsedBytes() >= sizeof(u32))
                    {
                        data.sizes[data.numSources] = std::min(buffer.UsedBytes(), 64u);
                        data.sources[data.numSources++] = builder.Read(buffer.GetBuffer(), BufferUsage::TRANSFER);
                    }
                };

                Track(_materialStorage.GetMaterials());
                Track(_materialStorage.GetMaterialInstances());
                Track(_materialStorage.GetMaterialTable());
                Track(_materialStorage.GetParameterStorage().GetBuffer());
                Track(_geometryStorage.GetRecords());
                Track(_geometryStorage.GetMeshes());
                Track(_geometryStorage.GetMeshLODs());
                Track(_geometryStorage.GetSubmeshes());
                Track(_geometryStorage.GetMeshlets());
                Track(_geometryStorage.GetPositions());
                Track(_geometryStorage.GetVertexAttributes());
                Track(_geometryStorage.GetSkinningData());
                Track(_geometryStorage.GetMeshletVertexIndices());
                Track(_geometryStorage.GetMeshletTriangles());
                Track(_geometryStorage.GetJointPaletteRemaps());
                Track(_geometryStorage.GetMaterialSlots());
                Track(_geometryStorage.GetEmbeddedInstanceSets());
                Track(_geometryStorage.GetEmbeddedInstances());
                data.scratch = builder.Write(_captureScratch.GetBuffer(), BufferUsage::TRANSFER);
                return true;
            },
            [](CapturePassData& data, Renderer::RenderGraphResources&, Renderer::CommandList& commandList) {
                commandList.PushMarker("Render Asset Capture Buffers", Color::White);
                for (u32 index = 0; index < data.numSources; ++index)
                    commandList.CopyBuffer(data.scratch, index * 64u, data.sources[index], 0, data.sizes[index]);
                commandList.PopMarker();
            }, Renderer::RenderPassFlags::SideEffect);
    }

    RenderAssetResourceStats RenderAssetResources::GetStats() const
    {
        RenderAssetResourceStats stats;
        stats.modelGeometry = _geometryStorage.GetStats();
        stats.models = _modelRegistry.GetStats();
        stats.materialStorage = _materialStorage.GetStats();
        stats.materialPrograms = _materialProgramLibrary.GetStats();
        stats.materialBindings = _materialBindings.GetStats();
        stats.materials = _materialRegistry.GetStats();
        stats.textures = _textureRegistry.GetStats();
        return stats;
    }
} // namespace RenderAssets
