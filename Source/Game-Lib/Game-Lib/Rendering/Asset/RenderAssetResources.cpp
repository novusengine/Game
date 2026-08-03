#include "RenderAssetResources.h"

#include <Base/Util/DebugHandler.h>

#include <Renderer/RenderGraph.h>

#include <algorithm>
#include <array>

namespace RenderAssets
{
    RenderAssetResources::RenderAssetResources(Renderer::Renderer* renderer, PACT::PactStorage* pactStorage, bool validateTransfers)
        : _renderer(renderer), _textureRegistry(renderer, pactStorage), _materialStorage(validateTransfers),
          _materialRegistry(pactStorage, &_materialStorage, &_textureRegistry), _geometryStorage(validateTransfers),
          _modelRegistry(pactStorage, &_geometryStorage, &_materialStorage, &_materialRegistry), _captureScratch(validateTransfers)
    {
        _captureScratch.SetDebugName("Render Asset Capture Scratch");
        _captureScratch.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
        _captureScratch.AddCount(18 * 16);
    }

    bool RenderAssetResources::Initialize()
    {
        if (_initialized)
            return false;

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

    void RenderAssetResources::SyncToGPU()
    {
        if (!_initialized)
            return;

        _materialStorage.SyncToGPU(_renderer);
        _geometryStorage.SyncToGPU(_renderer);
        _captureScratch.SyncToGPU(_renderer);
        _textureRegistry.FlushDescriptors();
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
                auto track = [&builder, &data](const auto& buffer) {
                    if (buffer.UsedBytes() >= sizeof(u32))
                    {
                        data.sizes[data.numSources] = std::min(buffer.UsedBytes(), 64u);
                        data.sources[data.numSources++] = builder.Read(buffer.GetBuffer(), BufferUsage::TRANSFER);
                    }
                };

                track(_materialStorage.GetMaterials());
                track(_materialStorage.GetMaterialInstances());
                track(_materialStorage.GetMaterialTable());
                track(_materialStorage.GetParameterStorage().GetBuffer());
                track(_geometryStorage.GetRecords());
                track(_geometryStorage.GetMeshes());
                track(_geometryStorage.GetMeshLODs());
                track(_geometryStorage.GetSubmeshes());
                track(_geometryStorage.GetMeshlets());
                track(_geometryStorage.GetPositions());
                track(_geometryStorage.GetVertexAttributes());
                track(_geometryStorage.GetSkinningData());
                track(_geometryStorage.GetMeshletVertexIndices());
                track(_geometryStorage.GetMeshletTriangles());
                track(_geometryStorage.GetJointPaletteRemaps());
                track(_geometryStorage.GetMaterialSlots());
                track(_geometryStorage.GetEmbeddedInstanceSets());
                track(_geometryStorage.GetEmbeddedInstances());
                data.scratch = builder.Write(_captureScratch.GetBuffer(), BufferUsage::TRANSFER);
                return true;
            },
            [](CapturePassData& data, Renderer::RenderGraphResources&, Renderer::CommandList& commandList) {
                commandList.PushMarker("Render Asset Capture Buffers", Color::White);
                for (u32 index = 0; index < data.numSources; ++index)
                    commandList.CopyBuffer(data.scratch, index * 64u, data.sources[index], 0, data.sizes[index]);
                commandList.PopMarker();
            });
    }

    RenderAssetResourceStats RenderAssetResources::GetStats() const
    {
        RenderAssetResourceStats stats;
        stats.modelGeometry = _geometryStorage.GetStats();
        stats.models = _modelRegistry.GetStats();
        stats.materialStorage = _materialStorage.GetStats();
        stats.materials = _materialRegistry.GetStats();
        stats.textures = _textureRegistry.GetStats();
        return stats;
    }
} // namespace RenderAssets
