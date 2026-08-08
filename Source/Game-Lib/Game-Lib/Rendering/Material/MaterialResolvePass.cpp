#include "MaterialResolvePass.h"

#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Asset/RenderAssetResources.h"
#include "Game-Lib/Rendering/Material/MaterialStorage.h"
#include "Game-Lib/Rendering/Material/MaterialProgramLibrary.h"
#include "Game-Lib/Rendering/Model/Asset/ModelGeometryStorage.h"
#include "Game-Lib/Rendering/Model/ModelRendererMode.h"
#include "Game-Lib/Rendering/Model/View/ModelViewWorkResources.h"
#include "Game-Lib/Rendering/RenderResources.h"
#include "Game-Lib/Rendering/Scene/RenderScene.h"
#include "Game-Lib/Rendering/Scene/RenderView.h"

#include <Base/CVarSystem/CVarSystem.h>
#include <Renderer/RenderGraph.h>
#include <Renderer/Renderer.h>

AutoCVar_Int CVAR_MaterialDebugMode(
    CVarCategory::Client | CVarCategory::Rendering, "materialDebugMode",
    "Material debug: 0 shaded, 1 material, 2 program, 3 execution group, 4 lighting model, 5 texture", 0);

namespace MaterialRendering
{
    MaterialResolvePass::MaterialResolvePass(Renderer::Renderer* renderer, GameRenderer* gameRenderer)
        : _renderer(renderer), _classificationSet(Renderer::DescriptorSetSlot::PER_PASS),
          _finalizeSet(Renderer::DescriptorSetSlot::PER_PASS),
          _resolveSet(Renderer::DescriptorSetSlot::PER_PASS)
    {
        Renderer::ComputePipelineDesc desc;
        Renderer::ComputeShaderDesc shader;
        shader.shaderEntry = gameRenderer->GetShaderEntry("Material/MaterialClassify.cs"_h,
                                                           "Material/MaterialClassify.cs");
        desc.debugName = "Material Classification";
        desc.computeShader = renderer->LoadShader(shader);
        _classificationPipeline = renderer->CreatePipeline(desc);

        shader.shaderEntry = gameRenderer->GetShaderEntry("Material/MaterialClassifyFinalize.cs"_h,
                                                           "Material/MaterialClassifyFinalize.cs");
        desc.debugName = "Material Classification Finalize";
        desc.computeShader = renderer->LoadShader(shader);
        _finalizePipeline = renderer->CreatePipeline(desc);

        for (u32 group = 0; group < FileFormat::Material::ABI::EXECUTION_GROUP_COUNT; ++group)
        {
            const FileFormat::Material::MaterialExecutionGroup* cookedGroup =
                gameRenderer->GetRenderAssetResources()->GetMaterialProgramLibrary().GetExecutionGroup(
                    static_cast<u16>(group));
            if (cookedGroup == nullptr)
                continue;
            shader.shaderEntry = gameRenderer->GetShaderEntry(
                cookedGroup->resolveShaderPermutationHash, "Generated/MaterialResolve.cs");
            desc.debugName = "Material Resolve Group " + std::to_string(group);
            desc.computeShader = renderer->LoadShader(shader);
            _resolvePipelines[group] = renderer->CreatePipeline(desc);
            _activeResolveGroups[group] = true;
        }

        _classificationSet.RegisterPipeline(renderer, _classificationPipeline);
        _classificationSet.Init(renderer);
        _finalizeSet.RegisterPipeline(renderer, _finalizePipeline);
        _finalizeSet.Init(renderer);

        for (u32 group = 0; group < FileFormat::Material::ABI::EXECUTION_GROUP_COUNT; ++group)
        {
            if (_activeResolveGroups[group])
                _resolveSet.RegisterPipeline(renderer, _resolvePipelines[group]);
        }
        _resolveSet.Init(renderer);
    }

