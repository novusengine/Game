#include "ModelShadowPass.h"

#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Material/MaterialStorage.h"
#include "Game-Lib/Rendering/Model/Asset/ModelGeometryStorage.h"
#include "Game-Lib/Rendering/RenderResources.h"
#include "Game-Lib/Rendering/Scene/RenderScene.h"

#include <Renderer/Descriptors/ComputeShaderDesc.h>
#include <Renderer/RenderGraph.h>
#include <Renderer/Renderer.h>

namespace ShadowRendering
{
    ModelShadowPass::ModelShadowPass(Renderer::Renderer* renderer, GameRenderer* gameRenderer)
        : _renderer(renderer), _expandSet(Renderer::DescriptorSetSlot::PER_PASS),
          _expandFinalizeSet(Renderer::DescriptorSetSlot::PER_PASS), _cullSet(Renderer::DescriptorSetSlot::PER_PASS),
          _finalizeSet(Renderer::DescriptorSetSlot::PER_PASS),
          _staticSolidSet(Renderer::DescriptorSetSlot::PER_PASS),
          _staticAlphaSet(Renderer::DescriptorSetSlot::PER_PASS),
          _dynamicSolidSet(Renderer::DescriptorSetSlot::PER_PASS),
          _dynamicAlphaSet(Renderer::DescriptorSetSlot::PER_PASS)
    {
        Renderer::ComputeShaderDesc shader;
        Renderer::ComputePipelineDesc desc;
        shader.shaderEntry = gameRenderer->GetShaderEntry("Shadows/ModelShadowExpand.cs"_h,
                                                          "Shadows/ModelShadowExpand.cs");
        desc.computeShader = renderer->LoadShader(shader);
        desc.debugName = "Model Shadow Expand";
        _expandPipeline = renderer->CreatePipeline(desc);
        shader.shaderEntry = gameRenderer->GetShaderEntry("Shadows/ModelShadowExpandFinalize.cs"_h,
                                                          "Shadows/ModelShadowExpandFinalize.cs");
        desc.computeShader = renderer->LoadShader(shader);
        desc.debugName = "Model Shadow Expand Finalize";
        _expandFinalizePipeline = renderer->CreatePipeline(desc);
        shader.shaderEntry = gameRenderer->GetShaderEntry("Shadows/ModelShadowCull.cs"_h,
                                                          "Shadows/ModelShadowCull.cs");
        desc.computeShader = renderer->LoadShader(shader);
        desc.debugName = "Model Shadow Cull";
        _cullPipeline = renderer->CreatePipeline(desc);
        shader.shaderEntry = gameRenderer->GetShaderEntry("Shadows/ModelShadowFinalize.cs"_h,
                                                          "Shadows/ModelShadowFinalize.cs");
        desc.computeShader = renderer->LoadShader(shader);
        desc.debugName = "Model Shadow Finalize";
        _finalizePipeline = renderer->CreatePipeline(desc);

        _expandSet.RegisterPipeline(renderer, _expandPipeline);
        _expandFinalizeSet.RegisterPipeline(renderer, _expandFinalizePipeline);
        _cullSet.RegisterPipeline(renderer, _cullPipeline);
        _finalizeSet.RegisterPipeline(renderer, _finalizePipeline);
        _expandSet.Init(renderer);
        _expandFinalizeSet.Init(renderer);
        _cullSet.Init(renderer);
        _finalizeSet.Init(renderer);

        Renderer::MeshShaderDesc solidMesh;
        solidMesh.shaderEntry = gameRenderer->GetShaderEntry("Shadows/ModelShadowSolid.ms"_h,
                                                             "Shadows/ModelShadowSolid.ms");
        Renderer::MeshShaderDesc alphaMesh;
        alphaMesh.shaderEntry = gameRenderer->GetShaderEntry("Shadows/ModelShadowAlphaTest.ms"_h,
                                                             "Shadows/ModelShadowAlphaTest.ms");
        Renderer::PixelShaderDesc solidPixel;
        solidPixel.shaderEntry = gameRenderer->GetShaderEntry("Shadows/ModelShadowSolid.ps"_h,
                                                              "Shadows/ModelShadowSolid.ps");
        Renderer::PixelShaderDesc alphaPixel;
        alphaPixel.shaderEntry = gameRenderer->GetShaderEntry("Shadows/ModelShadowAlphaTest.ps"_h,
                                                              "Shadows/ModelShadowAlphaTest.ps");

        Renderer::GraphicsPipelineDesc rasterDesc;
        rasterDesc.debugName = "Model Shadow Solid One Sided";
        rasterDesc.shaderStages = Renderer::MeshPipelineStages{.meshShader = renderer->LoadShader(solidMesh)};
        rasterDesc.states.pixelShader = renderer->LoadShader(solidPixel);
        // Legacy model shadows rasterized every caster two-sided. Model materials use sidedness
        // for visible rendering, but existing assets do not guarantee shadow-safe winding.
        rasterDesc.states.rasterizerState.cullMode = Renderer::CullMode::NONE;
        rasterDesc.states.rasterizerState.frontFaceMode = Renderer::FrontFaceState::COUNTERCLOCKWISE;
        rasterDesc.states.rasterizerState.depthClampEnabled = true;
        _solidOneSidedPipeline = renderer->CreatePipeline(rasterDesc);
        rasterDesc.debugName = "Model Shadow Solid Two Sided";
        rasterDesc.states.rasterizerState.cullMode = Renderer::CullMode::NONE;
        _solidTwoSidedPipeline = renderer->CreatePipeline(rasterDesc);

        rasterDesc.debugName = "Model Shadow Alpha Test One Sided";
        rasterDesc.shaderStages = Renderer::MeshPipelineStages{.meshShader = renderer->LoadShader(alphaMesh)};
        rasterDesc.states.pixelShader = renderer->LoadShader(alphaPixel);
        rasterDesc.states.rasterizerState.cullMode = Renderer::CullMode::NONE;
        _alphaOneSidedPipeline = renderer->CreatePipeline(rasterDesc);
        rasterDesc.debugName = "Model Shadow Alpha Test Two Sided";
        rasterDesc.states.rasterizerState.cullMode = Renderer::CullMode::NONE;
        _alphaTwoSidedPipeline = renderer->CreatePipeline(rasterDesc);

        for (Renderer::DescriptorSet* set : {&_staticSolidSet, &_dynamicSolidSet})
        {
            set->RegisterPipeline(renderer, _solidOneSidedPipeline);
            set->RegisterPipeline(renderer, _solidTwoSidedPipeline);
            set->Init(renderer);
        }
        for (Renderer::DescriptorSet* set : {&_staticAlphaSet, &_dynamicAlphaSet})
        {
            set->RegisterPipeline(renderer, _alphaOneSidedPipeline);
            set->RegisterPipeline(renderer, _alphaTwoSidedPipeline);
            set->Init(renderer);
        }
    }

