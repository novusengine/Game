#include "ModelTransparentPass.h"

#include "Game-Lib/Rendering/Asset/RenderAssetResources.h"
#include "Game-Lib/Rendering/CullUtils.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Light/LightRenderer.h"
#include "Game-Lib/Rendering/Material/MaterialProgramLibrary.h"
#include "Game-Lib/Rendering/Material/MaterialRenderer.h"
#include "Game-Lib/Rendering/Material/MaterialStorage.h"
#include "Game-Lib/Rendering/Model/Asset/ModelGeometryStorage.h"
#include "Game-Lib/Rendering/Model/ModelRendererMode.h"
#include "Game-Lib/Rendering/Model/View/ModelViewState.h"
#include "Game-Lib/Rendering/RenderResources.h"
#include "Game-Lib/Rendering/Scene/RenderScene.h"
#include "Game-Lib/Rendering/Scene/RenderView.h"
#include "Game-Lib/Rendering/Shadow/ShadowRenderer.h"

#include <Base/CVarSystem/CVarSystem.h>
#include <Renderer/Descriptors/ComputeShaderDesc.h>
#include <Renderer/RenderGraph.h>
#include <Renderer/Renderer.h>

namespace ModelPipeline
{
    ModelTransparentPass::ModelTransparentPass(Renderer::Renderer* renderer, GameRenderer* gameRenderer)
        : _renderer(renderer), _gameRenderer(gameRenderer), _beginSet(Renderer::DescriptorSetSlot::PER_PASS),
          _expandSet(Renderer::DescriptorSetSlot::PER_PASS),
          _expandFinalizeSet(Renderer::DescriptorSetSlot::PER_PASS), _cullSet(Renderer::DescriptorSetSlot::PER_PASS),
          _finalizeSet(Renderer::DescriptorSetSlot::PER_PASS), _rasterSet(Renderer::DescriptorSetSlot::PER_PASS)
    {
        Renderer::ComputeShaderDesc shader;
        Renderer::ComputePipelineDesc computeDesc;
        auto createCompute = [&](const char* path, StringUtils::StringHash hash, const char* name) {
            shader.shaderEntry = gameRenderer->GetShaderEntry(hash, path);
            computeDesc.computeShader = renderer->LoadShader(shader);
            computeDesc.debugName = name;
            return renderer->CreatePipeline(computeDesc);
        };
        _beginPipeline = createCompute("Model/TransparentBegin.cs", "Model/TransparentBegin.cs"_h,
                                       "Model Transparent Begin");
        _expandPipeline = createCompute("Model/TransparentExpand.cs", "Model/TransparentExpand.cs"_h,
                                        "Model Transparent Expand");
        _expandFinalizePipeline = createCompute("Model/TransparentExpandFinalize.cs",
                                                "Model/TransparentExpandFinalize.cs"_h,
                                                "Model Transparent Expand Finalize");
        _cullPipeline = createCompute("Model/TransparentCull.cs", "Model/TransparentCull.cs"_h,
                                      "Model Transparent Cull");
        _finalizePipeline = createCompute("Model/TransparentFinalize.cs", "Model/TransparentFinalize.cs"_h,
                                          "Model Transparent Finalize");

        _beginSet.RegisterPipeline(renderer, _beginPipeline);
        _expandSet.RegisterPipeline(renderer, _expandPipeline);
        _expandFinalizeSet.RegisterPipeline(renderer, _expandFinalizePipeline);
        _cullSet.RegisterPipeline(renderer, _cullPipeline);
        _finalizeSet.RegisterPipeline(renderer, _finalizePipeline);
        _beginSet.Init(renderer);
        _expandSet.Init(renderer);
        _expandFinalizeSet.Init(renderer);
        _cullSet.Init(renderer);
        _cullSet.Bind("_transparentDepthSampler"_h, DepthPyramidUtils::_pyramidSampler);
        _finalizeSet.Init(renderer);

        Renderer::MeshShaderDesc meshShader;
        meshShader.shaderEntry = gameRenderer->GetShaderEntry("Model/Transparent.ms"_h, "Model/Transparent.ms");
        const Renderer::MeshShaderID loadedMeshShader = renderer->LoadShader(meshShader);
        const MaterialLoading::MaterialProgramLibrary& library =
            gameRenderer->GetRenderAssetResources()->GetMaterialProgramLibrary();
        for (u32 bin = 0; bin < ModelView::MODEL_TRANSPARENT_BIN_COUNT; ++bin)
        {
            const u16 executionGroup = ModelView::TransparentExecutionGroup(bin);
            const FileFormat::Material::MaterialExecutionGroup* group = library.GetExecutionGroup(executionGroup);
            if (!group)
                continue;
            Renderer::PixelShaderDesc pixelShader;
            pixelShader.shaderEntry = gameRenderer->GetShaderEntry(group->forwardShaderPermutationHash,
                                                                    "Generated/MaterialGroupsForward.ps");
            Renderer::GraphicsPipelineDesc desc;
            desc.debugName = "Model Transparent Group " + std::to_string(executionGroup) +
                             ((bin & 1u) != 0u ? " Two Sided" : " One Sided");
            desc.shaderStages = Renderer::MeshPipelineStages{.meshShader = loadedMeshShader};
            desc.states.pixelShader = renderer->LoadShader(pixelShader);
            desc.states.depthStencilState.depthEnable = true;
            desc.states.depthStencilState.depthWriteEnable = false;
            desc.states.depthStencilState.depthFunc = Renderer::ComparisonFunc::GREATER;
            desc.states.rasterizerState.cullMode = (bin & 1u) != 0u ? Renderer::CullMode::NONE : Renderer::CullMode::BACK;
            desc.states.rasterizerState.frontFaceMode = Renderer::FrontFaceState::COUNTERCLOCKWISE;
            desc.states.blendState.independentBlendEnable = true;
            auto& accumulation = desc.states.blendState.renderTargets[0];
            accumulation.blendEnable = true;
            accumulation.blendOp = Renderer::BlendOp::ADD;
            accumulation.srcBlend = Renderer::BlendMode::ONE;
            accumulation.destBlend = Renderer::BlendMode::ONE;
            accumulation.srcBlendAlpha = Renderer::BlendMode::ONE;
            accumulation.destBlendAlpha = Renderer::BlendMode::ONE;
            accumulation.blendOpAlpha = Renderer::BlendOp::ADD;
            auto& revealage = desc.states.blendState.renderTargets[1];
            revealage.blendEnable = true;
            revealage.blendOp = Renderer::BlendOp::ADD;
            revealage.srcBlend = Renderer::BlendMode::ZERO;
            revealage.destBlend = Renderer::BlendMode::INV_SRC_ALPHA;
            revealage.srcBlendAlpha = Renderer::BlendMode::ZERO;
            revealage.destBlendAlpha = Renderer::BlendMode::INV_SRC_ALPHA;
            revealage.blendOpAlpha = Renderer::BlendOp::ADD;
            desc.states.renderTargetFormats[0] = Renderer::ImageFormat::R16G16B16A16_FLOAT;
            desc.states.renderTargetFormats[1] = Renderer::ImageFormat::R16_FLOAT;
            desc.states.depthStencilFormat = Renderer::DepthImageFormat::D32_FLOAT;
            _rasterPipelines[bin] = renderer->CreatePipeline(desc);
            _rasterSet.RegisterPipeline(renderer, _rasterPipelines[bin]);

            _activeBins[bin] = true;
        }
        _rasterSet.Init(renderer);
    }

