#include "ModelViewWorkPass.h"

#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/CullUtils.h"
#include "Game-Lib/Rendering/Material/MaterialStorage.h"
#include "Game-Lib/Rendering/Model/Asset/ModelGeometryStorage.h"
#include "Game-Lib/Rendering/Model/ModelRendererMode.h"
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

namespace
{
    constexpr i32 DEFAULT_MODEL_MESHLET_QUEUE_CAPACITY = 1024 * 1024;
}

AutoCVar_Int CVAR_ModelMeshletQueueLimit(
    CVarCategory::Client | CVarCategory::Rendering, "modelMeshletQueueLimit",
    "Limit model meshlet queue capacity for overflow testing (0 automatic)", 0);
AutoCVar_Int CVAR_ModelMeshletQueueCapacity(
    CVarCategory::Client | CVarCategory::Rendering, "modelMeshletQueueCapacity",
    "Maximum allocated model meshlet queue entries per View", DEFAULT_MODEL_MESHLET_QUEUE_CAPACITY);
AutoCVar_Int CVAR_ModelMeshletConeCulling(
    CVarCategory::Client | CVarCategory::Rendering, "modelMeshletConeCulling",
    "Cull one-sided model meshlets whose normal cones face away from the View", 1, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_ModelMeshletOcclusionCulling(
    CVarCategory::Client | CVarCategory::Rendering, "modelMeshletOcclusionCulling",
    "Cull newly visible model meshlets against the main View depth pyramid", 1, CVarFlags::EditCheckbox);

namespace ModelPipeline
{
    ModelViewWorkPass::ModelViewWorkPass(Renderer::Renderer* renderer, GameRenderer* gameRenderer)
        : _renderer(renderer), _expandDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS),
          _selectDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS),
          _selectFinalizeDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS),
          _expandFinalizeDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS),
          _cullDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS),
          _finalizeDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS),
          _historyClearDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS)
    {
        Renderer::ComputePipelineDesc desc;
        Renderer::ComputeShaderDesc shader;
        shader.shaderEntry = gameRenderer->GetShaderEntry("Model/ViewWorkSelect.cs"_h, "Model/ViewWorkSelect.cs");
        desc.debugName = "Model View Instance Selection";
        desc.computeShader = renderer->LoadShader(shader);
        _selectPipeline = renderer->CreatePipeline(desc);

        shader.shaderEntry = gameRenderer->GetShaderEntry("Model/ViewWorkSelectFinalize.cs"_h,
                                                           "Model/ViewWorkSelectFinalize.cs");
        desc.debugName = "Model View Instance Selection Finalize";
        desc.computeShader = renderer->LoadShader(shader);
        _selectFinalizePipeline = renderer->CreatePipeline(desc);

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

        _selectDescriptorSet.RegisterPipeline(renderer, _selectPipeline);
        _selectDescriptorSet.Init(renderer);
        _selectFinalizeDescriptorSet.RegisterPipeline(renderer, _selectFinalizePipeline);
        _selectFinalizeDescriptorSet.Init(renderer);
        _expandDescriptorSet.RegisterPipeline(renderer, _expandPipeline);
        _expandDescriptorSet.Init(renderer);
        _expandFinalizeDescriptorSet.RegisterPipeline(renderer, _expandFinalizePipeline);
        _expandFinalizeDescriptorSet.Init(renderer);
        _cullDescriptorSet.RegisterPipeline(renderer, _cullPipeline);
        _cullDescriptorSet.Init(renderer);
        _cullDescriptorSet.Bind("_depthSampler", DepthPyramidUtils::_pyramidSampler);
        _finalizeDescriptorSet.RegisterPipeline(renderer, _finalizePipeline);
        _finalizeDescriptorSet.Init(renderer);
        shader.shaderEntry = gameRenderer->GetShaderEntry("Model/ViewHistoryClear.cs"_h, "Model/ViewHistoryClear.cs");
        desc.debugName = "Model View Targeted History Clear";
        desc.computeShader = renderer->LoadShader(shader);
        _historyClearPipeline = renderer->CreatePipeline(desc);
        _historyClearDescriptorSet.RegisterPipeline(renderer, _historyClearPipeline);
        _historyClearDescriptorSet.Init(renderer);
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

    void ModelViewWorkPass::PrepareResources(const ModelView::ModelViewState& viewState,
                                             ModelView::ModelViewWorkResources& work,
                                             const RenderScenes::RenderScene& scene)
    {
        const u32 configuredCapacity = static_cast<u32>(std::clamp(CVAR_ModelMeshletQueueCapacity.Get(), 1,
            static_cast<i32>(ModelView::TYPED_VISIBILITY_RECORD_CAPACITY)));
        const bool queuesRecreated =
            work.EnsureQueueCapacity(std::min(viewState.GetQueueCapacity(), configuredCapacity));
        const bool selectionRecreated = work.EnsureSelectionCapacity(viewState.GetDispatchInputCount());
        const bool cullReasonsRecreated = work.EnsureCullReasonCapacity(ModelRendering::ShowModelCullReasons() ? work.GetQueueCapacity() : 1u);
        const RenderScenes::RenderSceneStats sceneStats = scene.GetStats();
        const bool historyRecreated = work.EnsureHistoryCapacity(sceneStats.instances.slotCapacity,
                                                                  sceneStats.meshletHistory.addressSpaceWords);
        if (queuesRecreated)
        {
            for (u32 frame = 0; frame < ModelView::MODEL_VIEW_FRAME_COUNT; ++frame)
            {
                _expandBindings.frames[frame].chunkQueue = Renderer::BufferID::Invalid();
                _cullBindings.frames[frame].chunkQueue = Renderer::BufferID::Invalid();
                for (Renderer::BufferID& queue : _cullBindings.frames[frame].rasterQueues)
                    queue = Renderer::BufferID::Invalid();
                for (Renderer::BufferID& queue : _cullBindings.frames[frame].geometryQueues)
                    queue = Renderer::BufferID::Invalid();
                _cullBindings.frames[frame].visibilityRecords = Renderer::BufferID::Invalid();
            }
        }
        if (selectionRecreated)
        {
            for (u32 frame = 0; frame < ModelView::MODEL_VIEW_FRAME_COUNT; ++frame)
            {
                _selectBindings.selectedInputs[frame] = Renderer::BufferID::Invalid();
                _expandBindings.frames[frame].selectedInputs = Renderer::BufferID::Invalid();
            }
        }
        if (historyRecreated)
        {
            for (u32 frame = 0; frame < ModelView::MODEL_VIEW_FRAME_COUNT; ++frame)
            {
                _cullBindings.frames[frame].meshletHistory = Renderer::BufferID::Invalid();
            }
        }
        if (cullReasonsRecreated)
        {
            for (u32 frame = 0; frame < ModelView::MODEL_VIEW_FRAME_COUNT; ++frame)
                _cullBindings.frames[frame].cullReasons = Renderer::BufferID::Invalid();
        }
    }

    bool ModelViewWorkPass::Upload(const ModelView::ModelViewState& viewState, ModelView::ModelViewWorkResources& work,
                                   const ModelLoading::ModelGeometryStorage& geometry,
                                   const RenderScenes::RenderScene& scene)
    {

        auto Finish = [&]() {
            return !_selectDescriptorSet.HasPendingBufferWrites() &&
                   !_selectFinalizeDescriptorSet.HasPendingBufferWrites() &&
                   !_expandDescriptorSet.HasPendingBufferWrites() &&
                   !_expandFinalizeDescriptorSet.HasPendingBufferWrites() &&
                   !_cullDescriptorSet.HasPendingBufferWrites() &&
                   !_finalizeDescriptorSet.HasPendingBufferWrites() &&
                   !_historyClearDescriptorSet.HasPendingBufferWrites();
        };

        Bind(_selectDescriptorSet, "_selectedInputs0"_h, work.GetSelectionQueue(0), _selectBindings.selectedInputs[0]);
        Bind(_selectDescriptorSet, "_selectedInputs1"_h, work.GetSelectionQueue(1), _selectBindings.selectedInputs[1]);
        Bind(_selectDescriptorSet, "_workStats0"_h, work.GetStatsBuffer(0), _selectBindings.workStats[0]);
        Bind(_selectDescriptorSet, "_workStats1"_h, work.GetStatsBuffer(1), _selectBindings.workStats[1]);
        Bind(_selectFinalizeDescriptorSet, "_workStats0"_h, work.GetStatsBuffer(0),
             _selectFinalizeBindings[0].workStats);
        Bind(_selectFinalizeDescriptorSet, "_selectionArguments0"_h, work.GetSelectionArguments(0),
             _selectFinalizeBindings[0].selectionArguments);
        Bind(_selectFinalizeDescriptorSet, "_workStats1"_h, work.GetStatsBuffer(1),
             _selectFinalizeBindings[1].workStats);
        Bind(_selectFinalizeDescriptorSet, "_selectionArguments1"_h, work.GetSelectionArguments(1),
             _selectFinalizeBindings[1].selectionArguments);

        Bind(_expandDescriptorSet, "_selectedInputs0"_h, work.GetSelectionQueue(0),
             _expandBindings.frames[0].selectedInputs);
        Bind(_expandDescriptorSet, "_selectedInputs1"_h, work.GetSelectionQueue(1),
             _expandBindings.frames[1].selectedInputs);
        Bind(_expandDescriptorSet, "_chunkQueue0"_h, work.GetChunkQueue(0), _expandBindings.frames[0].chunkQueue);
        Bind(_expandDescriptorSet, "_workStats0"_h, work.GetStatsBuffer(0), _expandBindings.frames[0].workStats);
        Bind(_expandDescriptorSet, "_chunkQueue1"_h, work.GetChunkQueue(1), _expandBindings.frames[1].chunkQueue);
        Bind(_expandDescriptorSet, "_workStats1"_h, work.GetStatsBuffer(1), _expandBindings.frames[1].workStats);
        Bind(_historyClearDescriptorSet, "_clearMeshletHistory0"_h, work.GetMeshletHistory(0), _historyClearMeshletHistory[0]);
        Bind(_historyClearDescriptorSet, "_clearMeshletHistory1"_h, work.GetMeshletHistory(1), _historyClearMeshletHistory[1]);

        Bind(_expandFinalizeDescriptorSet, "_workStats0"_h, work.GetStatsBuffer(0),
             _expandFinalizeBindings[0].workStats);
        Bind(_expandFinalizeDescriptorSet, "_chunkArguments0"_h, work.GetChunkArguments(0),
             _expandFinalizeBindings[0].chunkArguments);
        Bind(_expandFinalizeDescriptorSet, "_workStats1"_h, work.GetStatsBuffer(1),
             _expandFinalizeBindings[1].workStats);
        Bind(_expandFinalizeDescriptorSet, "_chunkArguments1"_h, work.GetChunkArguments(1),
             _expandFinalizeBindings[1].chunkArguments);

        Bind(_cullDescriptorSet, "_chunkQueue0"_h, work.GetChunkQueue(0), _cullBindings.frames[0].chunkQueue);
        static constexpr StringUtils::StringHash QUEUE_NAMES[ModelView::MODEL_VIEW_FRAME_COUNT]
                                                                 [ModelView::MODEL_RASTER_CLASS_COUNT] = {
            {"_oneSidedQueue0"_h, "_twoSidedQueue0"_h, "_alphaTestOneSidedQueue0"_h, "_alphaTestTwoSidedQueue0"_h},
            {"_oneSidedQueue1"_h, "_twoSidedQueue1"_h, "_alphaTestOneSidedQueue1"_h, "_alphaTestTwoSidedQueue1"_h}};
        static constexpr StringUtils::StringHash GEOMETRY_QUEUE_NAMES[ModelView::MODEL_VIEW_FRAME_COUNT]
                                                                          [ModelView::MODEL_RASTER_CLASS_COUNT] = {
            {"_geometryOneSidedQueue0"_h, "_geometryTwoSidedQueue0"_h,
             "_geometryAlphaTestOneSidedQueue0"_h, "_geometryAlphaTestTwoSidedQueue0"_h},
            {"_geometryOneSidedQueue1"_h, "_geometryTwoSidedQueue1"_h,
             "_geometryAlphaTestOneSidedQueue1"_h, "_geometryAlphaTestTwoSidedQueue1"_h}};
        for (u32 frame = 0; frame < ModelView::MODEL_VIEW_FRAME_COUNT; ++frame)
            for (u32 rasterClass = 0; rasterClass < ModelView::MODEL_RASTER_CLASS_COUNT; ++rasterClass)
            {
                Bind(_cullDescriptorSet, QUEUE_NAMES[frame][rasterClass], work.GetQueue(rasterClass, frame),
                     _cullBindings.frames[frame].rasterQueues[rasterClass]);
                Bind(_cullDescriptorSet, GEOMETRY_QUEUE_NAMES[frame][rasterClass],
                     work.GetGeometryQueue(rasterClass, frame),
                     _cullBindings.frames[frame].geometryQueues[rasterClass]);
            }
        Bind(_cullDescriptorSet, "_workStats0"_h, work.GetStatsBuffer(0), _cullBindings.frames[0].workStats);
        Bind(_cullDescriptorSet, "_visibilityRecords0"_h, work.GetVisibilityRecords(0),
             _cullBindings.frames[0].visibilityRecords);
        Bind(_cullDescriptorSet, "_cullReasons0"_h, work.GetCullReasons(0), _cullBindings.frames[0].cullReasons);
        Bind(_cullDescriptorSet, "_chunkQueue1"_h, work.GetChunkQueue(1), _cullBindings.frames[1].chunkQueue);
        Bind(_cullDescriptorSet, "_workStats1"_h, work.GetStatsBuffer(1), _cullBindings.frames[1].workStats);
        Bind(_cullDescriptorSet, "_visibilityRecords1"_h, work.GetVisibilityRecords(1),
             _cullBindings.frames[1].visibilityRecords);
        Bind(_cullDescriptorSet, "_cullReasons1"_h, work.GetCullReasons(1), _cullBindings.frames[1].cullReasons);
        Bind(_cullDescriptorSet, "_meshletHistory0"_h, work.GetMeshletHistory(0),
             _cullBindings.frames[0].meshletHistory);
        Bind(_cullDescriptorSet, "_meshletHistory1"_h, work.GetMeshletHistory(1),
             _cullBindings.frames[1].meshletHistory);
        Bind(_finalizeDescriptorSet, "_workStats0"_h, work.GetStatsBuffer(0), _finalizeBindings[0].workStats);
        Bind(_finalizeDescriptorSet, "_indirectArguments0"_h, work.GetArguments(0),
             _finalizeBindings[0].indirectArguments);
        Bind(_finalizeDescriptorSet, "_workStats1"_h, work.GetStatsBuffer(1), _finalizeBindings[1].workStats);
        Bind(_finalizeDescriptorSet, "_indirectArguments1"_h, work.GetArguments(1),
             _finalizeBindings[1].indirectArguments);
        Bind(_finalizeDescriptorSet, "_geometryArguments0"_h, work.GetGeometryArguments(0),
             _finalizeBindings[0].geometryArguments);
        Bind(_finalizeDescriptorSet, "_geometryArguments1"_h, work.GetGeometryArguments(1),
             _finalizeBindings[1].geometryArguments);

        if (viewState.GetInputs().IsEmpty())
            return Finish();

        Bind(_selectDescriptorSet, "_viewInputs"_h, viewState.GetInputs().GetBuffer(), _selectBindings.viewInputs);
        Bind(_selectDescriptorSet, "_modelInstances"_h, scene.GetModelInstances().GetRecords().GetBuffer(),
             _selectBindings.modelInstances);
        Bind(_selectDescriptorSet, "_modelRecords"_h, geometry.GetRecords().GetBuffer(),
             _selectBindings.modelRecords);
        Bind(_expandDescriptorSet, "_lodHistory"_h, viewState.GetLODHistory().GetBuffer(),
             _expandBindings.lodHistory);
        Bind(_expandDescriptorSet, "_modelInstances"_h, scene.GetModelInstances().GetRecords().GetBuffer(),
             _expandBindings.modelInstances);
        Bind(_expandDescriptorSet, "_modelRecords"_h, geometry.GetRecords().GetBuffer(),
             _expandBindings.modelRecords);
        Bind(_expandDescriptorSet, "_modelMeshes"_h, geometry.GetMeshes().GetBuffer(),
             _expandBindings.modelMeshes);
        Bind(_expandDescriptorSet, "_modelLODs"_h, geometry.GetMeshLODs().GetBuffer(),
             _expandBindings.modelLODs);
        Bind(_expandDescriptorSet, "_modelSubmeshes"_h, geometry.GetSubmeshes().GetBuffer(),
             _expandBindings.modelSubmeshes);
        Bind(_expandDescriptorSet, "_geometryGroupMasks"_h, scene.GetGeometryGroupMasks().GetMasks().GetBuffer(),
             _expandBindings.geometryGroupMasks);
        Bind(_expandDescriptorSet, "_materialTable"_h, scene.GetModelMaterialTables().GetEntries().GetBuffer(),
             _expandBindings.materialTable);
        Bind(_cullDescriptorSet, "_modelInstances"_h, scene.GetModelInstances().GetRecords().GetBuffer(),
             _cullBindings.modelInstances);
        Bind(_cullDescriptorSet, "_modelRecords"_h, geometry.GetRecords().GetBuffer(),
             _cullBindings.modelRecords);
        Bind(_cullDescriptorSet, "_modelMeshes"_h, geometry.GetMeshes().GetBuffer(),
             _cullBindings.modelMeshes);
        Bind(_cullDescriptorSet, "_modelMeshlets"_h, geometry.GetMeshlets().GetBuffer(),
             _cullBindings.modelMeshlets);
        return Finish();
    }

    void ModelViewWorkPass::AddHistoryClearPass(Renderer::RenderGraph* renderGraph, const RenderScenes::RenderView& view,
                                                ModelView::ModelViewState& viewState, ModelView::ModelViewWorkResources& work)
    {
        std::vector<ModelScene::MeshletHistoryRange> meshletRanges(viewState.GetPendingMeshletClears().begin(), viewState.GetPendingMeshletClears().end());
        if (meshletRanges.empty())
            return;

        std::sort(meshletRanges.begin(), meshletRanges.end(),
                  [](const ModelScene::MeshletHistoryRange& left,
                     const ModelScene::MeshletHistoryRange& right) {
                      return left.wordOffset < right.wordOffset;
                  });
        size_t meshletWrite = 0;
        for (const ModelScene::MeshletHistoryRange range : meshletRanges)
        {
            if (range.wordCount == 0)
                continue;
            if (meshletWrite != 0 && meshletRanges[meshletWrite - 1].wordOffset + meshletRanges[meshletWrite - 1].wordCount >= range.wordOffset)
            {
                auto& previous = meshletRanges[meshletWrite - 1];
                previous.wordCount = std::max(previous.wordCount, range.wordOffset + range.wordCount - previous.wordOffset);
            }
            else
            {
                meshletRanges[meshletWrite++] = range;
            }
        }
        meshletRanges.resize(meshletWrite);

        struct Data
        {
            Renderer::BufferMutableResource meshletHistory[ModelView::MODEL_VIEW_FRAME_COUNT];
            Renderer::DescriptorSetResource descriptorSet;
        };

        renderGraph->AddPass<Data>("Model History Clear: " + view.GetDebugName(),
            [this, &work](Data& data, Renderer::RenderGraphBuilder& builder) {
                using Usage = Renderer::BufferPassUsage;
                for (u32 frame = 0; frame < ModelView::MODEL_VIEW_FRAME_COUNT; ++frame)
                {
                    data.meshletHistory[frame] = builder.Write(work.GetMeshletHistory(frame), Usage::COMPUTE);
                }
                data.descriptorSet = builder.Use(_historyClearDescriptorSet);
                return true;
            },
            [this, meshletRanges](Data& data, Renderer::RenderGraphResources& graphResources,
                                                 Renderer::CommandList& commandList) {
                struct Constants
                {
                    u32 offset;
                    u32 count;
                    u32 reserved;
                };

                commandList.BeginPipeline(_historyClearPipeline);
                commandList.BindDescriptorSet(data.descriptorSet, 0);
                for (const ModelScene::MeshletHistoryRange range : meshletRanges)
                {
                    if (range.wordCount == 0)
                        continue;
                    Constants* constants = graphResources.FrameNew<Constants>();
                    *constants = {range.wordOffset, range.wordCount, 0};
                    commandList.PushConstant(constants, 0, sizeof(Constants));
                    commandList.Dispatch((range.wordCount + 63u) / 64u, 1, 1);
                }
                commandList.EndPipeline(_historyClearPipeline);
            });

        viewState.AcknowledgeTemporalClears();
    }

    void ModelViewWorkPass::AddPass(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                                    const RenderScenes::RenderView& view, const ModelView::ModelViewState& viewState,
                                    ModelView::ModelViewWorkResources& work,
                                    const ModelLoading::ModelGeometryStorage& geometry,
                                    const MaterialLoading::MaterialStorage& materials,
                                    const RenderScenes::RenderScene& scene, Renderer::ImageID depthPyramid,
                                    u8 frameIndex, bool resetHistory, bool temporal, i32 forcedLOD)
    {
        const u32 inputCount = viewState.GetDispatchInputCount();
        const i32 configuredQueueLimit = CVAR_ModelMeshletQueueLimit.Get();
        const u32 configuredQueueCapacity = static_cast<u32>(std::clamp(CVAR_ModelMeshletQueueCapacity.Get(), 1,
            static_cast<i32>(ModelView::TYPED_VISIBILITY_RECORD_CAPACITY)));
        u32 queueCapacity = std::min(work.GetQueueCapacity(), configuredQueueCapacity);
        const u32 selectionCapacity = work.GetSelectionCapacity();
        if (configuredQueueLimit > 0)
            queueCapacity = std::min(queueCapacity, static_cast<u32>(configuredQueueLimit));

        struct Data
        {
            Renderer::ImageResource depthPyramid;
            Renderer::BufferMutableResource history;
            Renderer::BufferMutableResource meshletHistory;
            Renderer::BufferMutableResource inactiveMeshletHistory;
            Renderer::BufferMutableResource selectionQueue;
            Renderer::BufferMutableResource selectionArguments;
            Renderer::BufferMutableResource chunks;
            Renderer::BufferMutableResource chunkArguments;
            Renderer::BufferMutableResource rasterQueues[ModelView::MODEL_RASTER_CLASS_COUNT];
            Renderer::BufferMutableResource geometryQueues[ModelView::MODEL_RASTER_CLASS_COUNT];
            Renderer::BufferMutableResource visibilityRecords;
            Renderer::BufferMutableResource stats;
            Renderer::BufferMutableResource arguments;
            Renderer::BufferMutableResource readback;
            Renderer::DescriptorSetResource globalSet;
            Renderer::DescriptorSetResource materialSet;
            Renderer::DescriptorSetResource selectSet;
            Renderer::DescriptorSetResource selectFinalizeSet;
            Renderer::DescriptorSetResource expandSet;
            Renderer::DescriptorSetResource expandFinalizeSet;
            Renderer::DescriptorSetResource cullSet;
            Renderer::DescriptorSetResource finalizeSet;
        };

        renderGraph->AddPass<Data>((temporal ? "Model Culling: " : "Model Work: ") + view.GetDebugName(),
            [&resources, &viewState, &work, &geometry, &materials, &scene, depthPyramid, frameIndex, inputCount,
             this](Data& data, Renderer::RenderGraphBuilder& builder) {
                using Usage = Renderer::BufferPassUsage;
                const u8 inactiveFrameIndex = (frameIndex + 1u) % ModelView::MODEL_VIEW_FRAME_COUNT;
                data.depthPyramid = builder.Read(depthPyramid, Renderer::PipelineType::COMPUTE);
                if (inputCount > 0)
                {
                    builder.Read(resources.cameras.GetBuffer(), Usage::COMPUTE);
                    builder.Read(viewState.GetInputs().GetBuffer(), Usage::COMPUTE);
                    data.history = builder.Write(viewState.GetLODHistory().GetBuffer(), Usage::COMPUTE);
                    data.meshletHistory =
                        builder.Write(work.GetMeshletHistory(frameIndex), Usage::COMPUTE | Usage::TRANSFER);
                    data.inactiveMeshletHistory =
                        builder.Write(work.GetMeshletHistory(inactiveFrameIndex), Usage::COMPUTE | Usage::TRANSFER);
                    data.selectionQueue = builder.Write(work.GetSelectionQueue(frameIndex), Usage::COMPUTE);
                    data.selectionArguments = builder.Write(work.GetSelectionArguments(frameIndex), Usage::COMPUTE);
                    builder.Write(work.GetSelectionQueue(inactiveFrameIndex), Usage::COMPUTE);
                    builder.Write(work.GetSelectionArguments(inactiveFrameIndex), Usage::COMPUTE);
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
                    for (u32 rasterClass = 0; rasterClass < ModelView::MODEL_RASTER_CLASS_COUNT; ++rasterClass)
                    {
                        data.rasterQueues[rasterClass] = builder.Write(work.GetQueue(rasterClass, frameIndex), Usage::COMPUTE);
                        data.geometryQueues[rasterClass] =
                            builder.Write(work.GetGeometryQueue(rasterClass, frameIndex), Usage::COMPUTE);
                    }
                    data.visibilityRecords = builder.Write(work.GetVisibilityRecords(frameIndex), Usage::COMPUTE);
                    builder.Write(work.GetCullReasons(frameIndex), Usage::COMPUTE);
                    // Both generations are RW shader bindings; resourceIndex selects the active generation.
                    builder.Write(work.GetChunkQueue(inactiveFrameIndex), Usage::COMPUTE);
                    for (u32 rasterClass = 0; rasterClass < ModelView::MODEL_RASTER_CLASS_COUNT; ++rasterClass)
                    {
                        builder.Write(work.GetQueue(rasterClass, inactiveFrameIndex), Usage::COMPUTE);
                        builder.Write(work.GetGeometryQueue(rasterClass, inactiveFrameIndex), Usage::COMPUTE);
                    }
                    builder.Write(work.GetVisibilityRecords(inactiveFrameIndex), Usage::COMPUTE);
                    builder.Write(work.GetCullReasons(inactiveFrameIndex), Usage::COMPUTE);
                    data.globalSet = builder.Use(resources.globalDescriptorSet);
                    data.materialSet = builder.Use(resources.materialDescriptorSet);
                    data.selectSet = builder.Use(_selectDescriptorSet);
                    data.selectFinalizeSet = builder.Use(_selectFinalizeDescriptorSet);
                    data.expandSet = builder.Use(_expandDescriptorSet);
                    data.cullSet = builder.Use(_cullDescriptorSet);
                }
                data.chunkArguments = builder.Write(work.GetChunkArguments(frameIndex), Usage::COMPUTE);
                builder.Write(work.GetChunkArguments(inactiveFrameIndex), Usage::COMPUTE);
                data.stats = builder.Write(work.GetStatsBuffer(frameIndex), Usage::COMPUTE | Usage::TRANSFER);
                data.arguments = builder.Write(work.GetArguments(frameIndex), Usage::COMPUTE | Usage::TRANSFER);
                // Both generations are RW shader bindings; resourceIndex selects the active generation.
                builder.Write(work.GetStatsBuffer(!frameIndex), Usage::COMPUTE);
                builder.Write(work.GetArguments(!frameIndex), Usage::COMPUTE);
                builder.Write(work.GetGeometryArguments(frameIndex), Usage::COMPUTE);
                builder.Write(work.GetGeometryArguments(!frameIndex), Usage::COMPUTE);
                data.readback = builder.Write(work.GetStatsReadback(frameIndex), Usage::TRANSFER);
                data.expandFinalizeSet = builder.Use(_expandFinalizeDescriptorSet);
                data.finalizeSet = builder.Use(_finalizeDescriptorSet);
                return true;
            },
            [this, &view, &work, inputCount, queueCapacity, selectionCapacity, frameIndex, resetHistory, temporal,
             forcedLOD](
                Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) {
                GPU_SCOPED_PROFILER_ZONE(commandList, ModelViewWork);
                commandList.FillBuffer(data.stats, 0, sizeof(ModelView::WorkStats), 0);
                if (inputCount > 0)
                {
                    commandList.FillBuffer(data.meshletHistory, 0, sizeof(u32) * work.GetMeshletHistoryWords(), 0);
                    if (resetHistory)
                        commandList.FillBuffer(data.inactiveMeshletHistory, 0,
                                               sizeof(u32) * work.GetMeshletHistoryWords(), 0);
                }
                commandList.BufferBarrier(data.stats, Renderer::BufferPassUsage::TRANSFER);
                if (inputCount > 0)
                {
                    commandList.BufferBarrier(data.meshletHistory, Renderer::BufferPassUsage::TRANSFER);
                    if (resetHistory)
                        commandList.BufferBarrier(data.inactiveMeshletHistory, Renderer::BufferPassUsage::TRANSFER);
                }

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
                    u32 showCullReasons;
                };
                Constants* constants = graphResources.FrameNew<Constants>();
                constants->viewIndex = view.GetCameraIndex();
                constants->inputCount = inputCount;
                constants->queueCapacity = queueCapacity;
                constants->forcedLOD = forcedLOD;
                constants->viewportHeight = static_cast<f32>(view.GetDimensions().y);
                constants->lodTargetPixels = 2.0f;
                constants->lodHysteresis = 0.15f;
                constants->resetHistory = resetHistory ? 1u : 0u;
                constants->resourceIndex = frameIndex;
                constants->showCullReasons = ModelRendering::ShowModelCullReasons() ? 1u : 0u;

                if (inputCount > 0)
                {
                    struct SelectConstants
                    {
                        u32 viewIndex;
                        u32 inputCount;
                        u32 selectionCapacity;
                        u32 resourceIndex;
                        u32 showCullReasons;
                    };
                    SelectConstants* select = graphResources.FrameNew<SelectConstants>();
                    select->viewIndex = view.GetCameraIndex();
                    select->inputCount = inputCount;
                    select->selectionCapacity = selectionCapacity;
                    select->resourceIndex = frameIndex;
                    select->showCullReasons = ModelRendering::ShowModelCullReasons() ? 1u : 0u;
                    commandList.BeginPipeline(_selectPipeline);
                    commandList.PushConstant(select, 0, sizeof(SelectConstants));
                    commandList.BindDescriptorSet(data.globalSet, frameIndex);
                    commandList.BindDescriptorSet(data.selectSet, frameIndex);
                    commandList.Dispatch((inputCount + 63u) / 64u, 1, 1);
                    commandList.EndPipeline(_selectPipeline);
                    commandList.BufferBarrier(data.selectionQueue, Renderer::BufferPassUsage::COMPUTE);
                    commandList.BufferBarrier(data.stats, Renderer::BufferPassUsage::COMPUTE);

                    struct SelectFinalizeConstants { u32 selectionCapacity; u32 resourceIndex; };
                    SelectFinalizeConstants* selectFinalize = graphResources.FrameNew<SelectFinalizeConstants>();
                    selectFinalize->selectionCapacity = selectionCapacity;
                    selectFinalize->resourceIndex = frameIndex;
                    commandList.BeginPipeline(_selectFinalizePipeline);
                    commandList.PushConstant(selectFinalize, 0, sizeof(SelectFinalizeConstants));
                    commandList.BindDescriptorSet(data.selectFinalizeSet, frameIndex);
                    commandList.Dispatch(1, 1, 1);
                    commandList.EndPipeline(_selectFinalizePipeline);
                    commandList.BufferBarrier(data.selectionArguments, Renderer::BufferPassUsage::COMPUTE);

                    commandList.BeginPipeline(_expandPipeline);
                    commandList.PushConstant(constants, 0, sizeof(Constants));
                    commandList.BindDescriptorSet(data.globalSet, frameIndex);
                    commandList.BindDescriptorSet(data.materialSet, frameIndex);
                    commandList.BindDescriptorSet(data.expandSet, frameIndex);
                    commandList.DispatchIndirect(data.selectionArguments, 0);
                    commandList.EndPipeline(_expandPipeline);

                    commandList.BufferBarrier(data.chunks, Renderer::BufferPassUsage::COMPUTE);
                    commandList.BufferBarrier(data.stats, Renderer::BufferPassUsage::COMPUTE);
                }
                struct ExpandFinalizeConstants
                {
                    u32 queueCapacity;
                    u32 resourceIndex;
                };
                ExpandFinalizeConstants* expandFinalizeConstants = graphResources.FrameNew<ExpandFinalizeConstants>();
                expandFinalizeConstants->queueCapacity = queueCapacity;
                expandFinalizeConstants->resourceIndex = frameIndex;
                commandList.BeginPipeline(_expandFinalizePipeline);
                commandList.PushConstant(expandFinalizeConstants, 0, sizeof(ExpandFinalizeConstants));
                commandList.BindDescriptorSet(data.expandFinalizeSet, frameIndex);
                commandList.Dispatch(1, 1, 1);
                commandList.EndPipeline(_expandFinalizePipeline);

                commandList.BufferBarrier(data.chunkArguments, Renderer::BufferPassUsage::COMPUTE);
                if (inputCount > 0)
                {
                    struct CullConstants
                    {
                        u32 viewIndex;
                        u32 queueCapacity;
                        u32 resourceIndex;
                        u32 enableConeCulling;
                        u32 enableOcclusionCulling;
                        u32 viewportWidth;
                        u32 viewportHeight;
                        u32 enableTemporal;
                        u32 showCullReasons;
                    };
                    CullConstants* cullConstants = graphResources.FrameNew<CullConstants>();
                    cullConstants->viewIndex = view.GetCameraIndex();
                    cullConstants->queueCapacity = queueCapacity;
                    cullConstants->resourceIndex = frameIndex;
                    cullConstants->enableConeCulling = CVAR_ModelMeshletConeCulling.Get() != 0 ? 1u : 0u;
                    cullConstants->enableOcclusionCulling =
                        temporal && CVAR_ModelMeshletOcclusionCulling.Get() != 0 ? 1u : 0u;
                    cullConstants->viewportWidth = view.GetDimensions().x;
                    cullConstants->viewportHeight = view.GetDimensions().y;
                    cullConstants->enableTemporal =
                        temporal && !ModelRendering::ShowModelCullReasons() ? 1u : 0u;
                    cullConstants->showCullReasons = ModelRendering::ShowModelCullReasons() ? 1u : 0u;
                    data.cullSet.Bind("_depthPyramid", data.depthPyramid);
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
                work.MarkSubmitted(frameIndex);
            }, Renderer::RenderPassFlags::SideEffect);
    }

} // namespace ModelPipeline