    bool ModelShadowPass::Bind(Renderer::DescriptorSet& set, StringUtils::StringHash name,
                               Renderer::BufferID buffer, Renderer::BufferID& current)
    {
        if (current == buffer)
            return false;
        set.Bind(name, buffer);
        current = buffer;
        return true;
    }

    bool ModelShadowPass::UploadCullBindings(const ModelShadowWorkResources& work,
                                             const ModelLoading::ModelGeometryStorage& geometry,
                                             const RenderScenes::RenderScene& scene, Renderer::BufferID svsmData)
    {
        bool changed = false;
        auto Bind = [&](Renderer::DescriptorSet& set, StringUtils::StringHash hash, Renderer::BufferID buffer) {
            Renderer::BufferID& current = _cullBindings[hash.computedHash];
            changed |= this->Bind(set, hash, buffer, current);
        };

        Bind(_expandSet, "_shadowModelInstances"_h, scene.GetModelInstances().GetRecords().GetBuffer());
        Bind(_expandSet, "_shadowModels"_h, geometry.GetRecords().GetBuffer());
        Bind(_expandSet, "_shadowMeshes"_h, geometry.GetMeshes().GetBuffer());
        Bind(_expandSet, "_shadowLODs"_h, geometry.GetMeshLODs().GetBuffer());
        Bind(_expandSet, "_shadowSubmeshes"_h, geometry.GetSubmeshes().GetBuffer());
        Bind(_expandSet, "_shadowGeometryGroupMasks"_h, scene.GetGeometryGroupMasks().GetMasks().GetBuffer());
        Bind(_expandSet, "_shadowMaterialTable"_h, scene.GetModelMaterialTables().GetEntries().GetBuffer());
        Bind(_expandSet, "_shadowSVSMData"_h, svsmData);
        Bind(_cullSet, "_shadowCullInstances"_h, scene.GetModelInstances().GetRecords().GetBuffer());
        Bind(_cullSet, "_shadowCullModels"_h, geometry.GetRecords().GetBuffer());
        Bind(_cullSet, "_shadowCullMeshlets"_h, geometry.GetMeshlets().GetBuffer());
        Bind(_cullSet, "_shadowCullSVSMData"_h, svsmData);

        static constexpr StringUtils::StringHash CHUNKS[MODEL_SHADOW_FRAME_COUNT] = {"_shadowChunks0"_h, "_shadowChunks1"_h};
        static constexpr StringUtils::StringHash STATS[MODEL_SHADOW_FRAME_COUNT] = {"_shadowStats0"_h, "_shadowStats1"_h};
        static constexpr StringUtils::StringHash EXPAND_STATS[MODEL_SHADOW_FRAME_COUNT] = {"_shadowExpandStats0"_h, "_shadowExpandStats1"_h};
        static constexpr StringUtils::StringHash CHUNK_ARGUMENTS[MODEL_SHADOW_FRAME_COUNT] = {"_shadowChunkArguments0"_h, "_shadowChunkArguments1"_h};
        static constexpr StringUtils::StringHash CULL_CHUNKS[MODEL_SHADOW_FRAME_COUNT] = {"_shadowCullChunks0"_h, "_shadowCullChunks1"_h};
        static constexpr StringUtils::StringHash RECORDS[MODEL_SHADOW_FRAME_COUNT] = {"_shadowRecords0"_h, "_shadowRecords1"_h};
        static constexpr StringUtils::StringHash CULL_STATS[MODEL_SHADOW_FRAME_COUNT] = {"_shadowCullStats0"_h, "_shadowCullStats1"_h};
        static constexpr StringUtils::StringHash FINALIZE_STATS[MODEL_SHADOW_FRAME_COUNT] = {"_shadowFinalizeStats0"_h, "_shadowFinalizeStats1"_h};
        static constexpr StringUtils::StringHash ARGUMENTS[MODEL_SHADOW_FRAME_COUNT] = {"_shadowArguments0"_h, "_shadowArguments1"_h};
        static constexpr StringUtils::StringHash QUEUES[MODEL_SHADOW_FRAME_COUNT][MODEL_SHADOW_QUEUE_COUNT] = {
            {"_shadowQueue0_0"_h, "_shadowQueue1_0"_h, "_shadowQueue2_0"_h, "_shadowQueue3_0"_h,
             "_shadowQueue4_0"_h, "_shadowQueue5_0"_h, "_shadowQueue6_0"_h, "_shadowQueue7_0"_h},
            {"_shadowQueue0_1"_h, "_shadowQueue1_1"_h, "_shadowQueue2_1"_h, "_shadowQueue3_1"_h,
             "_shadowQueue4_1"_h, "_shadowQueue5_1"_h, "_shadowQueue6_1"_h, "_shadowQueue7_1"_h}};

        for (u32 frame = 0; frame < MODEL_SHADOW_FRAME_COUNT; ++frame)
        {
            Bind(_expandSet, CHUNKS[frame], work.GetChunkQueue(frame));
            Bind(_expandSet, STATS[frame], work.GetStatsBuffer(frame));
            Bind(_expandFinalizeSet, EXPAND_STATS[frame], work.GetStatsBuffer(frame));
            Bind(_expandFinalizeSet, CHUNK_ARGUMENTS[frame], work.GetChunkArguments(frame));
            Bind(_cullSet, CULL_CHUNKS[frame], work.GetChunkQueue(frame));
            Bind(_cullSet, RECORDS[frame], work.GetRecords(frame));
            Bind(_cullSet, CULL_STATS[frame], work.GetStatsBuffer(frame));
            Bind(_finalizeSet, FINALIZE_STATS[frame], work.GetStatsBuffer(frame));
            Bind(_finalizeSet, ARGUMENTS[frame], work.GetArguments(frame));
            for (u32 queue = 0; queue < MODEL_SHADOW_QUEUE_COUNT; ++queue)
                Bind(_cullSet, QUEUES[frame][queue], work.GetQueue(queue, frame));
        }

        if (changed)
            _cullDescriptorWarmupFrames = _renderer->GetFrameIndexCount();
        else if (_cullDescriptorWarmupFrames > 0)
            --_cullDescriptorWarmupFrames;
        return _cullDescriptorWarmupFrames == 0;
    }