    bool ModelTransparentPass::Bind(Renderer::DescriptorSet& set, StringUtils::StringHash name,
                                    Renderer::BufferID buffer, Renderer::BufferID& current)
    {
        if (buffer == current)
            return false;
        set.Bind(name, buffer);
        current = buffer;
        return true;
    }

    bool ModelTransparentPass::Upload(const ModelView::ModelViewState& viewState,
                                      const ModelView::ModelTransparentWorkResources& work,
                                      const ModelLoading::ModelGeometryStorage& geometry,
                                      const RenderScenes::RenderScene& scene)
    {
        if (_generation != work.GetGeneration())
        {
            _generation = work.GetGeneration();
            _bindings.clear();
        }
        bool changed = false;
        auto bind = [&](Renderer::DescriptorSet& set, const char* name, Renderer::BufferID buffer) {
            const StringUtils::StringHash hash(name);
            const u64 key = (static_cast<u64>(&set == &_beginSet ? 0 : &set == &_expandSet ? 1 :
                                              &set == &_expandFinalizeSet ? 2 : &set == &_cullSet ? 3 :
                                              &set == &_finalizeSet ? 4 : 5) << 32u) | hash.computedHash;
            changed |= Bind(set, hash, buffer, _bindings[key]);
        };
        for (u32 frame = 0; frame < ModelView::MODEL_TRANSPARENT_FRAME_COUNT; ++frame)
        {
            const std::string suffix = std::to_string(frame);
            bind(_beginSet, ("_transparentStats" + suffix).c_str(), work.GetStatsBuffer(frame));
            bind(_beginSet, ("_transparentChunkArguments" + suffix).c_str(), work.GetChunkArguments(frame));
            bind(_beginSet, ("_transparentArguments" + suffix).c_str(), work.GetArguments(frame));
            bind(_expandSet, ("_transparentChunks" + suffix).c_str(), work.GetChunkQueue(frame));
            bind(_expandSet, ("_transparentStats" + suffix).c_str(), work.GetStatsBuffer(frame));
            bind(_expandFinalizeSet, ("_transparentStats" + suffix).c_str(), work.GetStatsBuffer(frame));
            bind(_expandFinalizeSet, ("_transparentChunkArguments" + suffix).c_str(), work.GetChunkArguments(frame));
            bind(_cullSet, ("_transparentChunks" + suffix).c_str(), work.GetChunkQueue(frame));
            bind(_cullSet, ("_transparentRecords" + suffix).c_str(), work.GetVisibilityRecords(frame));
            bind(_cullSet, ("_transparentStats" + suffix).c_str(), work.GetStatsBuffer(frame));
            bind(_finalizeSet, ("_transparentStats" + suffix).c_str(), work.GetStatsBuffer(frame));
            bind(_finalizeSet, ("_transparentArguments" + suffix).c_str(), work.GetArguments(frame));
            bind(_rasterSet, ("_transparentRecords" + suffix).c_str(), work.GetVisibilityRecords(frame));
            bind(_rasterSet, ("_transparentStats" + suffix).c_str(), work.GetStatsBuffer(frame));
        }
        bind(_expandSet, "_transparentViewInputs", viewState.GetInputs().GetBuffer());
        bind(_expandSet, "_transparentLODHistory", viewState.GetLODHistory().GetBuffer());
        bind(_expandSet, "_transparentInstances", scene.GetModelInstances().GetRecords().GetBuffer());
        bind(_expandSet, "_transparentModels", geometry.GetRecords().GetBuffer());
        bind(_expandSet, "_transparentMeshes", geometry.GetMeshes().GetBuffer());
        bind(_expandSet, "_transparentLODs", geometry.GetMeshLODs().GetBuffer());
        bind(_expandSet, "_transparentSubmeshes", geometry.GetSubmeshes().GetBuffer());
        bind(_expandSet, "_transparentGeometryGroups", scene.GetGeometryGroupMasks().GetMasks().GetBuffer());
        bind(_expandSet, "_transparentMaterialTable", scene.GetModelMaterialTables().GetEntries().GetBuffer());
        bind(_cullSet, "_transparentCullInstances", scene.GetModelInstances().GetRecords().GetBuffer());
        bind(_cullSet, "_transparentCullModels", geometry.GetRecords().GetBuffer());
        bind(_cullSet, "_transparentCullMeshlets", geometry.GetMeshlets().GetBuffer());
        bind(_rasterSet, "_transparentRasterInstances", scene.GetModelInstances().GetRecords().GetBuffer());
        bind(_rasterSet, "_transparentRasterModels", geometry.GetRecords().GetBuffer());
        bind(_rasterSet, "_transparentRasterMeshes", geometry.GetMeshes().GetBuffer());
        bind(_rasterSet, "_transparentRasterLODs", geometry.GetMeshLODs().GetBuffer());
        bind(_rasterSet, "_transparentRasterSubmeshes", geometry.GetSubmeshes().GetBuffer());
        bind(_rasterSet, "_transparentRasterMeshlets", geometry.GetMeshlets().GetBuffer());
        bind(_rasterSet, "_transparentRasterPositions", geometry.GetPositions().GetBuffer());
        bind(_rasterSet, "_transparentRasterVertexAttributes", geometry.GetVertexAttributes().GetBuffer());
        bind(_rasterSet, "_transparentRasterVertexIndices", geometry.GetMeshletVertexIndices().GetBuffer());
        bind(_rasterSet, "_transparentRasterTriangles", geometry.GetMeshletTriangles().GetBuffer());
        bind(_rasterSet, "_transparentRasterMaterialTable", scene.GetModelMaterialTables().GetEntries().GetBuffer());
        if (changed)
            _descriptorWarmupFrames = _renderer->GetFrameIndexCount();
        else if (_descriptorWarmupFrames > 0)
            --_descriptorWarmupFrames;
        return _descriptorWarmupFrames == 0;
    }