    void MaterialResolvePass::BindModelResources(Renderer::DescriptorSet& set, ModelBindings& bindings,
                                                 const ModelView::ModelViewWorkResources& work,
                                                 const ModelLoading::ModelGeometryStorage& geometry,
                                                 const RenderScenes::RenderScene& scene, bool& changed)
    {
        auto bind = [&set, &changed](StringUtils::StringHash name, Renderer::BufferID buffer,
                                     Renderer::BufferID& current) {
            if (buffer == current)
                return;
            set.Bind(name, buffer, true);
            current = buffer;
            changed = true;
        };
        bind("_resolvedModelVisibilityRecords0"_h, work.GetVisibilityRecords(0), bindings.visibilityRecords[0]);
        bind("_resolvedModelVisibilityStats0"_h, work.GetStatsBuffer(0), bindings.stats[0]);
        bind("_resolvedModelVisibilityRecords1"_h, work.GetVisibilityRecords(1), bindings.visibilityRecords[1]);
        bind("_resolvedModelVisibilityStats1"_h, work.GetStatsBuffer(1), bindings.stats[1]);
        bind("_resolvedModelInstances"_h, scene.GetModelInstances().GetRecords().GetBuffer(), bindings.instances);
        bind("_resolvedModels"_h, geometry.GetRecords().GetBuffer(), bindings.models);
        bind("_resolvedModelMeshes"_h, geometry.GetMeshes().GetBuffer(), bindings.meshes);
        bind("_resolvedModelLODs"_h, geometry.GetMeshLODs().GetBuffer(), bindings.lods);
        bind("_resolvedModelSubmeshes"_h, geometry.GetSubmeshes().GetBuffer(), bindings.submeshes);
        bind("_resolvedModelMeshlets"_h, geometry.GetMeshlets().GetBuffer(), bindings.meshlets);
        bind("_resolvedModelPositions"_h, geometry.GetPositions().GetBuffer(), bindings.positions);
        bind("_resolvedModelVertexAttributes"_h, geometry.GetVertexAttributes().GetBuffer(), bindings.attributes);
        bind("_resolvedModelVertexIndices"_h, geometry.GetMeshletVertexIndices().GetBuffer(), bindings.indices);
        bind("_resolvedModelTriangles"_h, geometry.GetMeshletTriangles().GetBuffer(), bindings.triangles);
        bind("_resolvedModelMaterialTable"_h, scene.GetModelMaterialTables().GetEntries().GetBuffer(),
             bindings.materialTable);
    }

    bool MaterialResolvePass::Upload(const ModelView::ModelViewWorkResources& work,
                                     MaterialResolveResources& resources,
                                     const ModelLoading::ModelGeometryStorage& geometry,
                                     const RenderScenes::RenderScene& scene)
    {
        const uvec2 renderSize = static_cast<uvec2>(_renderer->GetRenderSize());
        const u32 tilesX = (renderSize.x + MATERIAL_TILE_SIZE - 1u) / MATERIAL_TILE_SIZE;
        const u32 tilesY = (renderSize.y + MATERIAL_TILE_SIZE - 1u) / MATERIAL_TILE_SIZE;
        resources.EnsureTileCapacity(tilesX * tilesY);

        bool changed = false;
        BindModelResources(_classificationSet, _classificationBindings.model, work, geometry, scene,
                           changed);
        BindModelResources(_resolveSet, _resolveBindings.model, work, geometry, scene, changed);

        auto bind = [&changed](Renderer::DescriptorSet& set, StringUtils::StringHash name, Renderer::BufferID buffer,
                               Renderer::BufferID& current) {
            if (buffer == current)
                return;
            set.Bind(name, buffer);
            current = buffer;
            changed = true;
        };
        static constexpr StringUtils::StringHash TILE_QUEUE_NAMES[ModelView::MODEL_VIEW_FRAME_COUNT] = {
            "_materialTileQueue0"_h, "_materialTileQueue1"_h};
        static constexpr StringUtils::StringHash COUNTER_NAMES[ModelView::MODEL_VIEW_FRAME_COUNT] = {
            "_materialCounters0"_h, "_materialCounters1"_h};
        static constexpr StringUtils::StringHash ARGUMENT_NAMES[ModelView::MODEL_VIEW_FRAME_COUNT] = {
            "_materialArguments0"_h, "_materialArguments1"_h};
        for (u32 frame = 0; frame < ModelView::MODEL_VIEW_FRAME_COUNT; ++frame)
        {
            bind(_classificationSet, TILE_QUEUE_NAMES[frame], resources.GetTileQueue(frame),
                 _classificationBindings.tileQueues[frame]);
            bind(_classificationSet, COUNTER_NAMES[frame], resources.GetCounters(frame),
                 _classificationBindings.counters[frame]);
            bind(_finalizeSet, COUNTER_NAMES[frame], resources.GetCounters(frame),
                 _finalizeBindings.counters[frame]);
            bind(_finalizeSet, ARGUMENT_NAMES[frame], resources.GetArguments(frame),
                 _finalizeBindings.arguments[frame]);
            bind(_resolveSet, TILE_QUEUE_NAMES[frame], resources.GetTileQueue(frame),
                 _resolveBindings.tileQueues[frame]);
        }

        if (changed)
            _descriptorWarmupFrames = _renderer->GetFrameIndexCount();
        else if (_descriptorWarmupFrames > 0)
            --_descriptorWarmupFrames;
        return _descriptorWarmupFrames == 0;
    }