    void ModelShadowPass::AddCullPass(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                                      ModelShadowWorkResources& work,
                                      const ModelLoading::ModelGeometryStorage& geometry,
                                      const MaterialLoading::MaterialStorage& materials,
                                      const RenderScenes::RenderScene& scene, Renderer::BufferID svsmData,
                                      u32 numClipmaps, bool dynamicSplit, u8 frameIndex, i32 forcedLOD,
                                      f32 lodTargetTexels)
    {
        const u32 instanceCapacity = scene.GetStats().instances.slotCapacity;
        struct Data
        {
            Renderer::BufferMutableResource stats;
            Renderer::BufferMutableResource chunks;
            Renderer::BufferMutableResource chunkArguments;
            Renderer::BufferMutableResource records;
            Renderer::BufferMutableResource queues[MODEL_SHADOW_QUEUE_COUNT];
            Renderer::BufferMutableResource arguments;
            Renderer::BufferMutableResource readback;
            Renderer::DescriptorSetResource globalSet;
            Renderer::DescriptorSetResource materialSet;
            Renderer::DescriptorSetResource expandSet;
            Renderer::DescriptorSetResource expandFinalizeSet;
            Renderer::DescriptorSetResource cullSet;
            Renderer::DescriptorSetResource finalizeSet;
        };

        renderGraph->AddPass<Data>("Model Shadow Work",
            [this, &resources, &work, &geometry, &materials, &scene, svsmData, frameIndex](Data& data,
                Renderer::RenderGraphBuilder& builder) {
                using Usage = Renderer::BufferPassUsage;
                builder.Read(resources.cameras.GetBuffer(), Usage::COMPUTE);
                builder.Read(scene.GetModelInstances().GetRecords().GetBuffer(), Usage::COMPUTE);
                builder.Read(geometry.GetRecords().GetBuffer(), Usage::COMPUTE);
                builder.Read(geometry.GetMeshes().GetBuffer(), Usage::COMPUTE);
                builder.Read(geometry.GetMeshLODs().GetBuffer(), Usage::COMPUTE);
                builder.Read(geometry.GetSubmeshes().GetBuffer(), Usage::COMPUTE);
                builder.Read(geometry.GetMeshlets().GetBuffer(), Usage::COMPUTE);
                builder.Read(scene.GetGeometryGroupMasks().GetMasks().GetBuffer(), Usage::COMPUTE);
                builder.Read(scene.GetModelMaterialTables().GetEntries().GetBuffer(), Usage::COMPUTE);
                builder.Read(materials.GetMaterialInstances().GetBuffer(), Usage::COMPUTE);
                builder.Read(svsmData, Usage::COMPUTE);
                data.stats = builder.Write(work.GetStatsBuffer(frameIndex), Usage::COMPUTE | Usage::TRANSFER);
                data.chunks = builder.Write(work.GetChunkQueue(frameIndex), Usage::COMPUTE);
                data.chunkArguments = builder.Write(work.GetChunkArguments(frameIndex), Usage::COMPUTE);
                data.records = builder.Write(work.GetRecords(frameIndex), Usage::COMPUTE);
                for (u32 queue = 0; queue < MODEL_SHADOW_QUEUE_COUNT; ++queue)
                    data.queues[queue] = builder.Write(work.GetQueue(queue, frameIndex), Usage::COMPUTE);
                data.arguments = builder.Write(work.GetArguments(frameIndex), Usage::COMPUTE);
                const u8 inactive = !frameIndex;
                builder.Write(work.GetStatsBuffer(inactive), Usage::COMPUTE);
                builder.Write(work.GetChunkQueue(inactive), Usage::COMPUTE);
                builder.Write(work.GetChunkArguments(inactive), Usage::COMPUTE);
                builder.Write(work.GetRecords(inactive), Usage::COMPUTE);
                for (u32 queue = 0; queue < MODEL_SHADOW_QUEUE_COUNT; ++queue)
                    builder.Write(work.GetQueue(queue, inactive), Usage::COMPUTE);
                builder.Write(work.GetArguments(inactive), Usage::COMPUTE);
                data.readback = builder.Write(work.GetStatsReadback(frameIndex), Usage::TRANSFER);
                data.globalSet = builder.Use(resources.globalDescriptorSet);
                data.materialSet = builder.Use(resources.materialDescriptorSet);
                data.expandSet = builder.Use(_expandSet);
                data.expandFinalizeSet = builder.Use(_expandFinalizeSet);
                data.cullSet = builder.Use(_cullSet);
                data.finalizeSet = builder.Use(_finalizeSet);
                return true;
            },
            [this, &work, instanceCapacity, numClipmaps, dynamicSplit, frameIndex, forcedLOD,
             lodTargetTexels](Data& data, Renderer::RenderGraphResources& graphResources,
                              Renderer::CommandList& commandList) {
                GPU_SCOPED_PROFILER_ZONE(commandList, ModelShadowWork);
                commandList.FillBuffer(data.stats, 0, sizeof(ModelShadowStats), 0);
                commandList.BufferBarrier(data.stats, Renderer::BufferPassUsage::TRANSFER);

                struct ExpandConstants
                {
                    u32 instanceCapacity;
                    u32 numClipmaps;
                    u32 queueCapacity;
                    i32 forcedLOD;
                    f32 lodTargetTexels;
                    u32 dynamicSplit;
                    u32 frameIndex;
                };
                ExpandConstants* expand = graphResources.FrameNew<ExpandConstants>();
                *expand = {instanceCapacity, numClipmaps, work.GetCapacity(), forcedLOD, lodTargetTexels,
                           dynamicSplit ? 1u : 0u, frameIndex};
                commandList.BeginPipeline(_expandPipeline);
                commandList.PushConstant(expand, 0, sizeof(*expand));
                commandList.BindDescriptorSet(data.globalSet, frameIndex);
                commandList.BindDescriptorSet(data.materialSet, frameIndex);
                commandList.BindDescriptorSet(data.expandSet, frameIndex);
                const u32 instanceGroups = (instanceCapacity + MODEL_SHADOW_COMPUTE_THREAD_COUNT - 1u) / MODEL_SHADOW_COMPUTE_THREAD_COUNT;
                commandList.Dispatch(instanceGroups, numClipmaps, 1);
                commandList.EndPipeline(_expandPipeline);
                commandList.BufferBarrier(data.stats, Renderer::BufferPassUsage::COMPUTE);
                commandList.BufferBarrier(data.chunks, Renderer::BufferPassUsage::COMPUTE);

                struct FinalizeConstants { u32 queueCapacity; u32 frameIndex; };
                FinalizeConstants* finalize = graphResources.FrameNew<FinalizeConstants>();
                *finalize = {work.GetCapacity(), frameIndex};
                commandList.BeginPipeline(_expandFinalizePipeline);
                commandList.PushConstant(finalize, 0, sizeof(*finalize));
                commandList.BindDescriptorSet(data.expandFinalizeSet, frameIndex);
                commandList.Dispatch(1, 1, 1);
                commandList.EndPipeline(_expandFinalizePipeline);
                commandList.BufferBarrier(data.chunkArguments, Renderer::BufferPassUsage::COMPUTE);

                struct CullConstants { u32 queueCapacity; u32 frameIndex; };
                CullConstants* cull = graphResources.FrameNew<CullConstants>();
                *cull = {work.GetCapacity(), frameIndex};
                commandList.BeginPipeline(_cullPipeline);
                commandList.PushConstant(cull, 0, sizeof(*cull));
                commandList.BindDescriptorSet(data.globalSet, frameIndex);
                commandList.BindDescriptorSet(data.cullSet, frameIndex);
                commandList.DispatchIndirect(data.chunkArguments, 0);
                commandList.EndPipeline(_cullPipeline);
                commandList.BufferBarrier(data.stats, Renderer::BufferPassUsage::COMPUTE);

                commandList.BeginPipeline(_finalizePipeline);
                commandList.PushConstant(finalize, 0, sizeof(*finalize));
                commandList.BindDescriptorSet(data.finalizeSet, frameIndex);
                commandList.Dispatch(1, 1, 1);
                commandList.EndPipeline(_finalizePipeline);
                commandList.CopyBuffer(data.readback, 0, data.stats, 0, sizeof(ModelShadowStats));
            });
        work.MarkSubmitted(frameIndex);
    }

