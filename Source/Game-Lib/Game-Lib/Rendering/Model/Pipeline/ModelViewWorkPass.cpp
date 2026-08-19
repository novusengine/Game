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
          _expandFinalizeDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS),
          _cullDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS),
          _finalizeDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS),
          _replayDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS),
          _beginPhase2DescriptorSet(Renderer::DescriptorSetSlot::PER_PASS),
          _historyClearDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS)
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

        shader.shaderEntry = gameRenderer->GetShaderEntry("Model/ViewWorkReplay.cs"_h, "Model/ViewWorkReplay.cs");
        desc.debugName = "Model View Survivor Replay";
        desc.computeShader = renderer->LoadShader(shader);
        _replayPipeline = renderer->CreatePipeline(desc);

        shader.shaderEntry =
            gameRenderer->GetShaderEntry("Model/ViewWorkBeginPhase2.cs"_h, "Model/ViewWorkBeginPhase2.cs");
        desc.debugName = "Model View Begin Phase 2";
        desc.computeShader = renderer->LoadShader(shader);
        _beginPhase2Pipeline = renderer->CreatePipeline(desc);

        _expandDescriptorSet.RegisterPipeline(renderer, _expandPipeline);
        _expandDescriptorSet.Init(renderer);
        _expandFinalizeDescriptorSet.RegisterPipeline(renderer, _expandFinalizePipeline);
        _expandFinalizeDescriptorSet.Init(renderer);
        _cullDescriptorSet.RegisterPipeline(renderer, _cullPipeline);
        _cullDescriptorSet.Init(renderer);
        _cullDescriptorSet.Bind("_depthSampler", DepthPyramidUtils::_pyramidSampler);
        _finalizeDescriptorSet.RegisterPipeline(renderer, _finalizePipeline);
        _finalizeDescriptorSet.Init(renderer);
        _replayDescriptorSet.RegisterPipeline(renderer, _replayPipeline);
        _replayDescriptorSet.Init(renderer);
        _beginPhase2DescriptorSet.RegisterPipeline(renderer, _beginPhase2Pipeline);
        _beginPhase2DescriptorSet.Init(renderer);

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
        const u32 configuredCapacity = static_cast<u32>(std::max(CVAR_ModelMeshletQueueCapacity.Get(), 1));
        const bool queuesRecreated =
            work.EnsureQueueCapacity(std::min(viewState.GetQueueCapacity(), configuredCapacity));
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
                _cullBindings.frames[frame].visibilityRecords = Renderer::BufferID::Invalid();
                _cullBindings.frames[frame].survivorQueue = Renderer::BufferID::Invalid();
                _replayBindings.survivorQueues[frame] = Renderer::BufferID::Invalid();
                _replayBindings.visibilityRecords[frame] = Renderer::BufferID::Invalid();
                for (Renderer::BufferID& queue : _replayBindings.rasterQueues[frame])
                    queue = Renderer::BufferID::Invalid();
            }
        }
        if (historyRecreated)
        {
            for (u32 frame = 0; frame < ModelView::MODEL_VIEW_FRAME_COUNT; ++frame)
            {
                _expandBindings.frames[frame].instanceVisibility = Renderer::BufferID::Invalid();
                _cullBindings.frames[frame].meshletHistory = Renderer::BufferID::Invalid();
                _replayBindings.instanceVisibility[frame] = Renderer::BufferID::Invalid();
                _replayBindings.meshletHistory[frame] = Renderer::BufferID::Invalid();
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
            return !_expandDescriptorSet.HasPendingBufferWrites() &&
                   !_expandFinalizeDescriptorSet.HasPendingBufferWrites() &&
                   !_cullDescriptorSet.HasPendingBufferWrites() &&
                   !_finalizeDescriptorSet.HasPendingBufferWrites() &&
                   !_replayDescriptorSet.HasPendingBufferWrites() &&
                   !_beginPhase2DescriptorSet.HasPendingBufferWrites() &&
                   !_historyClearDescriptorSet.HasPendingBufferWrites();
        };

        Bind(_expandDescriptorSet, "_chunkQueue0"_h, work.GetChunkQueue(0), _expandBindings.frames[0].chunkQueue);
        Bind(_expandDescriptorSet, "_workStats0"_h, work.GetStatsBuffer(0), _expandBindings.frames[0].workStats);
        Bind(_expandDescriptorSet, "_chunkQueue1"_h, work.GetChunkQueue(1), _expandBindings.frames[1].chunkQueue);
        Bind(_expandDescriptorSet, "_workStats1"_h, work.GetStatsBuffer(1), _expandBindings.frames[1].workStats);
        Bind(_expandDescriptorSet, "_instanceVisibility0"_h, work.GetInstanceVisibility(0),
             _expandBindings.frames[0].instanceVisibility);
        Bind(_expandDescriptorSet, "_instanceVisibility1"_h, work.GetInstanceVisibility(1),
             _expandBindings.frames[1].instanceVisibility);
        Bind(_historyClearDescriptorSet, "_clearInstanceVisibility0"_h, work.GetInstanceVisibility(0), _historyClearInstanceVisibility[0]);
        Bind(_historyClearDescriptorSet, "_clearInstanceVisibility1"_h, work.GetInstanceVisibility(1), _historyClearInstanceVisibility[1]);
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
        for (u32 frame = 0; frame < ModelView::MODEL_VIEW_FRAME_COUNT; ++frame)
            for (u32 rasterClass = 0; rasterClass < ModelView::MODEL_RASTER_CLASS_COUNT; ++rasterClass)
                Bind(_cullDescriptorSet, QUEUE_NAMES[frame][rasterClass], work.GetQueue(rasterClass, frame),
                     _cullBindings.frames[frame].rasterQueues[rasterClass]);
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
        Bind(_cullDescriptorSet, "_survivors0"_h, work.GetSurvivorQueue(0),
             _cullBindings.frames[0].survivorQueue);
        Bind(_cullDescriptorSet, "_survivors1"_h, work.GetSurvivorQueue(1),
             _cullBindings.frames[1].survivorQueue);

        Bind(_finalizeDescriptorSet, "_workStats0"_h, work.GetStatsBuffer(0), _finalizeBindings[0].workStats);
        Bind(_finalizeDescriptorSet, "_indirectArguments0"_h, work.GetArguments(0),
             _finalizeBindings[0].indirectArguments);
        Bind(_finalizeDescriptorSet, "_workStats1"_h, work.GetStatsBuffer(1), _finalizeBindings[1].workStats);
        Bind(_finalizeDescriptorSet, "_indirectArguments1"_h, work.GetArguments(1),
             _finalizeBindings[1].indirectArguments);
        Bind(_finalizeDescriptorSet, "_survivorArguments0"_h, work.GetSurvivorArguments(0),
             _finalizeBindings[0].survivorArguments);
        Bind(_finalizeDescriptorSet, "_survivorArguments1"_h, work.GetSurvivorArguments(1),
             _finalizeBindings[1].survivorArguments);

        Bind(_beginPhase2DescriptorSet, "_workStats0"_h, work.GetStatsBuffer(0), _beginPhase2Bindings[0].workStats);
        Bind(_beginPhase2DescriptorSet, "_workStats1"_h, work.GetStatsBuffer(1), _beginPhase2Bindings[1].workStats);
        Bind(_beginPhase2DescriptorSet, "_indirectArguments0"_h, work.GetArguments(0),
             _beginPhase2Bindings[0].indirectArguments);
        Bind(_beginPhase2DescriptorSet, "_indirectArguments1"_h, work.GetArguments(1),
             _beginPhase2Bindings[1].indirectArguments);

        Bind(_replayDescriptorSet, "_modelInstances"_h, scene.GetModelInstances().GetRecords().GetBuffer(),
             _replayBindings.modelInstances);
        Bind(_replayDescriptorSet, "_modelRecords"_h, geometry.GetRecords().GetBuffer(), _replayBindings.modelRecords);
        Bind(_replayDescriptorSet, "_modelMeshlets"_h, geometry.GetMeshlets().GetBuffer(), _replayBindings.modelMeshlets);
        Bind(_replayDescriptorSet, "_lodHistory"_h, viewState.GetLODHistory().GetBuffer(), _replayBindings.lodHistory);
        static constexpr StringUtils::StringHash REPLAY_QUEUE_NAMES[ModelView::MODEL_VIEW_FRAME_COUNT]
                                                                    [ModelView::MODEL_RASTER_CLASS_COUNT] = {
            {"_oneSidedQueue0"_h, "_twoSidedQueue0"_h, "_alphaTestOneSidedQueue0"_h, "_alphaTestTwoSidedQueue0"_h},
            {"_oneSidedQueue1"_h, "_twoSidedQueue1"_h, "_alphaTestOneSidedQueue1"_h, "_alphaTestTwoSidedQueue1"_h}};
        for (u32 frame = 0; frame < ModelView::MODEL_VIEW_FRAME_COUNT; ++frame)
        {
            Bind(_replayDescriptorSet, frame == 0 ? "_instanceVisibility0"_h : "_instanceVisibility1"_h,
                 work.GetInstanceVisibility(frame), _replayBindings.instanceVisibility[frame]);
            Bind(_replayDescriptorSet, frame == 0 ? "_meshletHistory0"_h : "_meshletHistory1"_h,
                 work.GetMeshletHistory(frame), _replayBindings.meshletHistory[frame]);
            Bind(_replayDescriptorSet, frame == 0 ? "_survivors0"_h : "_survivors1"_h,
                 work.GetSurvivorQueue(frame), _replayBindings.survivorQueues[frame]);
            Bind(_replayDescriptorSet, frame == 0 ? "_visibilityRecords0"_h : "_visibilityRecords1"_h,
                 work.GetVisibilityRecords(frame), _replayBindings.visibilityRecords[frame]);
            Bind(_replayDescriptorSet, frame == 0 ? "_workStats0"_h : "_workStats1"_h,
                 work.GetStatsBuffer(frame), _replayBindings.workStats[frame]);
            for (u32 rasterClass = 0; rasterClass < ModelView::MODEL_RASTER_CLASS_COUNT; ++rasterClass)
                Bind(_replayDescriptorSet, REPLAY_QUEUE_NAMES[frame][rasterClass], work.GetQueue(rasterClass, frame),
                     _replayBindings.rasterQueues[frame][rasterClass]);
        }

        if (viewState.GetInputs().IsEmpty())
            return Finish();

        Bind(_expandDescriptorSet, "_viewInputs"_h, viewState.GetInputs().GetBuffer(),
             _expandBindings.viewInputs);
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
        std::vector<u32> instanceSlots(viewState.GetPendingInstanceClears().begin(), viewState.GetPendingInstanceClears().end());
        std::vector<ModelScene::MeshletHistoryRange> meshletRanges(viewState.GetPendingMeshletClears().begin(), viewState.GetPendingMeshletClears().end());
        if (instanceSlots.empty() && meshletRanges.empty())
            return;

        std::sort(instanceSlots.begin(), instanceSlots.end());
        std::vector<RenderScenes::StableRange> instanceRanges;
        instanceRanges.reserve(instanceSlots.size());
        for (const u32 slot : instanceSlots)
        {
            if (!instanceRanges.empty() && instanceRanges.back().offset + instanceRanges.back().count == slot)
                ++instanceRanges.back().count;
            else if (instanceRanges.empty() || instanceRanges.back().offset != slot)
                instanceRanges.push_back({slot, 1});
        }

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
            Renderer::BufferMutableResource instanceVisibility[ModelView::MODEL_VIEW_FRAME_COUNT];
            Renderer::BufferMutableResource meshletHistory[ModelView::MODEL_VIEW_FRAME_COUNT];
            Renderer::DescriptorSetResource descriptorSet;
        };

        renderGraph->AddPass<Data>("Model History Clear: " + view.GetDebugName(),
            [this, &work](Data& data, Renderer::RenderGraphBuilder& builder) {
                using Usage = Renderer::BufferPassUsage;
                for (u32 frame = 0; frame < ModelView::MODEL_VIEW_FRAME_COUNT; ++frame)
                {
                    data.instanceVisibility[frame] = builder.Write(work.GetInstanceVisibility(frame), Usage::COMPUTE);
                    data.meshletHistory[frame] = builder.Write(work.GetMeshletHistory(frame), Usage::COMPUTE);
                }
                data.descriptorSet = builder.Use(_historyClearDescriptorSet);
                return true;
            },
            [this, instanceRanges, meshletRanges](Data& data, Renderer::RenderGraphResources& graphResources,
                                                 Renderer::CommandList& commandList) {
                struct Constants
                {
                    u32 mode;
                    u32 offset;
                    u32 count;
                };

                commandList.BeginPipeline(_historyClearPipeline);
                commandList.BindDescriptorSet(data.descriptorSet, 0);
                for (const RenderScenes::StableRange range : instanceRanges)
                {
                    Constants* constants = graphResources.FrameNew<Constants>();
                    *constants = {0, range.offset, range.count};
                    commandList.PushConstant(constants, 0, sizeof(Constants));
                    commandList.Dispatch((range.count + 63u) / 64u, 1, 1);
                }
                for (const ModelScene::MeshletHistoryRange range : meshletRanges)
                {
                    if (range.wordCount == 0)
                        continue;
                    Constants* constants = graphResources.FrameNew<Constants>();
                    *constants = {1, range.wordOffset, range.wordCount};
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
                                    const RenderScenes::RenderScene& scene, u8 frameIndex, bool resetHistory,
                                    i32 forcedLOD)
    {
        const u32 inputCount = viewState.GetDispatchInputCount();
        const i32 configuredQueueLimit = CVAR_ModelMeshletQueueLimit.Get();
        const u32 configuredQueueCapacity =
            static_cast<u32>(std::max(CVAR_ModelMeshletQueueCapacity.Get(), 1));
        u32 queueCapacity = std::min(work.GetQueueCapacity(), configuredQueueCapacity);
        if (configuredQueueLimit > 0)
            queueCapacity = std::min(queueCapacity, static_cast<u32>(configuredQueueLimit));

        struct Data
        {
            Renderer::ImageResource depthPyramid;
            Renderer::BufferMutableResource history;
            Renderer::BufferMutableResource instanceVisibility;
            Renderer::BufferMutableResource chunks;
            Renderer::BufferMutableResource chunkArguments;
            Renderer::BufferMutableResource rasterQueues[ModelView::MODEL_RASTER_CLASS_COUNT];
            Renderer::BufferMutableResource visibilityRecords;
            Renderer::BufferMutableResource stats;
            Renderer::BufferMutableResource arguments;
            Renderer::BufferMutableResource readback;
            Renderer::DescriptorSetResource globalSet;
            Renderer::DescriptorSetResource materialSet;
            Renderer::DescriptorSetResource expandSet;
            Renderer::DescriptorSetResource expandFinalizeSet;
            Renderer::DescriptorSetResource cullSet;
            Renderer::DescriptorSetResource finalizeSet;
        };

        renderGraph->AddPass<Data>("Model Work: " + view.GetDebugName(),
            [&resources, &view, &viewState, &work, &geometry, &materials, &scene, frameIndex, inputCount, this](Data& data, Renderer::RenderGraphBuilder& builder) {
                using Usage = Renderer::BufferPassUsage;
                const u8 inactiveFrameIndex = (frameIndex + 1u) % ModelView::MODEL_VIEW_FRAME_COUNT;
                data.depthPyramid = builder.Read(view.GetDepthPyramidTarget(), Renderer::PipelineType::COMPUTE);
                if (inputCount > 0)
                {
                    builder.Read(resources.cameras.GetBuffer(), Usage::COMPUTE);
                    builder.Read(viewState.GetInputs().GetBuffer(), Usage::COMPUTE);
                    data.history = builder.Write(viewState.GetLODHistory().GetBuffer(), Usage::COMPUTE);
                    data.instanceVisibility = builder.Write(work.GetInstanceVisibility(frameIndex), Usage::COMPUTE | Usage::TRANSFER);
                    builder.Write(work.GetInstanceVisibility(inactiveFrameIndex), Usage::COMPUTE);
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
                        data.rasterQueues[rasterClass] = builder.Write(work.GetQueue(rasterClass, frameIndex), Usage::COMPUTE);
                    data.visibilityRecords = builder.Write(work.GetVisibilityRecords(frameIndex), Usage::COMPUTE);
                    builder.Write(work.GetCullReasons(frameIndex), Usage::COMPUTE);
                    builder.Write(work.GetMeshletHistory(frameIndex), Usage::COMPUTE);
                    builder.Write(work.GetSurvivorQueue(frameIndex), Usage::COMPUTE);
                    // Both generations are RW shader bindings; resourceIndex selects the active generation.
                    builder.Write(work.GetChunkQueue(inactiveFrameIndex), Usage::COMPUTE);
                    for (u32 rasterClass = 0; rasterClass < ModelView::MODEL_RASTER_CLASS_COUNT; ++rasterClass)
                        builder.Write(work.GetQueue(rasterClass, inactiveFrameIndex), Usage::COMPUTE);
                    builder.Write(work.GetVisibilityRecords(inactiveFrameIndex), Usage::COMPUTE);
                    builder.Write(work.GetCullReasons(inactiveFrameIndex), Usage::COMPUTE);
                    builder.Write(work.GetMeshletHistory(inactiveFrameIndex), Usage::COMPUTE);
                    builder.Write(work.GetSurvivorQueue(inactiveFrameIndex), Usage::COMPUTE);
                    data.globalSet = builder.Use(resources.globalDescriptorSet);
                    data.materialSet = builder.Use(resources.materialDescriptorSet);
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
                builder.Write(work.GetSurvivorArguments(frameIndex), Usage::COMPUTE);
                builder.Write(work.GetSurvivorArguments(!frameIndex), Usage::COMPUTE);
                data.readback = builder.Write(work.GetStatsReadback(frameIndex), Usage::TRANSFER);
                data.expandFinalizeSet = builder.Use(_expandFinalizeDescriptorSet);
                data.finalizeSet = builder.Use(_finalizeDescriptorSet);
                return true;
            },
            [this, &view, &work, inputCount, queueCapacity, frameIndex, resetHistory, forcedLOD](
                Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) {
                GPU_SCOPED_PROFILER_ZONE(commandList, ModelViewWork);
                commandList.FillBuffer(data.stats, 0, sizeof(ModelView::WorkStats), 0);
                if (inputCount > 0)
                    commandList.FillBuffer(data.instanceVisibility, 0, sizeof(u32) * work.GetInstanceVisibilityWords(), 0);
                commandList.BufferBarrier(data.stats, Renderer::BufferPassUsage::TRANSFER);
                if (inputCount > 0)
                    commandList.BufferBarrier(data.instanceVisibility, Renderer::BufferPassUsage::TRANSFER);

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
                    commandList.BeginPipeline(_expandPipeline);
                    commandList.PushConstant(constants, 0, sizeof(Constants));
                    commandList.BindDescriptorSet(data.globalSet, frameIndex);
                    commandList.BindDescriptorSet(data.materialSet, frameIndex);
                    commandList.BindDescriptorSet(data.expandSet, frameIndex);
                    commandList.Dispatch(inputCount, 1, 1);
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
                    cullConstants->enableOcclusionCulling = 0;
                    cullConstants->viewportWidth = view.GetDimensions().x;
                    cullConstants->viewportHeight = view.GetDimensions().y;
                    cullConstants->enableTemporal = 0;
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
            });
    }

    void ModelViewWorkPass::AddPhase1Pass(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                                          const RenderScenes::RenderView& view,
                                          const ModelView::ModelViewState& viewState,
                                          ModelView::ModelViewWorkResources& work,
                                          const ModelLoading::ModelGeometryStorage& geometry,
                                          const MaterialLoading::MaterialStorage& materials,
                                          const RenderScenes::RenderScene& scene, u8 frameIndex, bool resetHistory,
                                          i32 forcedLOD)
    {
        const u32 inputCount = viewState.GetDispatchInputCount();
        const u32 inactiveFrameIndex = (frameIndex + 1u) % ModelView::MODEL_VIEW_FRAME_COUNT;
        const u32 configuredCapacity = static_cast<u32>(std::max(CVAR_ModelMeshletQueueCapacity.Get(), 1));
        u32 queueCapacity = std::min(work.GetQueueCapacity(), configuredCapacity);
        if (CVAR_ModelMeshletQueueLimit.Get() > 0)
            queueCapacity = std::min(queueCapacity, static_cast<u32>(CVAR_ModelMeshletQueueLimit.Get()));

        struct Data
        {
            Renderer::BufferMutableResource lodHistory;
            Renderer::BufferMutableResource instanceVisibility;
            Renderer::BufferMutableResource inactiveInstanceVisibility;
            Renderer::BufferMutableResource meshletHistory;
            Renderer::BufferMutableResource inactiveMeshletHistory;
            Renderer::BufferMutableResource chunks;
            Renderer::BufferMutableResource chunkArguments;
            Renderer::BufferMutableResource rasterQueues[ModelView::MODEL_RASTER_CLASS_COUNT];
            Renderer::BufferMutableResource visibilityRecords;
            Renderer::BufferMutableResource stats;
            Renderer::BufferMutableResource arguments;
            Renderer::BufferMutableResource survivorQueue;
            Renderer::BufferMutableResource survivorArguments;
            Renderer::BufferMutableResource previousSurvivorArguments;
            Renderer::DescriptorSetResource globalSet;
            Renderer::DescriptorSetResource materialSet;
            Renderer::DescriptorSetResource expandSet;
            Renderer::DescriptorSetResource expandFinalizeSet;
            Renderer::DescriptorSetResource replaySet;
            Renderer::DescriptorSetResource finalizeSet;
        };

        renderGraph->AddPass<Data>(
            "Model Work Phase 1: " + view.GetDebugName(),
            [&resources, &viewState, &work, &geometry, &materials, &scene, frameIndex, inactiveFrameIndex, inputCount,
             this](Data& data, Renderer::RenderGraphBuilder& builder) {
                using Usage = Renderer::BufferPassUsage;
                if (inputCount > 0)
                {
                    builder.Read(resources.cameras.GetBuffer(), Usage::COMPUTE);
                    builder.Read(viewState.GetInputs().GetBuffer(), Usage::COMPUTE);
                    data.lodHistory = builder.Write(viewState.GetLODHistory().GetBuffer(), Usage::COMPUTE);
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
                    data.globalSet = builder.Use(resources.globalDescriptorSet);
                    data.materialSet = builder.Use(resources.materialDescriptorSet);
                    data.expandSet = builder.Use(_expandDescriptorSet);
                    data.replaySet = builder.Use(_replayDescriptorSet);
                }
                data.instanceVisibility = builder.Write(work.GetInstanceVisibility(frameIndex), Usage::COMPUTE | Usage::TRANSFER);
                data.inactiveInstanceVisibility = builder.Write(work.GetInstanceVisibility(inactiveFrameIndex), Usage::COMPUTE | Usage::TRANSFER);
                data.meshletHistory = builder.Write(work.GetMeshletHistory(frameIndex), Usage::COMPUTE | Usage::TRANSFER);
                data.inactiveMeshletHistory = builder.Write(work.GetMeshletHistory(inactiveFrameIndex), Usage::COMPUTE | Usage::TRANSFER);
                data.chunks = builder.Write(work.GetChunkQueue(frameIndex), Usage::COMPUTE);
                data.chunkArguments = builder.Write(work.GetChunkArguments(frameIndex), Usage::COMPUTE | Usage::TRANSFER);
                builder.Write(work.GetChunkQueue(inactiveFrameIndex), Usage::COMPUTE);
                builder.Write(work.GetChunkArguments(inactiveFrameIndex), Usage::COMPUTE);
                for (u32 rasterClass = 0; rasterClass < ModelView::MODEL_RASTER_CLASS_COUNT; ++rasterClass)
                {
                    data.rasterQueues[rasterClass] = builder.Write(work.GetQueue(rasterClass, frameIndex), Usage::COMPUTE);
                    builder.Write(work.GetQueue(rasterClass, inactiveFrameIndex), Usage::COMPUTE);
                }
                data.visibilityRecords = builder.Write(work.GetVisibilityRecords(frameIndex), Usage::COMPUTE);
                builder.Write(work.GetVisibilityRecords(inactiveFrameIndex), Usage::COMPUTE);
                builder.Write(work.GetCullReasons(frameIndex), Usage::COMPUTE);
                builder.Write(work.GetCullReasons(inactiveFrameIndex), Usage::COMPUTE);
                data.stats = builder.Write(work.GetStatsBuffer(frameIndex), Usage::COMPUTE | Usage::TRANSFER);
                builder.Write(work.GetStatsBuffer(inactiveFrameIndex), Usage::COMPUTE);
                data.arguments = builder.Write(work.GetArguments(frameIndex), Usage::COMPUTE | Usage::TRANSFER);
                builder.Write(work.GetArguments(inactiveFrameIndex), Usage::COMPUTE);
                data.survivorQueue = builder.Write(work.GetSurvivorQueue(frameIndex), Usage::COMPUTE);
                builder.Write(work.GetSurvivorQueue(inactiveFrameIndex), Usage::COMPUTE);
                data.previousSurvivorArguments = builder.Write(work.GetSurvivorArguments(inactiveFrameIndex), Usage::COMPUTE);
                data.survivorArguments = builder.Write(work.GetSurvivorArguments(frameIndex), Usage::COMPUTE | Usage::TRANSFER);
                if (inputCount > 0)
                {
                    data.expandFinalizeSet = builder.Use(_expandFinalizeDescriptorSet);
                    data.finalizeSet = builder.Use(_finalizeDescriptorSet);
                }
                return true;
            },
            [this, &view, &work, inputCount, queueCapacity, frameIndex, resetHistory, forcedLOD](
                Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) {
                GPU_SCOPED_PROFILER_ZONE(commandList, ModelViewPhase1);
                commandList.FillBuffer(data.stats, 0, sizeof(ModelView::WorkStats), 0);
                commandList.FillBuffer(data.instanceVisibility, 0, sizeof(u32) * work.GetInstanceVisibilityWords(), 0);
                commandList.FillBuffer(data.meshletHistory, 0, sizeof(u32) * work.GetMeshletHistoryWords(), 0);
                if (resetHistory)
                {
                    commandList.FillBuffer(data.inactiveInstanceVisibility, 0,
                                           sizeof(u32) * work.GetInstanceVisibilityWords(), 0);
                    commandList.FillBuffer(data.inactiveMeshletHistory, 0,
                                           sizeof(u32) * work.GetMeshletHistoryWords(), 0);
                }
                commandList.BufferBarrier(data.stats, Renderer::BufferPassUsage::TRANSFER);
                commandList.BufferBarrier(data.instanceVisibility, Renderer::BufferPassUsage::TRANSFER);
                commandList.BufferBarrier(data.meshletHistory, Renderer::BufferPassUsage::TRANSFER);
                if (resetHistory)
                {
                    commandList.BufferBarrier(data.inactiveInstanceVisibility, Renderer::BufferPassUsage::TRANSFER);
                    commandList.BufferBarrier(data.inactiveMeshletHistory, Renderer::BufferPassUsage::TRANSFER);
                }
                if (inputCount == 0)
                {
                    commandList.FillBuffer(data.chunkArguments, 0, sizeof(u32) * ModelView::MODEL_DISPATCH_ARGUMENT_COUNT, 0);
                    commandList.FillBuffer(data.arguments, 0,
                                           sizeof(u32) * ModelView::MODEL_RASTER_CLASS_COUNT *
                                               ModelView::MODEL_DISPATCH_ARGUMENT_COUNT, 0);
                    commandList.FillBuffer(data.survivorArguments, 0, sizeof(u32) * ModelView::MODEL_DISPATCH_ARGUMENT_COUNT, 0);
                    commandList.BufferBarrier(data.chunkArguments, Renderer::BufferPassUsage::TRANSFER);
                    commandList.BufferBarrier(data.arguments, Renderer::BufferPassUsage::TRANSFER);
                    commandList.BufferBarrier(data.survivorArguments, Renderer::BufferPassUsage::TRANSFER);
                    return;
                }
                struct WorkConstants
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
                WorkConstants* constants = graphResources.FrameNew<WorkConstants>();
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
                    commandList.BeginPipeline(_expandPipeline);
                    commandList.PushConstant(constants, 0, sizeof(WorkConstants));
                    commandList.BindDescriptorSet(data.globalSet, frameIndex);
                    commandList.BindDescriptorSet(data.materialSet, frameIndex);
                    commandList.BindDescriptorSet(data.expandSet, frameIndex);
                    commandList.Dispatch(inputCount, 1, 1);
                    commandList.EndPipeline(_expandPipeline);

                    commandList.BufferBarrier(data.chunks, Renderer::BufferPassUsage::COMPUTE);
                    commandList.BufferBarrier(data.stats, Renderer::BufferPassUsage::COMPUTE);
                }
                struct ExpandFinalizeConstants { u32 queueCapacity; u32 resourceIndex; };
                ExpandFinalizeConstants* expand = graphResources.FrameNew<ExpandFinalizeConstants>();
                expand->queueCapacity = queueCapacity;
                expand->resourceIndex = frameIndex;
                commandList.BeginPipeline(_expandFinalizePipeline);
                commandList.PushConstant(expand, 0, sizeof(ExpandFinalizeConstants));
                commandList.BindDescriptorSet(data.expandFinalizeSet, frameIndex);
                commandList.Dispatch(1, 1, 1);
                commandList.EndPipeline(_expandFinalizePipeline);

                if (inputCount > 0 && !resetHistory)
                {
                    commandList.BufferBarrier(data.instanceVisibility, Renderer::BufferPassUsage::COMPUTE);
                    struct ReplayConstants { u32 queueCapacity; u32 resourceIndex; u32 resetHistory; u32 reserved; };
                    ReplayConstants* replay = graphResources.FrameNew<ReplayConstants>();
                    replay->queueCapacity = queueCapacity;
                    replay->resourceIndex = frameIndex;
                    replay->resetHistory = resetHistory || ModelRendering::ShowModelCullReasons() ? 1u : 0u;
                    replay->reserved = 0;
                    commandList.BeginPipeline(_replayPipeline);
                    commandList.PushConstant(replay, 0, sizeof(ReplayConstants));
                    commandList.BindDescriptorSet(data.replaySet, frameIndex);
                    commandList.DispatchIndirect(data.previousSurvivorArguments, 0);
                    commandList.EndPipeline(_replayPipeline);
                }

                commandList.BufferBarrier(data.stats, Renderer::BufferPassUsage::COMPUTE);
                commandList.BeginPipeline(_finalizePipeline);
                commandList.PushConstant(constants, 0, sizeof(WorkConstants));
                commandList.BindDescriptorSet(data.finalizeSet, frameIndex);
                commandList.Dispatch(1, 1, 1);
                commandList.EndPipeline(_finalizePipeline);
                commandList.BufferBarrier(data.arguments, Renderer::BufferPassUsage::COMPUTE);
            });
    }

    void ModelViewWorkPass::AddPhase2Pass(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                                          const RenderScenes::RenderView& view,
                                          const ModelView::ModelViewState& viewState,
                                          ModelView::ModelViewWorkResources& work,
                                          const ModelLoading::ModelGeometryStorage& geometry,
                                          const RenderScenes::RenderScene& scene, Renderer::ImageID depthPyramid,
                                          u8 frameIndex)
    {
        const u32 configuredCapacity = static_cast<u32>(std::max(CVAR_ModelMeshletQueueCapacity.Get(), 1));
        u32 queueCapacity = std::min(work.GetQueueCapacity(), configuredCapacity);
        if (CVAR_ModelMeshletQueueLimit.Get() > 0)
            queueCapacity = std::min(queueCapacity, static_cast<u32>(CVAR_ModelMeshletQueueLimit.Get()));

        struct Data
        {
            Renderer::ImageResource depthPyramid;
            Renderer::BufferResource chunkArguments;
            Renderer::BufferMutableResource stats;
            Renderer::BufferMutableResource arguments;
            Renderer::BufferMutableResource readback;
            Renderer::DescriptorSetResource globalSet;
            Renderer::DescriptorSetResource cullSet;
            Renderer::DescriptorSetResource beginPhase2Set;
            Renderer::DescriptorSetResource finalizeSet;
        };

        renderGraph->AddPass<Data>(
            "Model Work Phase 2: " + view.GetDebugName(),
            [&resources, &viewState, &work, &geometry, &scene, depthPyramid, frameIndex,
             this](Data& data, Renderer::RenderGraphBuilder& builder) {
                using Usage = Renderer::BufferPassUsage;
                const u32 inactiveFrameIndex = (frameIndex + 1u) % ModelView::MODEL_VIEW_FRAME_COUNT;
                data.depthPyramid = builder.Read(depthPyramid, Renderer::PipelineType::COMPUTE);
                builder.Read(resources.cameras.GetBuffer(), Usage::COMPUTE);
                builder.Read(viewState.GetLODHistory().GetBuffer(), Usage::COMPUTE);
                builder.Read(scene.GetModelInstances().GetRecords().GetBuffer(), Usage::COMPUTE);
                builder.Read(geometry.GetRecords().GetBuffer(), Usage::COMPUTE);
                builder.Read(geometry.GetMeshes().GetBuffer(), Usage::COMPUTE);
                builder.Read(geometry.GetMeshlets().GetBuffer(), Usage::COMPUTE);
                builder.Read(work.GetChunkQueue(frameIndex), Usage::COMPUTE);
                data.chunkArguments = builder.Read(work.GetChunkArguments(frameIndex), Usage::COMPUTE);
                builder.Write(work.GetChunkQueue(inactiveFrameIndex), Usage::COMPUTE);
                builder.Write(work.GetChunkArguments(inactiveFrameIndex), Usage::COMPUTE);
                for (u32 rasterClass = 0; rasterClass < ModelView::MODEL_RASTER_CLASS_COUNT; ++rasterClass)
                {
                    builder.Write(work.GetQueue(rasterClass, frameIndex), Usage::COMPUTE);
                    builder.Write(work.GetQueue(rasterClass, inactiveFrameIndex), Usage::COMPUTE);
                }
                builder.Write(work.GetVisibilityRecords(frameIndex), Usage::COMPUTE);
                builder.Write(work.GetVisibilityRecords(inactiveFrameIndex), Usage::COMPUTE);
                builder.Write(work.GetCullReasons(frameIndex), Usage::COMPUTE);
                builder.Write(work.GetCullReasons(inactiveFrameIndex), Usage::COMPUTE);
                builder.Write(work.GetMeshletHistory(frameIndex), Usage::COMPUTE);
                builder.Write(work.GetMeshletHistory(inactiveFrameIndex), Usage::COMPUTE);
                builder.Write(work.GetSurvivorQueue(frameIndex), Usage::COMPUTE);
                builder.Write(work.GetSurvivorQueue(inactiveFrameIndex), Usage::COMPUTE);
                data.stats = builder.Write(work.GetStatsBuffer(frameIndex), Usage::COMPUTE | Usage::TRANSFER);
                builder.Write(work.GetStatsBuffer(inactiveFrameIndex), Usage::COMPUTE);
                data.arguments = builder.Write(work.GetArguments(frameIndex), Usage::COMPUTE | Usage::TRANSFER);
                builder.Write(work.GetArguments(inactiveFrameIndex), Usage::COMPUTE);
                builder.Write(work.GetSurvivorArguments(frameIndex), Usage::COMPUTE);
                builder.Write(work.GetSurvivorArguments(inactiveFrameIndex), Usage::COMPUTE);
                data.readback = builder.Write(work.GetStatsReadback(frameIndex), Usage::TRANSFER);
                data.globalSet = builder.Use(resources.globalDescriptorSet);
                data.cullSet = builder.Use(_cullDescriptorSet);
                data.beginPhase2Set = builder.Use(_beginPhase2DescriptorSet);
                data.finalizeSet = builder.Use(_finalizeDescriptorSet);
                return true;
            },
            [this, &view, &viewState, &work, queueCapacity, frameIndex](Data& data,
                Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) {
                GPU_SCOPED_PROFILER_ZONE(commandList, ModelViewPhase2);
                struct BeginConstants { u32 resourceIndex; };
                BeginConstants* begin = graphResources.FrameNew<BeginConstants>();
                begin->resourceIndex = frameIndex;
                commandList.BeginPipeline(_beginPhase2Pipeline);
                commandList.PushConstant(begin, 0, sizeof(BeginConstants));
                commandList.BindDescriptorSet(data.beginPhase2Set, frameIndex);
                commandList.Dispatch(1, 1, 1);
                commandList.EndPipeline(_beginPhase2Pipeline);
                commandList.BufferBarrier(data.stats, Renderer::BufferPassUsage::COMPUTE);
                commandList.BufferBarrier(data.arguments, Renderer::BufferPassUsage::COMPUTE);

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
                CullConstants* cull = graphResources.FrameNew<CullConstants>();
                cull->viewIndex = view.GetCameraIndex();
                cull->queueCapacity = queueCapacity;
                cull->resourceIndex = frameIndex;
                cull->enableConeCulling = CVAR_ModelMeshletConeCulling.Get() != 0 ? 1u : 0u;
                cull->enableOcclusionCulling = CVAR_ModelMeshletOcclusionCulling.Get() != 0 ? 1u : 0u;
                cull->viewportWidth = view.GetDimensions().x;
                cull->viewportHeight = view.GetDimensions().y;
                cull->enableTemporal = ModelRendering::ShowModelCullReasons() ? 0u : 1u;
                cull->showCullReasons = ModelRendering::ShowModelCullReasons() ? 1u : 0u;
                data.cullSet.Bind("_depthPyramid", data.depthPyramid);
                commandList.BeginPipeline(_cullPipeline);
                commandList.PushConstant(cull, 0, sizeof(CullConstants));
                commandList.BindDescriptorSet(data.globalSet, frameIndex);
                commandList.BindDescriptorSet(data.cullSet, frameIndex);
                commandList.DispatchIndirect(data.chunkArguments, 0);
                commandList.EndPipeline(_cullPipeline);

                commandList.BufferBarrier(data.stats, Renderer::BufferPassUsage::COMPUTE);
                struct WorkConstants
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
                WorkConstants* constants = graphResources.FrameNew<WorkConstants>();
                constants->viewIndex = view.GetCameraIndex();
                constants->inputCount = viewState.GetDispatchInputCount();
                constants->queueCapacity = queueCapacity;
                constants->forcedLOD = -1;
                constants->viewportHeight = static_cast<f32>(view.GetDimensions().y);
                constants->lodTargetPixels = 2.0f;
                constants->lodHysteresis = 0.15f;
                constants->resetHistory = 0;
                constants->resourceIndex = frameIndex;
                constants->showCullReasons = ModelRendering::ShowModelCullReasons() ? 1u : 0u;
                commandList.BeginPipeline(_finalizePipeline);
                commandList.PushConstant(constants, 0, sizeof(WorkConstants));
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