    void MaterialResolvePass::RegisterModelUsage(Renderer::RenderGraphBuilder& builder,
                                                 const ModelView::ModelViewWorkResources& work,
                                                 const ModelLoading::ModelGeometryStorage& geometry,
                                                 const MaterialLoading::MaterialStorage& materials,
                                                 const RenderScenes::RenderScene& scene) const
    {
        using Usage = Renderer::BufferPassUsage;
        for (u32 frame = 0; frame < ModelView::MODEL_VIEW_FRAME_COUNT; ++frame)
        {
            builder.Read(work.GetVisibilityRecords(frame), Usage::COMPUTE);
            builder.Read(work.GetStatsBuffer(frame), Usage::COMPUTE);
        }
        builder.Read(scene.GetModelInstances().GetRecords().GetBuffer(), Usage::COMPUTE);
        builder.Read(geometry.GetRecords().GetBuffer(), Usage::COMPUTE);
        builder.Read(geometry.GetMeshes().GetBuffer(), Usage::COMPUTE);
        builder.Read(geometry.GetMeshLODs().GetBuffer(), Usage::COMPUTE);
        builder.Read(geometry.GetSubmeshes().GetBuffer(), Usage::COMPUTE);
        builder.Read(geometry.GetMeshlets().GetBuffer(), Usage::COMPUTE);
        builder.Read(geometry.GetPositions().GetBuffer(), Usage::COMPUTE);
        builder.Read(geometry.GetVertexAttributes().GetBuffer(), Usage::COMPUTE);
        builder.Read(geometry.GetMeshletVertexIndices().GetBuffer(), Usage::COMPUTE);
        builder.Read(geometry.GetMeshletTriangles().GetBuffer(), Usage::COMPUTE);
        builder.Read(scene.GetModelMaterialTables().GetEntries().GetBuffer(), Usage::COMPUTE);
        builder.Read(materials.GetMaterialInstances().GetBuffer(), Usage::COMPUTE);
        builder.Read(materials.GetTextureIndices().GetBuffer(), Usage::COMPUTE);
        builder.Read(materials.GetSamplerIDs().GetBuffer(), Usage::COMPUTE);
    }