    void ModelShadowPass::BindRasterShared(Renderer::DescriptorSet& set, u32 setIndex, bool alphaTest,
                                           const ModelShadowWorkResources& work,
                                           const ModelLoading::ModelGeometryStorage& geometry,
                                           const RenderScenes::RenderScene& scene, Renderer::BufferID svsmData,
                                           Renderer::BufferID pageTable, bool& changed)
    {
        const u64 setKey = static_cast<u64>(setIndex) << 32u;
        auto Bind = [&](StringUtils::StringHash hash, Renderer::BufferID buffer) {
            Renderer::BufferID& current = _rasterBindings[setKey | hash.computedHash];
            changed |= this->Bind(set, hash, buffer, current);
        };
        static constexpr StringUtils::StringHash QUEUES[MODEL_SHADOW_FRAME_COUNT][MODEL_SHADOW_QUEUE_COUNT] = {
            {"_shadowRasterQueue0_0"_h, "_shadowRasterQueue1_0"_h, "_shadowRasterQueue2_0"_h,
             "_shadowRasterQueue3_0"_h, "_shadowRasterQueue4_0"_h, "_shadowRasterQueue5_0"_h,
             "_shadowRasterQueue6_0"_h, "_shadowRasterQueue7_0"_h},
            {"_shadowRasterQueue0_1"_h, "_shadowRasterQueue1_1"_h, "_shadowRasterQueue2_1"_h,
             "_shadowRasterQueue3_1"_h, "_shadowRasterQueue4_1"_h, "_shadowRasterQueue5_1"_h,
             "_shadowRasterQueue6_1"_h, "_shadowRasterQueue7_1"_h}};
        static constexpr StringUtils::StringHash RECORDS[MODEL_SHADOW_FRAME_COUNT] = {"_shadowRasterRecords0"_h, "_shadowRasterRecords1"_h};
        static constexpr StringUtils::StringHash STATS[MODEL_SHADOW_FRAME_COUNT] = {"_shadowRasterStats0"_h, "_shadowRasterStats1"_h};
        for (u32 frame = 0; frame < MODEL_SHADOW_FRAME_COUNT; ++frame)
        {
            for (u32 queue = 0; queue < MODEL_SHADOW_QUEUE_COUNT; ++queue)
                Bind(QUEUES[frame][queue], work.GetQueue(queue, frame));
            Bind(RECORDS[frame], work.GetRecords(frame));
            Bind(STATS[frame], work.GetStatsBuffer(frame));
        }
        Bind("_shadowRasterInstances"_h, scene.GetModelInstances().GetRecords().GetBuffer());
        Bind("_shadowRasterModels"_h, geometry.GetRecords().GetBuffer());
        Bind("_shadowRasterMeshes"_h, geometry.GetMeshes().GetBuffer());
        Bind("_shadowRasterLODs"_h, geometry.GetMeshLODs().GetBuffer());
        Bind("_shadowRasterSubmeshes"_h, geometry.GetSubmeshes().GetBuffer());
        Bind("_shadowRasterMeshlets"_h, geometry.GetMeshlets().GetBuffer());
        Bind("_shadowRasterPositions"_h, geometry.GetPositions().GetBuffer());
        Bind("_shadowRasterVertexIndices"_h, geometry.GetMeshletVertexIndices().GetBuffer());
        Bind("_shadowRasterTriangles"_h, geometry.GetMeshletTriangles().GetBuffer());
        if (alphaTest)
        {
            Bind("_shadowRasterVertexAttributes"_h, geometry.GetVertexAttributes().GetBuffer());
            Bind("_shadowRasterMaterialTable"_h, scene.GetModelMaterialTables().GetEntries().GetBuffer());
        }
        Bind("_shadowRasterSVSMData"_h, svsmData);
        Bind("_shadowRasterPageTable"_h, pageTable);
    }

