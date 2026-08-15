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
        rasterDesc.states.rasterizerState.cullMode = Renderer::CullMode::BACK;
        rasterDesc.states.rasterizerState.frontFaceMode = Renderer::FrontFaceState::COUNTERCLOCKWISE;
        rasterDesc.states.rasterizerState.depthClampEnabled = true;
        _solidOneSidedPipeline = renderer->CreatePipeline(rasterDesc);
        rasterDesc.debugName = "Model Shadow Solid Two Sided";
        rasterDesc.states.rasterizerState.cullMode = Renderer::CullMode::NONE;
        _solidTwoSidedPipeline = renderer->CreatePipeline(rasterDesc);

        rasterDesc.debugName = "Model Shadow Alpha Test One Sided";
        rasterDesc.shaderStages = Renderer::MeshPipelineStages{.meshShader = renderer->LoadShader(alphaMesh)};
        rasterDesc.states.pixelShader = renderer->LoadShader(alphaPixel);
        rasterDesc.states.rasterizerState.cullMode = Renderer::CullMode::BACK;
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
        auto bind = [&](Renderer::DescriptorSet& set, StringUtils::StringHash hash, Renderer::BufferID buffer) {
            Renderer::BufferID& current = _cullBindings[hash.computedHash];
            changed |= Bind(set, hash, buffer, current);
        };

        bind(_expandSet, "_shadowModelInstances"_h, scene.GetModelInstances().GetRecords().GetBuffer());
        bind(_expandSet, "_shadowModels"_h, geometry.GetRecords().GetBuffer());
        bind(_expandSet, "_shadowMeshes"_h, geometry.GetMeshes().GetBuffer());
        bind(_expandSet, "_shadowLODs"_h, geometry.GetMeshLODs().GetBuffer());
        bind(_expandSet, "_shadowSubmeshes"_h, geometry.GetSubmeshes().GetBuffer());
        bind(_expandSet, "_shadowGeometryGroupMasks"_h, scene.GetGeometryGroupMasks().GetMasks().GetBuffer());
        bind(_expandSet, "_shadowMaterialTable"_h, scene.GetModelMaterialTables().GetEntries().GetBuffer());
        bind(_expandSet, "_shadowSVSMData"_h, svsmData);
        bind(_cullSet, "_shadowCullInstances"_h, scene.GetModelInstances().GetRecords().GetBuffer());
        bind(_cullSet, "_shadowCullModels"_h, geometry.GetRecords().GetBuffer());
        bind(_cullSet, "_shadowCullMeshlets"_h, geometry.GetMeshlets().GetBuffer());
        bind(_cullSet, "_shadowCullSVSMData"_h, svsmData);

        for (u32 frame = 0; frame < MODEL_SHADOW_FRAME_COUNT; ++frame)
        {
            const std::string suffix = std::to_string(frame);
            auto bindFrame = [&](Renderer::DescriptorSet& set, const std::string& name, Renderer::BufferID buffer) {
                const StringUtils::StringHash hash = StringUtils::fnv1a_32(name.c_str(), name.size());
                Renderer::BufferID& current = _cullBindings[hash.computedHash];
                changed |= Bind(set, hash, buffer, current);
            };
            bindFrame(_expandSet, "_shadowChunks" + suffix, work.GetChunkQueue(frame));
            bindFrame(_expandSet, "_shadowStats" + suffix, work.GetStatsBuffer(frame));
            bindFrame(_expandFinalizeSet, "_shadowExpandStats" + suffix, work.GetStatsBuffer(frame));
            bindFrame(_expandFinalizeSet, "_shadowChunkArguments" + suffix, work.GetChunkArguments(frame));
            bindFrame(_cullSet, "_shadowCullChunks" + suffix, work.GetChunkQueue(frame));
            bindFrame(_cullSet, "_shadowRecords" + suffix, work.GetRecords(frame));
            bindFrame(_cullSet, "_shadowCullStats" + suffix, work.GetStatsBuffer(frame));
            bindFrame(_finalizeSet, "_shadowFinalizeStats" + suffix, work.GetStatsBuffer(frame));
            bindFrame(_finalizeSet, "_shadowArguments" + suffix, work.GetArguments(frame));
            for (u32 queue = 0; queue < MODEL_SHADOW_QUEUE_COUNT; ++queue)
                bindFrame(_cullSet, "_shadowQueue" + std::to_string(queue) + "_" + suffix,
                          work.GetQueue(queue, frame));
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
                                      f32 lodTargetTexels, bool coneCulling)
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
            [this, &work, instanceCapacity, numClipmaps, dynamicSplit, frameIndex, forcedLOD, lodTargetTexels,
             coneCulling](Data& data, Renderer::RenderGraphResources& graphResources,
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

                struct CullConstants { u32 queueCapacity; u32 frameIndex; u32 enableConeCulling; };
                CullConstants* cull = graphResources.FrameNew<CullConstants>();
                *cull = {work.GetCapacity(), frameIndex, coneCulling ? 1u : 0u};
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
        auto bind = [&](const std::string& name, Renderer::BufferID buffer) {
            const StringUtils::StringHash hash(name);
            Renderer::BufferID& current = _rasterBindings[setKey | hash.computedHash];
            changed |= Bind(set, hash, buffer, current);
        };
        for (u32 frame = 0; frame < MODEL_SHADOW_FRAME_COUNT; ++frame)
        {
            const std::string suffix = std::to_string(frame);
            for (u32 queue = 0; queue < MODEL_SHADOW_QUEUE_COUNT; ++queue)
                bind("_shadowRasterQueue" + std::to_string(queue) + "_" + suffix, work.GetQueue(queue, frame));
            bind("_shadowRasterRecords" + suffix, work.GetRecords(frame));
        }
        bind("_shadowRasterInstances", scene.GetModelInstances().GetRecords().GetBuffer());
        bind("_shadowRasterModels", geometry.GetRecords().GetBuffer());
        bind("_shadowRasterMeshes", geometry.GetMeshes().GetBuffer());
        bind("_shadowRasterLODs", geometry.GetMeshLODs().GetBuffer());
        bind("_shadowRasterSubmeshes", geometry.GetSubmeshes().GetBuffer());
        bind("_shadowRasterMeshlets", geometry.GetMeshlets().GetBuffer());
        bind("_shadowRasterPositions", geometry.GetPositions().GetBuffer());
        bind("_shadowRasterVertexIndices", geometry.GetMeshletVertexIndices().GetBuffer());
        bind("_shadowRasterTriangles", geometry.GetMeshletTriangles().GetBuffer());
        if (alphaTest)
        {
            bind("_shadowRasterVertexAttributes", geometry.GetVertexAttributes().GetBuffer());
            bind("_shadowRasterMaterialTable", scene.GetModelMaterialTables().GetEntries().GetBuffer());
        }
        bind("_shadowRasterSVSMData", svsmData);
        bind("_shadowRasterPageTable", pageTable);
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

                struct Constants { u32 queueIndex; u32 frameIndex; u32 dynamicCaster; u32 opacityDither; };
                auto draw = [&](Renderer::GraphicsPipelineID pipeline, u32 queueIndex, bool alphaTest,
                                Renderer::DescriptorSetResource& set) {
                    Constants* constants = graphResources.FrameNew<Constants>();
                    *constants = {queueIndex, frameIndex, queueIndex >= MODEL_SHADOW_RASTER_CLASS_COUNT ? 1u : 0u,
                                  opacityDither ? 1u : 0u};
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
                draw(_solidOneSidedPipeline, ModelShadowQueueIndex(ModelShadowRasterClass::SolidOneSided, false), false, data.staticSolidSet);
                draw(_solidTwoSidedPipeline, ModelShadowQueueIndex(ModelShadowRasterClass::SolidTwoSided, false), false, data.staticSolidSet);
                draw(_alphaOneSidedPipeline, ModelShadowQueueIndex(ModelShadowRasterClass::AlphaTestOneSided, false), true, data.staticAlphaSet);
                draw(_alphaTwoSidedPipeline, ModelShadowQueueIndex(ModelShadowRasterClass::AlphaTestTwoSided, false), true, data.staticAlphaSet);
                if (dynamicSplit)
                {
                    draw(_solidOneSidedPipeline, ModelShadowQueueIndex(ModelShadowRasterClass::SolidOneSided, true), false, data.dynamicSolidSet);
                    draw(_solidTwoSidedPipeline, ModelShadowQueueIndex(ModelShadowRasterClass::SolidTwoSided, true), false, data.dynamicSolidSet);
                    draw(_alphaOneSidedPipeline, ModelShadowQueueIndex(ModelShadowRasterClass::AlphaTestOneSided, true), true, data.dynamicAlphaSet);
                    draw(_alphaTwoSidedPipeline, ModelShadowQueueIndex(ModelShadowRasterClass::AlphaTestTwoSided, true), true, data.dynamicAlphaSet);
                }
                commandList.EndRenderPass(pass);
            });
    }
} // namespace ShadowRendering