    void MaterialResolvePass::AddClassificationPass(
        Renderer::RenderGraph* renderGraph, RenderResources& renderResources, const RenderScenes::RenderView&,
        const ModelView::ModelViewWorkResources& work, MaterialResolveResources& resources,
        const ModelLoading::ModelGeometryStorage& geometry, const MaterialLoading::MaterialStorage& materials,
        const RenderScenes::RenderScene& scene, u8 frameIndex)
    {
        if (!ModelRendering::UseMeshletModelRenderer())
            return;
        struct Data
        {
            Renderer::ImageResource visibility;
            Renderer::ImageMutableResource materialIDs;
            Renderer::BufferMutableResource queue;
            Renderer::BufferMutableResource counters;
            Renderer::BufferMutableResource arguments;
            Renderer::BufferMutableResource readback;
            Renderer::DescriptorSetResource classificationSet;
            Renderer::DescriptorSetResource materialSet;
            Renderer::DescriptorSetResource finalizeSet;
        };
        renderGraph->AddPass<Data>("Material Classification",
            [this, &renderResources, &work, &resources, &geometry, &materials, &scene, frameIndex](
                Data& data, Renderer::RenderGraphBuilder& builder) {
                using Usage = Renderer::BufferPassUsage;
                data.visibility = builder.Read(renderResources.visibilityBuffer, Renderer::PipelineType::COMPUTE);
                data.materialIDs = builder.Write(resources.GetMaterialIDs(), Renderer::PipelineType::COMPUTE,
                                                 Renderer::LoadMode::CLEAR);
                RegisterModelUsage(builder, work, geometry, materials, scene);
                builder.Read(materials.GetMaterials().GetBuffer(), Usage::COMPUTE);
                for (u32 frame = 0; frame < ModelView::MODEL_VIEW_FRAME_COUNT; ++frame)
                {
                    if (frame == frameIndex)
                    {
                        data.queue = builder.Write(resources.GetTileQueue(frame), Usage::COMPUTE);
                        data.counters = builder.Write(resources.GetCounters(frame), Usage::COMPUTE | Usage::TRANSFER);
                        data.arguments = builder.Write(resources.GetArguments(frame), Usage::COMPUTE | Usage::TRANSFER);
                    }
                    else
                    {
                        builder.Write(resources.GetTileQueue(frame), Usage::COMPUTE);
                        builder.Write(resources.GetCounters(frame), Usage::COMPUTE);
                        builder.Write(resources.GetArguments(frame), Usage::COMPUTE);
                    }
                }
                data.readback = builder.Write(resources.GetReadback(frameIndex), Usage::TRANSFER);
                data.classificationSet = builder.Use(_classificationSet);
                data.materialSet = builder.Use(renderResources.materialDescriptorSet);
                data.finalizeSet = builder.Use(_finalizeSet);
                return true;
            },
            [this, &resources, frameIndex](Data& data, Renderer::RenderGraphResources& graphResources,
                                           Renderer::CommandList& commandList) {
                GPU_SCOPED_PROFILER_ZONE(commandList, MaterialClassification);
                commandList.FillBuffer(data.counters, 0, sizeof(MaterialClassificationStats), 0);
                commandList.FillBuffer(data.arguments, 0,
                    sizeof(u32) * FileFormat::Material::ABI::EXECUTION_GROUP_COUNT *
                        MATERIAL_DISPATCH_ARGUMENT_COUNT, 0);
                commandList.BufferBarrier(data.counters, Renderer::BufferPassUsage::COMPUTE);
                commandList.BufferBarrier(data.arguments, Renderer::BufferPassUsage::COMPUTE);

                const uvec2 renderSize = static_cast<uvec2>(_renderer->GetRenderSize());
                const u32 tilesX = (renderSize.x + MATERIAL_TILE_SIZE - 1u) / MATERIAL_TILE_SIZE;
                const u32 tilesY = (renderSize.y + MATERIAL_TILE_SIZE - 1u) / MATERIAL_TILE_SIZE;
                struct Constants
                {
                    vec4 renderInfo;
                    u32 resourceIndex;
                    u32 tileCapacity;
                    u32 tilesX;
                };
                Constants* constants = graphResources.FrameNew<Constants>();
                constants->renderInfo = vec4(renderSize, 1.0f / vec2(renderSize));
                constants->resourceIndex = frameIndex;
                constants->tileCapacity = resources.GetTileCapacity();
                constants->tilesX = tilesX;

                commandList.BeginPipeline(_classificationPipeline);
                data.classificationSet.Bind("_visibilityBuffer"_h, data.visibility);
                data.classificationSet.Bind("_materialIDs"_h, data.materialIDs);
                commandList.PushConstant(constants, 0, sizeof(Constants));
                commandList.BindDescriptorSet(data.materialSet, frameIndex);
                commandList.BindDescriptorSet(data.classificationSet, frameIndex);
                commandList.Dispatch(tilesX, tilesY, 1);
                commandList.EndPipeline(_classificationPipeline);

                commandList.BufferBarrier(data.counters, Renderer::BufferPassUsage::COMPUTE);
                struct FinalizeConstants { u32 resourceIndex; u32 tileCapacity; };
                FinalizeConstants* finalize = graphResources.FrameNew<FinalizeConstants>();
                finalize->resourceIndex = frameIndex;
                finalize->tileCapacity = resources.GetTileCapacity();
                commandList.BeginPipeline(_finalizePipeline);
                commandList.PushConstant(finalize, 0, sizeof(FinalizeConstants));
                commandList.BindDescriptorSet(data.finalizeSet, frameIndex);
                commandList.Dispatch(1, 1, 1);
                commandList.EndPipeline(_finalizePipeline);
                commandList.BufferBarrier(data.arguments, Renderer::BufferPassUsage::COMPUTE);
                commandList.BufferBarrier(data.counters, Renderer::BufferPassUsage::TRANSFER);
                commandList.CopyBuffer(data.readback, 0, data.counters, 0,
                                       sizeof(MaterialClassificationStats));
            });
        resources.MarkSubmitted(frameIndex);
    }