    bool ModelShadowPass::UploadRasterBindings(const ModelShadowWorkResources& work,
                                               const ModelLoading::ModelGeometryStorage& geometry,
                                               const RenderScenes::RenderScene& scene, Renderer::BufferID svsmData,
                                               Renderer::BufferID staticPageTable,
                                               Renderer::BufferID dynamicPageTable)
    {
        bool changed = false;
        BindRasterShared(_staticSolidSet, 0, false, work, geometry, scene, svsmData, staticPageTable, changed);
        BindRasterShared(_staticAlphaSet, 1, true, work, geometry, scene, svsmData, staticPageTable, changed);
        BindRasterShared(_dynamicSolidSet, 2, false, work, geometry, scene, svsmData, dynamicPageTable, changed);
        BindRasterShared(_dynamicAlphaSet, 3, true, work, geometry, scene, svsmData, dynamicPageTable, changed);
        if (changed)
            _rasterDescriptorWarmupFrames = _renderer->GetFrameIndexCount();
        else if (_rasterDescriptorWarmupFrames > 0)
            --_rasterDescriptorWarmupFrames;
        return _rasterDescriptorWarmupFrames == 0;
    }

    bool ModelShadowPass::Upload(const ModelShadowWorkResources& work,
                                 const ModelLoading::ModelGeometryStorage& geometry,
                                 const RenderScenes::RenderScene& scene, Renderer::BufferID svsmData,
                                 Renderer::BufferID staticPageTable, Renderer::BufferID dynamicPageTable)
    {
        if (_generation != work.GetGeneration())
        {
            _generation = work.GetGeneration();
            _cullBindings.clear();
            _rasterBindings.clear();
        }
        const bool cullReady = UploadCullBindings(work, geometry, scene, svsmData);
        const bool rasterReady = UploadRasterBindings(work, geometry, scene, svsmData, staticPageTable,
                                                      dynamicPageTable);
        return cullReady && rasterReady;
    }

