#pragma once

#include "ModelShadowWorkResources.h"

#include <Renderer/DescriptorSet.h>
#include <Renderer/Descriptors/ComputePipelineDesc.h>
#include <Renderer/Descriptors/GraphicsPipelineDesc.h>

#include <robinhood/robinhood.h>

struct RenderResources;
class GameRenderer;

namespace MaterialLoading { class MaterialStorage; }
namespace ModelLoading { class ModelGeometryStorage; }
namespace RenderScenes { class RenderScene; }
namespace Renderer { class RenderGraph; class Renderer; }

namespace ShadowRendering
{
    // Owns the GPU-side pipelines and bindings that select, cull, and rasterize model shadow meshlets.
    // It turns Scene model instances into static and dynamic SVSM page-pool writes.
    class ModelShadowPass
    {
      public:
        ModelShadowPass(Renderer::Renderer* renderer, GameRenderer* gameRenderer);

        bool Upload(const ModelShadowWorkResources& work, const ModelLoading::ModelGeometryStorage& geometry,
                    const RenderScenes::RenderScene& scene, Renderer::BufferID svsmData,
                    Renderer::BufferID staticPageTable, Renderer::BufferID dynamicPageTable);
        void AddCullPass(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                         ModelShadowWorkResources& work, const ModelLoading::ModelGeometryStorage& geometry,
                         const MaterialLoading::MaterialStorage& materials, const RenderScenes::RenderScene& scene,
                         Renderer::BufferID svsmData, u32 numClipmaps, bool dynamicSplit, u8 frameIndex,
                         i32 forcedLOD, f32 lodTargetTexels, bool coneCulling);
        void AddRasterPass(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                           const ModelShadowWorkResources& work,
                           const ModelLoading::ModelGeometryStorage& geometry,
                           const MaterialLoading::MaterialStorage& materials,
                           const RenderScenes::RenderScene& scene, Renderer::BufferID svsmData,
                           Renderer::BufferID staticPageTable, Renderer::BufferID dynamicPageTable,
                           Renderer::ImageID staticPagePool, Renderer::ImageID dynamicPagePool,
                           u32 virtualSize, bool dynamicSplit, bool opacityDither, u8 frameIndex);

      private:
        bool Bind(Renderer::DescriptorSet& set, StringUtils::StringHash name, Renderer::BufferID buffer,
                  Renderer::BufferID& current);
        bool UploadCullBindings(const ModelShadowWorkResources& work,
                                const ModelLoading::ModelGeometryStorage& geometry,
                                const RenderScenes::RenderScene& scene, Renderer::BufferID svsmData);
        bool UploadRasterBindings(const ModelShadowWorkResources& work,
                                  const ModelLoading::ModelGeometryStorage& geometry,
                                  const RenderScenes::RenderScene& scene, Renderer::BufferID svsmData,
                                  Renderer::BufferID staticPageTable, Renderer::BufferID dynamicPageTable);
        void BindRasterShared(Renderer::DescriptorSet& set, u32 setIndex, bool alphaTest,
                              const ModelShadowWorkResources& work,
                              const ModelLoading::ModelGeometryStorage& geometry,
                              const RenderScenes::RenderScene& scene, Renderer::BufferID svsmData,
                              Renderer::BufferID pageTable, bool& changed);

        Renderer::Renderer* _renderer = nullptr;
        Renderer::DescriptorSet _expandSet;
        Renderer::DescriptorSet _expandFinalizeSet;
        Renderer::DescriptorSet _cullSet;
        Renderer::DescriptorSet _finalizeSet;
        Renderer::DescriptorSet _staticSolidSet;
        Renderer::DescriptorSet _staticAlphaSet;
        Renderer::DescriptorSet _dynamicSolidSet;
        Renderer::DescriptorSet _dynamicAlphaSet;
        Renderer::ComputePipelineID _expandPipeline = Renderer::ComputePipelineID::Invalid();
        Renderer::ComputePipelineID _expandFinalizePipeline = Renderer::ComputePipelineID::Invalid();
        Renderer::ComputePipelineID _cullPipeline = Renderer::ComputePipelineID::Invalid();
        Renderer::ComputePipelineID _finalizePipeline = Renderer::ComputePipelineID::Invalid();
        Renderer::GraphicsPipelineID _solidOneSidedPipeline = Renderer::GraphicsPipelineID::Invalid();
        Renderer::GraphicsPipelineID _solidTwoSidedPipeline = Renderer::GraphicsPipelineID::Invalid();
        Renderer::GraphicsPipelineID _alphaOneSidedPipeline = Renderer::GraphicsPipelineID::Invalid();
        Renderer::GraphicsPipelineID _alphaTwoSidedPipeline = Renderer::GraphicsPipelineID::Invalid();
        robin_hood::unordered_flat_map<u32, Renderer::BufferID> _cullBindings;
        robin_hood::unordered_flat_map<u64, Renderer::BufferID> _rasterBindings;
        u32 _generation = 0;
        u32 _cullDescriptorWarmupFrames = 0;
        u32 _rasterDescriptorWarmupFrames = 0;
    };
} // namespace ShadowRendering