    void MaterialResolvePass::AddResolvePass(
        Renderer::RenderGraph* renderGraph, RenderResources& renderResources, const RenderScenes::RenderView&,
        const ModelView::ModelViewWorkResources& work, MaterialResolveResources& resources,
        const ModelLoading::ModelGeometryStorage& geometry, const MaterialLoading::MaterialStorage& materials,
        const RenderScenes::RenderScene& scene, u8 frameIndex)
    {
        if (!ModelRendering::UseMeshletModelRenderer())
            return;
        struct Data
        {
            Renderer::ImageResource visibility;
            Renderer::ImageResource materialIDs;
            Renderer::ImageMutableResource color;
            Renderer::BufferResource arguments;
            Renderer::DescriptorSetResource globalSet;
            Renderer::DescriptorSetResource materialSet;
            Renderer::DescriptorSetResource resolveSet;
        };
        renderGraph->AddPass<Data>("Material Resolve",
            [this, &renderResources, &work, &resources, &geometry, &materials, &scene, frameIndex](
                Data& data, Renderer::RenderGraphBuilder& builder) {
                using Usage = Renderer::BufferPassUsage;
                data.visibility = builder.Read(renderResources.visibilityBuffer, Renderer::PipelineType::COMPUTE);
                data.materialIDs = builder.Read(resources.GetMaterialIDs(), Renderer::PipelineType::COMPUTE);
                data.color = builder.Write(renderResources.sceneColor, Renderer::PipelineType::COMPUTE,
                                           Renderer::LoadMode::LOAD);
                builder.Read(renderResources.cameras.GetBuffer(), Usage::COMPUTE);
                RegisterModelUsage(builder, work, geometry, materials, scene);
                builder.Read(materials.GetMaterials().GetBuffer(), Usage::COMPUTE);
                builder.Read(materials.GetParameterStorage().GetBuffer().GetBuffer(), Usage::COMPUTE);
                for (u32 frame = 0; frame < ModelView::MODEL_VIEW_FRAME_COUNT; ++frame)
                    builder.Read(resources.GetTileQueue(frame), Usage::COMPUTE);
                data.arguments = builder.Read(resources.GetArguments(frameIndex), Usage::COMPUTE);
                data.globalSet = builder.Use(renderResources.globalDescriptorSet);
                data.materialSet = builder.Use(renderResources.materialDescriptorSet);
                data.resolveSet = builder.Use(_resolveSet);
                return true;
            },
            [this, &resources, frameIndex](Data& data, Renderer::RenderGraphResources& graphResources,
                                           Renderer::CommandList& commandList) {
                GPU_SCOPED_PROFILER_ZONE(commandList, MaterialResolve);
                const uvec2 renderSize = static_cast<uvec2>(_renderer->GetRenderSize());
                const u32 tilesX = (renderSize.x + MATERIAL_TILE_SIZE - 1u) / MATERIAL_TILE_SIZE;
                struct Constants
                {
                    vec4 renderInfo;
                    u32 resourceIndex;
                    u32 tileCapacity;
                    u32 tilesX;
                    u32 debugMode;
                };
                data.resolveSet.Bind("_visibilityBuffer"_h, data.visibility);
                data.resolveSet.Bind("_materialIDs"_h, data.materialIDs);
                data.resolveSet.Bind("_materialColor"_h, data.color);
                for (u32 group = 0; group < FileFormat::Material::ABI::EXECUTION_GROUP_COUNT; ++group)
                {
                    if (!_activeResolveGroups[group])
                        continue;
                    commandList.BeginPipeline(_resolvePipelines[group]);
                    commandList.BindDescriptorSet(data.globalSet, frameIndex);
                    commandList.BindDescriptorSet(data.materialSet, frameIndex);
                    commandList.BindDescriptorSet(data.resolveSet, frameIndex);
                    Constants* constants = graphResources.FrameNew<Constants>();
                    constants->renderInfo = vec4(renderSize, 1.0f / vec2(renderSize));
                    constants->resourceIndex = frameIndex;
                    constants->tileCapacity = resources.GetTileCapacity();
                    constants->tilesX = tilesX;
                    constants->debugMode = static_cast<u32>(glm::clamp(CVAR_MaterialDebugMode.Get(), 0, 5));
                    commandList.PushConstant(constants, 0, sizeof(Constants));
                    commandList.DispatchIndirect(
                        data.arguments, group * sizeof(u32) * MATERIAL_DISPATCH_ARGUMENT_COUNT);
                    commandList.EndPipeline(_resolvePipelines[group]);
                }
            });
    }
} // namespace MaterialRendering