    void ModelShadowPass::AddRasterPass(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                                        const ModelShadowWorkResources& work,
                                        const ModelLoading::ModelGeometryStorage& geometry,
                                        const MaterialLoading::MaterialStorage& materials,
                                        const RenderScenes::RenderScene& scene, Renderer::BufferID svsmData,
                                        Renderer::BufferID staticPageTable, Renderer::BufferID dynamicPageTable,
                                        Renderer::ImageID staticPagePool, Renderer::ImageID dynamicPagePool,
                                        u32 virtualSize, bool dynamicSplit, bool opacityDither, u8 frameIndex)
    {
        struct Data
        {
            Renderer::ImageMutableResource staticPool;
            Renderer::ImageMutableResource dynamicPool;
            Renderer::BufferResource arguments;
            Renderer::DescriptorSetResource globalSet;
            Renderer::DescriptorSetResource materialSet;
            Renderer::DescriptorSetResource staticSolidSet;
            Renderer::DescriptorSetResource staticAlphaSet;
            Renderer::DescriptorSetResource dynamicSolidSet;
            Renderer::DescriptorSetResource dynamicAlphaSet;
        };
        renderGraph->AddPass<Data>("Model Shadow Raster",
            [this, &resources, &work, &geometry, &materials, &scene, svsmData, staticPageTable, dynamicPageTable,
             staticPagePool, dynamicPagePool, dynamicSplit, frameIndex](Data& data,
                Renderer::RenderGraphBuilder& builder) {
                using Usage = Renderer::BufferPassUsage;
                data.staticPool = builder.Write(staticPagePool, Renderer::PipelineType::GRAPHICS,
                                                Renderer::LoadMode::LOAD);
                if (dynamicSplit)
                    data.dynamicPool = builder.Write(dynamicPagePool, Renderer::PipelineType::GRAPHICS,
                                                     Renderer::LoadMode::LOAD);
                builder.Read(resources.cameras.GetBuffer(), Usage::GRAPHICS);
                builder.Read(svsmData, Usage::GRAPHICS);
                builder.Read(staticPageTable, Usage::GRAPHICS);
                if (dynamicSplit)
                    builder.Read(dynamicPageTable, Usage::GRAPHICS);
                for (u32 frame = 0; frame < MODEL_SHADOW_FRAME_COUNT; ++frame)
                {
                    for (u32 queue = 0; queue < MODEL_SHADOW_QUEUE_COUNT; ++queue)
                        builder.Read(work.GetQueue(queue, frame), Usage::GRAPHICS);
                    builder.Read(work.GetRecords(frame), Usage::GRAPHICS);
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
                data.materialSet = builder.Use(resources.materialDescriptorSet);
                data.staticSolidSet = builder.Use(_staticSolidSet);
                data.staticAlphaSet = builder.Use(_staticAlphaSet);
                if (dynamicSplit)
                {
                    data.dynamicSolidSet = builder.Use(_dynamicSolidSet);
                    data.dynamicAlphaSet = builder.Use(_dynamicAlphaSet);
                }
                return true;
            },
            [this, &work, virtualSize, dynamicSplit, opacityDither, frameIndex](Data& data,
                Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) {
                GPU_SCOPED_PROFILER_ZONE(commandList, ModelShadowRaster);
                data.staticSolidSet.Bind("_shadowRasterPagePool"_h, data.staticPool);
                data.staticAlphaSet.Bind("_shadowRasterPagePool"_h, data.staticPool);
                if (dynamicSplit)
                {
                    data.dynamicSolidSet.Bind("_shadowRasterPagePool"_h, data.dynamicPool);
                    data.dynamicAlphaSet.Bind("_shadowRasterPagePool"_h, data.dynamicPool);
                }
                Renderer::RenderPassDesc pass;
                graphResources.InitializeRenderPassDesc(pass);
                pass.extent = uvec2(virtualSize);
                commandList.SetViewport(0, 0, static_cast<f32>(virtualSize), static_cast<f32>(virtualSize), 0.0f, 1.0f);
                commandList.SetScissorRect(0, virtualSize, 0, virtualSize);
                commandList.BeginRenderPass(pass);

                struct Constants
                {
                    u32 queueIndex;
                    u32 frameIndex;
                    u32 dynamicCaster;
                    u32 opacityDither;
                    u32 queueCapacity;
                };
                auto Draw = [&](Renderer::GraphicsPipelineID pipeline, u32 queueIndex, bool alphaTest,
                                Renderer::DescriptorSetResource& set) {
                    Constants* constants = graphResources.FrameNew<Constants>();
                    *constants = {queueIndex, frameIndex, queueIndex >= MODEL_SHADOW_RASTER_CLASS_COUNT ? 1u : 0u,
                                  opacityDither ? 1u : 0u, work.GetCapacity()};
                    commandList.BeginPipeline(pipeline);
                    commandList.PushConstant(constants, 0, sizeof(*constants));
                    commandList.BindDescriptorSet(data.globalSet, frameIndex);
                    if (alphaTest)
                        commandList.BindDescriptorSet(data.materialSet, frameIndex);
                    commandList.BindDescriptorSet(set, frameIndex);
                    commandList.DrawMeshTasksIndirect(data.arguments, queueIndex * sizeof(u32) *
                                                      MODEL_SHADOW_DISPATCH_ARGUMENT_COUNT);
                    commandList.EndPipeline(pipeline);
                };
                Draw(_solidOneSidedPipeline, ModelShadowQueueIndex(ModelShadowRasterClass::SolidOneSided, false), false, data.staticSolidSet);
                Draw(_solidTwoSidedPipeline, ModelShadowQueueIndex(ModelShadowRasterClass::SolidTwoSided, false), false, data.staticSolidSet);
                Draw(_alphaOneSidedPipeline, ModelShadowQueueIndex(ModelShadowRasterClass::AlphaTestOneSided, false), true, data.staticAlphaSet);
                Draw(_alphaTwoSidedPipeline, ModelShadowQueueIndex(ModelShadowRasterClass::AlphaTestTwoSided, false), true, data.staticAlphaSet);
                if (dynamicSplit)
                {
                    Draw(_solidOneSidedPipeline, ModelShadowQueueIndex(ModelShadowRasterClass::SolidOneSided, true), false, data.dynamicSolidSet);
                    Draw(_solidTwoSidedPipeline, ModelShadowQueueIndex(ModelShadowRasterClass::SolidTwoSided, true), false, data.dynamicSolidSet);
                    Draw(_alphaOneSidedPipeline, ModelShadowQueueIndex(ModelShadowRasterClass::AlphaTestOneSided, true), true, data.dynamicAlphaSet);
                    Draw(_alphaTwoSidedPipeline, ModelShadowQueueIndex(ModelShadowRasterClass::AlphaTestTwoSided, true), true, data.dynamicAlphaSet);
                }
                commandList.EndRenderPass(pass);
            });
    }
} // namespace ShadowRendering