    void ModelTransparentPass::AddCullPass(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                                           const RenderScenes::RenderView& view,
                                           const ModelView::ModelViewState& viewState,
                                           ModelView::ModelTransparentWorkResources& work,
                                           const ModelLoading::ModelGeometryStorage& geometry,
                                           const MaterialLoading::MaterialStorage& materials,
                                           const RenderScenes::RenderScene& scene, u8 frameIndex)
    {
        const u32 inputCount = viewState.GetInputs().Count();
        struct Data
        {
            Renderer::ImageResource depthPyramid;
            Renderer::BufferMutableResource stats;
            Renderer::BufferMutableResource chunks;
            Renderer::BufferMutableResource chunkArguments;
            Renderer::BufferMutableResource records;
            Renderer::BufferMutableResource arguments;
            Renderer::BufferMutableResource readback;
            Renderer::DescriptorSetResource globalSet;
            Renderer::DescriptorSetResource materialSet;
            Renderer::DescriptorSetResource beginSet;
            Renderer::DescriptorSetResource expandSet;
            Renderer::DescriptorSetResource expandFinalizeSet;
            Renderer::DescriptorSetResource cullSet;
            Renderer::DescriptorSetResource finalizeSet;
        };
        renderGraph->AddPass<Data>("Model Transparent Work: " + view.GetDebugName(),
            [this, &resources, &view, &viewState, &work, &geometry, &materials, &scene, frameIndex, inputCount](Data& data,
                Renderer::RenderGraphBuilder& builder) {
                using Usage = Renderer::BufferPassUsage;
                data.depthPyramid = builder.Read(view.GetDepthPyramidTarget(), Renderer::PipelineType::COMPUTE);
                builder.Read(resources.cameras.GetBuffer(), Usage::COMPUTE);
                builder.Read(viewState.GetInputs().GetBuffer(), Usage::COMPUTE);
                builder.Read(viewState.GetLODHistory().GetBuffer(), Usage::COMPUTE);
                builder.Read(scene.GetModelInstances().GetRecords().GetBuffer(), Usage::COMPUTE);
                builder.Read(geometry.GetRecords().GetBuffer(), Usage::COMPUTE);
                builder.Read(geometry.GetMeshes().GetBuffer(), Usage::COMPUTE);
                builder.Read(geometry.GetMeshLODs().GetBuffer(), Usage::COMPUTE);
                builder.Read(geometry.GetSubmeshes().GetBuffer(), Usage::COMPUTE);
                builder.Read(geometry.GetMeshlets().GetBuffer(), Usage::COMPUTE);
                builder.Read(scene.GetGeometryGroupMasks().GetMasks().GetBuffer(), Usage::COMPUTE);
                builder.Read(scene.GetModelMaterialTables().GetEntries().GetBuffer(), Usage::COMPUTE);
                builder.Read(materials.GetMaterialInstances().GetBuffer(), Usage::COMPUTE);
                builder.Read(materials.GetMaterials().GetBuffer(), Usage::COMPUTE);
                data.stats = builder.Write(work.GetStatsBuffer(frameIndex), Usage::COMPUTE | Usage::TRANSFER);
                data.chunks = builder.Write(work.GetChunkQueue(frameIndex), Usage::COMPUTE);
                data.chunkArguments = builder.Write(work.GetChunkArguments(frameIndex), Usage::COMPUTE);
                data.records = builder.Write(work.GetVisibilityRecords(frameIndex), Usage::COMPUTE);
                data.arguments = builder.Write(work.GetArguments(frameIndex), Usage::COMPUTE);
                data.readback = builder.Write(work.GetStatsReadback(frameIndex), Usage::TRANSFER);
                const u8 inactive = !frameIndex;
                builder.Write(work.GetStatsBuffer(inactive), Usage::COMPUTE);
                builder.Write(work.GetChunkQueue(inactive), Usage::COMPUTE);
                builder.Write(work.GetChunkArguments(inactive), Usage::COMPUTE);
                builder.Write(work.GetVisibilityRecords(inactive), Usage::COMPUTE);
                builder.Write(work.GetArguments(inactive), Usage::COMPUTE);
                data.globalSet = builder.Use(resources.globalDescriptorSet);
                data.materialSet = builder.Use(resources.materialDescriptorSet);
                data.beginSet = builder.Use(_beginSet);
                data.expandSet = builder.Use(_expandSet);
                data.expandFinalizeSet = builder.Use(_expandFinalizeSet);
                data.cullSet = builder.Use(_cullSet);
                data.finalizeSet = builder.Use(_finalizeSet);
                return true;
            },
            [this, &view, &work, inputCount, frameIndex](Data& data, Renderer::RenderGraphResources& graphResources,
                Renderer::CommandList& commandList) {
                GPU_SCOPED_PROFILER_ZONE(commandList, ModelTransparentWork);
                struct BeginConstants { u32 resourceIndex; };
                BeginConstants* begin = graphResources.FrameNew<BeginConstants>();
                begin->resourceIndex = frameIndex;
                commandList.BeginPipeline(_beginPipeline);
                commandList.PushConstant(begin, 0, sizeof(*begin));
                commandList.BindDescriptorSet(data.beginSet, frameIndex);
                commandList.Dispatch(1, 1, 1);
                commandList.EndPipeline(_beginPipeline);
                commandList.BufferBarrier(data.stats, Renderer::BufferPassUsage::COMPUTE);

                if (inputCount > 0)
                {
                    struct ExpandConstants { u32 viewIndex; u32 inputCount; u32 chunkCapacity; u32 resourceIndex; };
                    ExpandConstants* expand = graphResources.FrameNew<ExpandConstants>();
                    *expand = {view.GetCameraIndex(), inputCount, work.GetChunkCapacity(), frameIndex};
                    commandList.BeginPipeline(_expandPipeline);
                    commandList.PushConstant(expand, 0, sizeof(*expand));
                    commandList.BindDescriptorSet(data.globalSet, frameIndex);
                    commandList.BindDescriptorSet(data.materialSet, frameIndex);
                    commandList.BindDescriptorSet(data.expandSet, frameIndex);
                    commandList.Dispatch(inputCount, 1, 1);
                    commandList.EndPipeline(_expandPipeline);
                    commandList.BufferBarrier(data.stats, Renderer::BufferPassUsage::COMPUTE);
                    commandList.BufferBarrier(data.chunks, Renderer::BufferPassUsage::COMPUTE);

                    struct ExpandFinalizeConstants { u32 chunkCapacity; u32 resourceIndex; };
                    ExpandFinalizeConstants* expandFinalize = graphResources.FrameNew<ExpandFinalizeConstants>();
                    *expandFinalize = {work.GetChunkCapacity(), frameIndex};
                    commandList.BeginPipeline(_expandFinalizePipeline);
                    commandList.PushConstant(expandFinalize, 0, sizeof(*expandFinalize));
                    commandList.BindDescriptorSet(data.expandFinalizeSet, frameIndex);
                    commandList.Dispatch(1, 1, 1);
                    commandList.EndPipeline(_expandFinalizePipeline);
                    commandList.BufferBarrier(data.chunkArguments, Renderer::BufferPassUsage::COMPUTE);

                    CVarSystem* cvars = CVarSystem::Get();
                    struct CullConstants
                    {
                        u32 viewIndex;
                        u32 resourceIndex;
                        u32 binCapacity;
                        u32 enableConeCulling;
                        u32 enableOcclusionCulling;
                        u32 viewportWidth;
                        u32 viewportHeight;
                    };
                    CullConstants* cull = graphResources.FrameNew<CullConstants>();
                    *cull = {view.GetCameraIndex(), frameIndex, work.GetBinCapacity(),
                             *cvars->GetIntCVar(CVarCategory::Client | CVarCategory::Rendering,
                                               "modelMeshletConeCulling") != 0 ? 1u : 0u,
                             *cvars->GetIntCVar(CVarCategory::Client | CVarCategory::Rendering,
                                               "modelMeshletOcclusionCulling") != 0 ? 1u : 0u,
                             view.GetDimensions().x, view.GetDimensions().y};
                    data.cullSet.Bind("_transparentDepthPyramid"_h, data.depthPyramid);
                    commandList.BeginPipeline(_cullPipeline);
                    commandList.PushConstant(cull, 0, sizeof(*cull));
                    commandList.BindDescriptorSet(data.globalSet, frameIndex);
                    commandList.BindDescriptorSet(data.cullSet, frameIndex);
                    commandList.DispatchIndirect(data.chunkArguments, 0);
                    commandList.EndPipeline(_cullPipeline);
                    commandList.BufferBarrier(data.stats, Renderer::BufferPassUsage::COMPUTE);
                    commandList.BufferBarrier(data.records, Renderer::BufferPassUsage::COMPUTE);
                }

                struct FinalizeConstants { u32 resourceIndex; u32 binCapacity; };
                FinalizeConstants* finalize = graphResources.FrameNew<FinalizeConstants>();
                *finalize = {frameIndex, work.GetBinCapacity()};
                commandList.BeginPipeline(_finalizePipeline);
                commandList.PushConstant(finalize, 0, sizeof(*finalize));
                commandList.BindDescriptorSet(data.finalizeSet, frameIndex);
                commandList.Dispatch(1, 1, 1);
                commandList.EndPipeline(_finalizePipeline);
                commandList.CopyBuffer(data.readback, 0, data.stats, 0, sizeof(ModelView::TransparentWorkStats));
            });
        work.MarkSubmitted(frameIndex);
    }

