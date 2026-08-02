#include "ModelViewWorkPass.h"

#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Material/MaterialStorage.h"
#include "Game-Lib/Rendering/Model/Asset/ModelGeometryStorage.h"
#include "Game-Lib/Rendering/Model/View/ModelViewState.h"
#include "Game-Lib/Rendering/Model/View/ModelViewWorkResources.h"
#include "Game-Lib/Rendering/RenderResources.h"
#include "Game-Lib/Rendering/Scene/RenderScene.h"
#include "Game-Lib/Rendering/Scene/RenderView.h"

#include <algorithm>
#include <Base/CVarSystem/CVarSystem.h>
#include <Renderer/Descriptors/ComputeShaderDesc.h>
#include <Renderer/RenderGraph.h>
#include <Renderer/Renderer.h>

AutoCVar_Int CVAR_ModelMeshletQueueLimit(
    CVarCategory::Client | CVarCategory::Rendering, "modelMeshletQueueLimit",
    "Limit model meshlet queue capacity for overflow testing (0 automatic)", 0);

namespace ModelPipeline
{
    ModelViewWorkPass::ModelViewWorkPass(Renderer::Renderer* renderer, GameRenderer* gameRenderer)
        : _renderer(renderer), _expandDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS),
          _expandFinalizeDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS),
          _cullDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS),
          _finalizeDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS)
    {
        Renderer::ComputePipelineDesc desc;
        Renderer::ComputeShaderDesc shader;
        shader.shaderEntry = gameRenderer->GetShaderEntry("Model/ViewWork.cs"_h, "Model/ViewWork.cs");
        desc.debugName = "Model View Instance Expansion";
        desc.computeShader = renderer->LoadShader(shader);
        _expandPipeline = renderer->CreatePipeline(desc);

        shader.shaderEntry =
            gameRenderer->GetShaderEntry("Model/ViewWorkExpandFinalize.cs"_h, "Model/ViewWorkExpandFinalize.cs");
        desc.debugName = "Model View Expansion Finalize";
        desc.computeShader = renderer->LoadShader(shader);
        _expandFinalizePipeline = renderer->CreatePipeline(desc);

        shader.shaderEntry = gameRenderer->GetShaderEntry("Model/ViewWorkCull.cs"_h, "Model/ViewWorkCull.cs");
        desc.debugName = "Model View Meshlet Cull";
        desc.computeShader = renderer->LoadShader(shader);
        _cullPipeline = renderer->CreatePipeline(desc);

        shader.shaderEntry = gameRenderer->GetShaderEntry("Model/ViewWorkFinalize.cs"_h, "Model/ViewWorkFinalize.cs");
        desc.debugName = "Model View Work Finalize";
        desc.computeShader = renderer->LoadShader(shader);
        _finalizePipeline = renderer->CreatePipeline(desc);

        _expandDescriptorSet.RegisterPipeline(renderer, _expandPipeline);
        _expandDescriptorSet.Init(renderer);
        _expandFinalizeDescriptorSet.RegisterPipeline(renderer, _expandFinalizePipeline);
        _expandFinalizeDescriptorSet.Init(renderer);
        _cullDescriptorSet.RegisterPipeline(renderer, _cullPipeline);
        _cullDescriptorSet.Init(renderer);
        _finalizeDescriptorSet.RegisterPipeline(renderer, _finalizePipeline);
        _finalizeDescriptorSet.Init(renderer);
    }

    bool ModelViewWorkPass::Bind(Renderer::DescriptorSet& descriptorSet, StringUtils::StringHash name,
                                 Renderer::BufferID buffer, Renderer::BufferID& current)
    {
        if (buffer != current)
        {
            descriptorSet.Bind(name, buffer);
            current = buffer;
            return true;
        }
        return false;
    }

    bool ModelViewWorkPass::Upload(const ModelView::ModelViewState& viewState, ModelView::ModelViewWorkResources& work,
                                   const ModelLoading::ModelGeometryStorage& geometry,
                                   const MaterialLoading::MaterialStorage& materials,
                                   const RenderScenes::RenderScene& scene)
    {
        const bool queuesRecreated = work.EnsureQueueCapacity(viewState.GetQueueCapacity());
        if (queuesRecreated)
        {
            for (u32 frame = 0; frame < ModelView::MODEL_VIEW_FRAME_COUNT; ++frame)
            {
                _expandBindings.frames[frame].chunkQueue = Renderer::BufferID::Invalid();
                _cullBindings.frames[frame].chunkQueue = Renderer::BufferID::Invalid();
                _cullBindings.frames[frame].oneSidedQueue = Renderer::BufferID::Invalid();
                _cullBindings.frames[frame].twoSidedQueue = Renderer::BufferID::Invalid();
                _cullBindings.frames[frame].visibilityRecords = Renderer::BufferID::Invalid();
            }
        }

        bool bindingsChanged = false;
        auto bind = [&bindingsChanged, this](Renderer::DescriptorSet& set, StringUtils::StringHash name,
                                              Renderer::BufferID buffer, Renderer::BufferID& current) {
            bindingsChanged |= Bind(set, name, buffer, current);
        };
        auto finish = [&]() {
            if (bindingsChanged)
                _descriptorWarmupFrames = _renderer->GetFrameIndexCount();
            else if (_descriptorWarmupFrames > 0)
                --_descriptorWarmupFrames;
            return _descriptorWarmupFrames == 0;
        };

        bind(_expandDescriptorSet, "_chunkQueue0"_h, work.GetChunkQueue(0), _expandBindings.frames[0].chunkQueue);
        bind(_expandDescriptorSet, "_workStats0"_h, work.GetStatsBuffer(0), _expandBindings.frames[0].workStats);
        bind(_expandDescriptorSet, "_chunkQueue1"_h, work.GetChunkQueue(1), _expandBindings.frames[1].chunkQueue);
        bind(_expandDescriptorSet, "_workStats1"_h, work.GetStatsBuffer(1), _expandBindings.frames[1].workStats);

        bind(_expandFinalizeDescriptorSet, "_workStats0"_h, work.GetStatsBuffer(0),
             _expandFinalizeBindings[0].workStats);
        bind(_expandFinalizeDescriptorSet, "_chunkArguments0"_h, work.GetChunkArguments(0),
             _expandFinalizeBindings[0].chunkArguments);
        bind(_expandFinalizeDescriptorSet, "_workStats1"_h, work.GetStatsBuffer(1),
             _expandFinalizeBindings[1].workStats);
        bind(_expandFinalizeDescriptorSet, "_chunkArguments1"_h, work.GetChunkArguments(1),
             _expandFinalizeBindings[1].chunkArguments);

        bind(_cullDescriptorSet, "_chunkQueue0"_h, work.GetChunkQueue(0), _cullBindings.frames[0].chunkQueue);
        bind(_cullDescriptorSet, "_oneSidedQueue0"_h, work.GetQueue(ModelView::MODEL_RASTER_ONE_SIDED, 0),
             _cullBindings.frames[0].oneSidedQueue);
        bind(_cullDescriptorSet, "_twoSidedQueue0"_h, work.GetQueue(ModelView::MODEL_RASTER_TWO_SIDED, 0),
             _cullBindings.frames[0].twoSidedQueue);
        bind(_cullDescriptorSet, "_workStats0"_h, work.GetStatsBuffer(0), _cullBindings.frames[0].workStats);
        bind(_cullDescriptorSet, "_visibilityRecords0"_h, work.GetVisibilityRecords(0),
             _cullBindings.frames[0].visibilityRecords);
        bind(_cullDescriptorSet, "_chunkQueue1"_h, work.GetChunkQueue(1), _cullBindings.frames[1].chunkQueue);
        bind(_cullDescriptorSet, "_oneSidedQueue1"_h, work.GetQueue(ModelView::MODEL_RASTER_ONE_SIDED, 1),
             _cullBindings.frames[1].oneSidedQueue);
        bind(_cullDescriptorSet, "_twoSidedQueue1"_h, work.GetQueue(ModelView::MODEL_RASTER_TWO_SIDED, 1),
             _cullBindings.frames[1].twoSidedQueue);
        bind(_cullDescriptorSet, "_workStats1"_h, work.GetStatsBuffer(1), _cullBindings.frames[1].workStats);
        bind(_cullDescriptorSet, "_visibilityRecords1"_h, work.GetVisibilityRecords(1),
             _cullBindings.frames[1].visibilityRecords);

        bind(_finalizeDescriptorSet, "_workStats0"_h, work.GetStatsBuffer(0), _finalizeBindings[0].workStats);
        bind(_finalizeDescriptorSet, "_indirectArguments0"_h, work.GetArguments(0),
             _finalizeBindings[0].indirectArguments);
        bind(_finalizeDescriptorSet, "_workStats1"_h, work.GetStatsBuffer(1), _finalizeBindings[1].workStats);
        bind(_finalizeDescriptorSet, "_indirectArguments1"_h, work.GetArguments(1),
             _finalizeBindings[1].indirectArguments);

        if (viewState.GetInputs().IsEmpty())
            return finish();

        bind(_expandDescriptorSet, "_viewInputs"_h, viewState.GetInputs().GetBuffer(),
             _expandBindings.viewInputs);
        bind(_expandDescriptorSet, "_lodHistory"_h, viewState.GetLODHistory().GetBuffer(),
             _expandBindings.lodHistory);
        bind(_expandDescriptorSet, "_modelInstances"_h, scene.GetModelInstances().GetRecords().GetBuffer(),
             _expandBindings.modelInstances);
        bind(_expandDescriptorSet, "_modelRecords"_h, geometry.GetRecords().GetBuffer(),
             _expandBindings.modelRecords);
        bind(_expandDescriptorSet, "_modelMeshes"_h, geometry.GetMeshes().GetBuffer(),
             _expandBindings.modelMeshes);
        bind(_expandDescriptorSet, "_modelLODs"_h, geometry.GetMeshLODs().GetBuffer(),
             _expandBindings.modelLODs);
        bind(_expandDescriptorSet, "_modelSubmeshes"_h, geometry.GetSubmeshes().GetBuffer(),
             _expandBindings.modelSubmeshes);
        bind(_expandDescriptorSet, "_geometryGroupMasks"_h, scene.GetGeometryGroupMasks().GetMasks().GetBuffer(),
             _expandBindings.geometryGroupMasks);
        bind(_expandDescriptorSet, "_materialTable"_h, scene.GetModelMaterialTables().GetEntries().GetBuffer(),
             _expandBindings.materialTable);
        bind(_expandDescriptorSet, "_materialInstances"_h, materials.GetMaterialInstances().GetBuffer(),
             _expandBindings.materialInstances);
        bind(_expandDescriptorSet, "_materials"_h, materials.GetMaterials().GetBuffer(),
             _expandBindings.materials);

        bind(_cullDescriptorSet, "_modelInstances"_h, scene.GetModelInstances().GetRecords().GetBuffer(),
             _cullBindings.modelInstances);
        bind(_cullDescriptorSet, "_modelRecords"_h, geometry.GetRecords().GetBuffer(),
             _cullBindings.modelRecords);
        bind(_cullDescriptorSet, "_modelMeshes"_h, geometry.GetMeshes().GetBuffer(),
             _cullBindings.modelMeshes);
        bind(_cullDescriptorSet, "_modelMeshlets"_h, geometry.GetMeshlets().GetBuffer(),
             _cullBindings.modelMeshlets);
        return finish();
    }

    void ModelViewWorkPass::AddPass(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                                    const RenderScenes::RenderView& view, const ModelView::ModelViewState& viewState,
                                    ModelView::ModelViewWorkResources& work,
                                    const ModelLoading::ModelGeometryStorage& geometry,
                                    const MaterialLoading::MaterialStorage& materials,
                                    const RenderScenes::RenderScene& scene, u8 frameIndex, bool resetHistory,
                                    i32 forcedLOD)
    {
        const u32 inputCount = viewState.GetInputs().Count();
        const i32 configuredQueueLimit = CVAR_ModelMeshletQueueLimit.Get();
        const u32 queueCapacity = configuredQueueLimit > 0
                                      ? std::min(work.GetQueueCapacity(), static_cast<u32>(configuredQueueLimit))
                                      : work.GetQueueCapacity();

        struct Data
        {
            Renderer::BufferMutableResource history;
            Renderer::BufferMutableResource chunks;
            Renderer::BufferMutableResource chunkArguments;
            Renderer::BufferMutableResource oneSided;
            Renderer::BufferMutableResource twoSided;
            Renderer::BufferMutableResource visibilityRecords;
            Renderer::BufferMutableResource stats;
            Renderer::BufferMutableResource arguments;
            Renderer::BufferMutableResource readback;
            Renderer::DescriptorSetResource globalSet;
            Renderer::DescriptorSetResource expandSet;
            Renderer::DescriptorSetResource expandFinalizeSet;
            Renderer::DescriptorSetResource cullSet;
            Renderer::DescriptorSetResource finalizeSet;
        };

        renderGraph->AddPass<Data>("Model View Work",
            [&resources, &viewState, &work, &geometry, &materials, &scene, frameIndex, inputCount, this](Data& data, Renderer::RenderGraphBuilder& builder) {
                using Usage = Renderer::BufferPassUsage;
                const u8 inactiveFrameIndex = (frameIndex + 1u) % ModelView::MODEL_VIEW_FRAME_COUNT;
                if (inputCount > 0)
                {
                    builder.Read(resources.cameras.GetBuffer(), Usage::COMPUTE);
                    builder.Read(viewState.GetInputs().GetBuffer(), Usage::COMPUTE);
                    data.history = builder.Write(viewState.GetLODHistory().GetBuffer(), Usage::COMPUTE);
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
                    data.chunks = builder.Write(work.GetChunkQueue(frameIndex), Usage::COMPUTE);
                    data.chunkArguments = builder.Write(work.GetChunkArguments(frameIndex), Usage::COMPUTE);
                    data.oneSided =
                        builder.Write(work.GetQueue(ModelView::MODEL_RASTER_ONE_SIDED, frameIndex), Usage::COMPUTE);
                    data.twoSided =
                        builder.Write(work.GetQueue(ModelView::MODEL_RASTER_TWO_SIDED, frameIndex), Usage::COMPUTE);
                    data.visibilityRecords = builder.Write(work.GetVisibilityRecords(frameIndex), Usage::COMPUTE);
                    // Both generations are RW shader bindings; resourceIndex selects the active generation.
                    builder.Write(work.GetChunkQueue(inactiveFrameIndex), Usage::COMPUTE);
                    builder.Write(work.GetChunkArguments(inactiveFrameIndex), Usage::COMPUTE);
                    builder.Write(work.GetQueue(ModelView::MODEL_RASTER_ONE_SIDED, inactiveFrameIndex),
                                  Usage::COMPUTE);
                    builder.Write(work.GetQueue(ModelView::MODEL_RASTER_TWO_SIDED, inactiveFrameIndex),
                                  Usage::COMPUTE);
                    builder.Write(work.GetVisibilityRecords(inactiveFrameIndex), Usage::COMPUTE);
                    data.globalSet = builder.Use(resources.globalDescriptorSet);
                    data.expandSet = builder.Use(_expandDescriptorSet);
                    data.expandFinalizeSet = builder.Use(_expandFinalizeDescriptorSet);
                    data.cullSet = builder.Use(_cullDescriptorSet);
                }
                data.stats = builder.Write(work.GetStatsBuffer(frameIndex), Usage::COMPUTE | Usage::TRANSFER);
                data.arguments = builder.Write(work.GetArguments(frameIndex), Usage::COMPUTE | Usage::TRANSFER);
                // Both generations are RW shader bindings; resourceIndex selects the active generation.
                builder.Write(work.GetStatsBuffer(!frameIndex), Usage::COMPUTE);
                builder.Write(work.GetArguments(!frameIndex), Usage::COMPUTE);
                data.readback = builder.Write(work.GetStatsReadback(frameIndex), Usage::TRANSFER);
                data.finalizeSet = builder.Use(_finalizeDescriptorSet);
                return true;
            },
            [this, &view, inputCount, queueCapacity, frameIndex, resetHistory, forcedLOD](
                Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) {
                GPU_SCOPED_PROFILER_ZONE(commandList, ModelViewWork);
                commandList.FillBuffer(data.stats, 0, sizeof(ModelView::WorkStats), 0);
                commandList.BufferBarrier(data.stats, Renderer::BufferPassUsage::TRANSFER);

                struct Constants
                {
                    u32 viewIndex;
                    u32 inputCount;
                    u32 queueCapacity;
                    i32 forcedLOD;
                    f32 viewportHeight;
                    f32 lodTargetPixels;
                    f32 lodHysteresis;
                    u32 resetHistory;
                    u32 resourceIndex;
                };
                Constants* constants = graphResources.FrameNew<Constants>();
                constants->viewIndex = view.GetCameraIndex();
                constants->inputCount = inputCount;
                constants->queueCapacity = queueCapacity;
                constants->forcedLOD = forcedLOD;
                constants->viewportHeight = _renderer->GetRenderSize().y;
                constants->lodTargetPixels = 2.0f;
                constants->lodHysteresis = 0.15f;
                constants->resetHistory = resetHistory ? 1u : 0u;
                constants->resourceIndex = frameIndex;

                if (inputCount > 0)
                {
                    commandList.BeginPipeline(_expandPipeline);
                    commandList.PushConstant(constants, 0, sizeof(Constants));
                    commandList.BindDescriptorSet(data.globalSet, frameIndex);
                    commandList.BindDescriptorSet(data.expandSet, frameIndex);
                    commandList.Dispatch(inputCount, 1, 1);
                    commandList.EndPipeline(_expandPipeline);

                    commandList.BufferBarrier(data.chunks, Renderer::BufferPassUsage::COMPUTE);
                    commandList.BufferBarrier(data.stats, Renderer::BufferPassUsage::COMPUTE);
                    struct ExpandFinalizeConstants
                    {
                        u32 queueCapacity;
                        u32 resourceIndex;
                    };
                    ExpandFinalizeConstants* expandFinalizeConstants =
                        graphResources.FrameNew<ExpandFinalizeConstants>();
                    expandFinalizeConstants->queueCapacity = queueCapacity;
                    expandFinalizeConstants->resourceIndex = frameIndex;
                    commandList.BeginPipeline(_expandFinalizePipeline);
                    commandList.PushConstant(expandFinalizeConstants, 0, sizeof(ExpandFinalizeConstants));
                    commandList.BindDescriptorSet(data.expandFinalizeSet, frameIndex);
                    commandList.Dispatch(1, 1, 1);
                    commandList.EndPipeline(_expandFinalizePipeline);

                    commandList.BufferBarrier(data.chunkArguments, Renderer::BufferPassUsage::COMPUTE);
                    struct CullConstants
                    {
                        u32 viewIndex;
                        u32 queueCapacity;
                        u32 resourceIndex;
                    };
                    CullConstants* cullConstants = graphResources.FrameNew<CullConstants>();
                    cullConstants->viewIndex = view.GetCameraIndex();
                    cullConstants->queueCapacity = queueCapacity;
                    cullConstants->resourceIndex = frameIndex;
                    commandList.BeginPipeline(_cullPipeline);
                    commandList.PushConstant(cullConstants, 0, sizeof(CullConstants));
                    commandList.BindDescriptorSet(data.globalSet, frameIndex);
                    commandList.BindDescriptorSet(data.cullSet, frameIndex);
                    commandList.DispatchIndirect(data.chunkArguments, 0);
                    commandList.EndPipeline(_cullPipeline);
                }

                commandList.BufferBarrier(data.stats, Renderer::BufferPassUsage::COMPUTE);
                commandList.BeginPipeline(_finalizePipeline);
                commandList.PushConstant(constants, 0, sizeof(Constants));
                commandList.BindDescriptorSet(data.finalizeSet, frameIndex);
                commandList.Dispatch(1, 1, 1);
                commandList.EndPipeline(_finalizePipeline);
                commandList.BufferBarrier(data.arguments, Renderer::BufferPassUsage::COMPUTE);
                commandList.BufferBarrier(data.stats, Renderer::BufferPassUsage::COMPUTE);
                commandList.CopyBuffer(data.readback, 0, data.stats, 0, sizeof(ModelView::WorkStats));
                commandList.CopyBuffer(data.readback, sizeof(ModelView::WorkStats), data.arguments, 0,
                                       sizeof(u32) * ModelView::MODEL_RASTER_CLASS_COUNT *
                                           ModelView::MODEL_DISPATCH_ARGUMENT_COUNT);
            });
    }
} // namespace ModelPipeline