    void ModelTransparentPass::AddRasterPass(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                                             const RenderScenes::RenderView& view,
                                             const ModelView::ModelTransparentWorkResources& work,
                                             const ModelLoading::ModelGeometryStorage& geometry,
                                             const MaterialLoading::MaterialStorage& materials,
                                             const RenderScenes::RenderScene& scene, u8 frameIndex)
    {
        struct Data
        {
            Renderer::ImageMutableResource accumulation;
            Renderer::ImageMutableResource revealage;
            Renderer::DepthImageMutableResource depth;
            Renderer::ImageResource ambientVisibility;
            Renderer::ImageResource svsmPagePool;
            Renderer::ImageResource svsmDynamicPagePool;
            Renderer::BufferResource arguments;
            Renderer::DescriptorSetResource globalSet;
            Renderer::DescriptorSetResource lightSet;
            Renderer::DescriptorSetResource materialSet;
            Renderer::DescriptorSetResource rasterSet;
        };
        renderGraph->AddPass<Data>("Model Transparent Raster: " + view.GetDebugName(),
            [this, &resources, &view, &work, &geometry, &materials, &scene, frameIndex](Data& data,
                Renderer::RenderGraphBuilder& builder) {
                using Usage = Renderer::BufferPassUsage;
                const Renderer::LoadMode loadMode = view.ShouldClearTargets() ? Renderer::LoadMode::CLEAR : Renderer::LoadMode::LOAD;
                data.accumulation = builder.Write(view.GetTransparencyAccumulationTarget(), Renderer::PipelineType::GRAPHICS, loadMode);
                data.revealage = builder.Write(view.GetTransparencyRevealageTarget(), Renderer::PipelineType::GRAPHICS, loadMode);
                data.depth = builder.Write(view.GetDepthTarget(), Renderer::PipelineType::GRAPHICS,
                                           Renderer::LoadMode::LOAD);
                data.ambientVisibility = builder.Read(resources.ssaoTarget, Renderer::PipelineType::GRAPHICS);
                builder.Read(resources.cameras.GetBuffer(), Usage::GRAPHICS);
                builder.Read(_gameRenderer->GetMaterialRenderer()->GetDirectionalLightBuffer(), Usage::GRAPHICS);
                ShadowRenderer* shadows = _gameRenderer->GetShadowRenderer();
                data.svsmPagePool = builder.Read(shadows->GetSVSMPagePoolOrPlaceholder(), Renderer::PipelineType::GRAPHICS);
                data.svsmDynamicPagePool = builder.Read(shadows->GetSVSMDynamicPagePoolOrPlaceholder(), Renderer::PipelineType::GRAPHICS);
                builder.Read(shadows->GetSVSMDataBuffer(), Usage::GRAPHICS);
                builder.Read(shadows->GetSVSMPageTableBuffer(), Usage::GRAPHICS);
                builder.Read(shadows->GetSVSMDynamicPageTableBuffer(), Usage::GRAPHICS);
                for (u32 frame = 0; frame < ModelView::MODEL_TRANSPARENT_FRAME_COUNT; ++frame)
                {
                    builder.Read(work.GetVisibilityRecords(frame), Usage::GRAPHICS);
                    builder.Read(work.GetStatsBuffer(frame), Usage::GRAPHICS);
                }
                data.arguments = builder.Read(work.GetArguments(frameIndex), Usage::GRAPHICS);
                builder.Read(scene.GetModelInstances().GetRecords().GetBuffer(), Usage::GRAPHICS);
                builder.Read(geometry.GetRecords().GetBuffer(), Usage::GRAPHICS);
                builder.Read(geometry.GetMeshes().GetBuffer(), Usage::GRAPHICS);
                builder.Read(geometry.GetMeshLODs().GetBuffer(), Usage::GRAPHICS);
                builder.Read(geometry.GetSubmeshes().GetBuffer(), Usage::GRAPHICS);
                builder.Read(geometry.GetMeshlets().GetBuffer(), Usage::GRAPHICS);
                builder.Read(geometry.GetPositions().GetBuffer(), Usage::GRAPHICS);
                builder.Read(geometry.GetVertexAttributes().GetBuffer(), Usage::GRAPHICS);
                builder.Read(geometry.GetMeshletVertexIndices().GetBuffer(), Usage::GRAPHICS);
                builder.Read(geometry.GetMeshletTriangles().GetBuffer(), Usage::GRAPHICS);
                builder.Read(scene.GetModelMaterialTables().GetEntries().GetBuffer(), Usage::GRAPHICS);
                builder.Read(materials.GetMaterialInstances().GetBuffer(), Usage::GRAPHICS);
                builder.Read(materials.GetMaterials().GetBuffer(), Usage::GRAPHICS);
                builder.Read(materials.GetParameterStorage().GetBuffer().GetBuffer(), Usage::GRAPHICS);
                builder.Read(materials.GetTextureIndices().GetBuffer(), Usage::GRAPHICS);
                builder.Read(materials.GetSamplerIDs().GetBuffer(), Usage::GRAPHICS);
                data.globalSet = builder.Use(resources.globalDescriptorSet);
                data.lightSet = builder.Use(resources.lightDescriptorSet);
                data.materialSet = builder.Use(resources.materialDescriptorSet);
                data.rasterSet = builder.Use(_rasterSet);
                return true;
            },
            [this, &view, &work, frameIndex](Data& data, Renderer::RenderGraphResources& graphResources,
                Renderer::CommandList& commandList) {
                GPU_SCOPED_PROFILER_ZONE(commandList, ModelTransparentRaster);
                data.rasterSet.Bind("_ambientVisibility"_h, data.ambientVisibility);
                Renderer::RenderPassDesc pass;
                graphResources.InitializeRenderPassDesc(pass);
                pass.renderTargets[0] = data.accumulation;
                pass.renderTargets[1] = data.revealage;
                pass.depthStencil = data.depth;
                const uvec2 dimensions = view.GetDimensions();
                commandList.SetViewport(0, 0, static_cast<f32>(dimensions.x), static_cast<f32>(dimensions.y), 0.0f, 1.0f);
                commandList.SetScissorRect(0, dimensions.x, 0, dimensions.y);
                commandList.BeginRenderPass(pass);
                CVarSystem* cvars = CVarSystem::Get();
                const f32 shadowStrength = static_cast<f32>(*cvars->GetFloatCVar(
                    CVarCategory::Client | CVarCategory::Rendering, "shadowStrength"));
                ShadowRenderer* shadows = _gameRenderer->GetShadowRenderer();
                const bool shadowsReady = view.UsesWorldShadows() &&
                    *cvars->GetIntCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowEnabled") != 0 &&
                    shadowStrength > 0.0f && shadows->GetSVSMPagePool() != Renderer::ImageID::Invalid();
                struct Constants
                {
                    vec4 shadowSettings;
                    vec4 fogColor;
                    vec4 fogSettings;
                    u32 viewIndex;
                    u32 numDirectionalLights;
                    u32 shadowsReady;
                    u32 resourceIndex;
                    u32 binIndex;
                    u32 binCapacity;
                };
                for (u32 bin = 0; bin < ModelView::MODEL_TRANSPARENT_BIN_COUNT; ++bin)
                {
                    if (!_activeBins[bin])
                        continue;
                    Constants* constants = graphResources.FrameNew<Constants>();
                    constants->shadowSettings = vec4(shadowStrength,
                        *cvars->GetFloatCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowNormalOffsetBias"),
                        *cvars->GetFloatCVar(CVarCategory::Client | CVarCategory::Rendering, "svsmConstantBias"), 0.0f);
                    constants->fogColor = _gameRenderer->GetMaterialRenderer()->GetFogColor();
                    constants->fogSettings = _gameRenderer->GetMaterialRenderer()->GetFogSettings();
                    constants->viewIndex = view.GetCameraIndex();
                    constants->numDirectionalLights = _gameRenderer->GetMaterialRenderer()->GetNumDirectionalLights();
                    constants->shadowsReady = shadowsReady ? 1u : 0u;
                    constants->resourceIndex = frameIndex;
                    constants->binIndex = bin;
                    constants->binCapacity = work.GetBinCapacity();
                    commandList.BeginPipeline(_rasterPipelines[bin]);
                    commandList.PushConstant(constants, 0, sizeof(*constants));
                    commandList.BindDescriptorSet(data.globalSet, frameIndex);
                    commandList.BindDescriptorSet(data.lightSet, frameIndex);
                    commandList.BindDescriptorSet(data.materialSet, frameIndex);
                    commandList.BindDescriptorSet(data.rasterSet, frameIndex);
                    commandList.DrawMeshTasksIndirect(data.arguments,
                        bin * sizeof(u32) * ModelView::MODEL_TRANSPARENT_DISPATCH_ARGUMENT_COUNT);
                    commandList.EndPipeline(_rasterPipelines[bin]);
                }
                commandList.EndRenderPass(pass);
            });
    }
} // namespace ModelPipeline
