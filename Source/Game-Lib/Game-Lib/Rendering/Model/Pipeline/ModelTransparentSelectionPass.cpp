#include "ModelTransparentSelectionPass.h"

#include "Game-Lib/Rendering/Asset/RenderAssetResources.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Material/MaterialRenderer.h"
#include "Game-Lib/Rendering/Material/MaterialProgramLibrary.h"
#include "Game-Lib/Rendering/Material/MaterialStorage.h"
#include "Game-Lib/Rendering/Model/Asset/ModelGeometryStorage.h"
#include "Game-Lib/Rendering/RenderResources.h"
#include "Game-Lib/Rendering/Scene/RenderScene.h"
#include "Game-Lib/Rendering/Scene/RenderView.h"

#include <Renderer/Descriptors/ComputeShaderDesc.h>
#include <Renderer/RenderGraph.h>
#include <Renderer/Renderer.h>

namespace ModelPipeline
{
    ModelTransparentSelectionPass::ModelTransparentSelectionPass(Renderer::Renderer* renderer,
                                                                 GameRenderer* gameRenderer)
        : _renderer(renderer), _gameRenderer(gameRenderer), _depthSet(Renderer::DescriptorSetSlot::PER_PASS),
          _outlineSet(Renderer::DescriptorSetSlot::PER_PASS)
    {
        Renderer::MeshShaderDesc meshShader;
        meshShader.shaderEntry = gameRenderer->GetShaderEntry("Model/Transparent.ms"_h, "Model/Transparent.ms");
        const Renderer::MeshShaderID loadedMeshShader = renderer->LoadShader(meshShader);
        const MaterialLoading::MaterialProgramLibrary& library =
            gameRenderer->GetRenderAssetResources()->GetMaterialProgramLibrary();
        for (u32 bin = 0; bin < ModelView::MODEL_TRANSPARENT_BIN_COUNT; ++bin)
        {
            const u16 executionGroup = ModelView::TransparentExecutionGroup(bin);
            if (!library.GetExecutionGroup(executionGroup))
                continue;

            const std::string selectionPath = "Generated/MaterialGroupsSelection-MATERIAL_COOK_GROUP" +
                std::to_string(executionGroup) + ".ps";
            Renderer::PixelShaderDesc pixelShader;
            pixelShader.shaderEntry = gameRenderer->GetShaderEntry(
                StringUtils::fnv1a_32(selectionPath.c_str(), selectionPath.size()),
                "Generated/MaterialGroupsSelection.ps");
            Renderer::GraphicsPipelineDesc desc;
            desc.debugName = "Model Transparent Selection Group " + std::to_string(executionGroup) +
                ((bin & 1u) != 0u ? " Two Sided" : " One Sided");
            desc.shaderStages = Renderer::MeshPipelineStages{.meshShader = loadedMeshShader};
            desc.states.pixelShader = renderer->LoadShader(pixelShader);
            desc.states.depthStencilState.depthEnable = true;
            desc.states.depthStencilState.depthWriteEnable = true;
            desc.states.depthStencilState.depthFunc = Renderer::ComparisonFunc::GREATER_EQUAL;
            desc.states.rasterizerState.cullMode = (bin & 1u) != 0u ? Renderer::CullMode::NONE : Renderer::CullMode::BACK;
            desc.states.rasterizerState.frontFaceMode = Renderer::FrontFaceState::COUNTERCLOCKWISE;
            desc.states.depthStencilFormat = Renderer::DepthImageFormat::D32_FLOAT;
            _depthPipelines[bin] = renderer->CreatePipeline(desc);
            _depthSet.RegisterPipeline(renderer, _depthPipelines[bin]);
            _activeBins[bin] = true;
        }
        _depthSet.Init(renderer);

        Renderer::ComputeShaderDesc shader;
        shader.shaderEntry = gameRenderer->GetShaderEntry("Model/TransparentSelectionOutline.cs"_h,
                                                          "Model/TransparentSelectionOutline.cs");
        Renderer::ComputePipelineDesc desc;
        desc.computeShader = renderer->LoadShader(shader);
        desc.debugName = "Model Transparent Selection Outline";
        _outlinePipeline = renderer->CreatePipeline(desc);
        _outlineSet.RegisterPipeline(renderer, _outlinePipeline);
        _outlineSet.Init(renderer);
    }

    bool ModelTransparentSelectionPass::Bind(StringUtils::StringHash name, Renderer::BufferID buffer)
    {
        Renderer::BufferID& current = _bindings[name.computedHash];
        if (current == buffer)
            return false;
        _depthSet.Bind(name, buffer);
        current = buffer;
        return true;
    }

    bool ModelTransparentSelectionPass::Upload(const ModelView::ModelTransparentWorkResources& work,
                                               const ModelLoading::ModelGeometryStorage& geometry,
                                               const RenderScenes::RenderScene& scene)
    {
        if (_generation != work.GetGeneration())
        {
            _generation = work.GetGeneration();
            _bindings.clear();
        }
        bool changed = false;
        for (u32 frame = 0; frame < ModelView::MODEL_TRANSPARENT_FRAME_COUNT; ++frame)
        {
            const std::string suffix = std::to_string(frame);
            changed |= Bind(StringUtils::StringHash("_transparentRecords" + suffix),
                            work.GetVisibilityRecords(frame));
            changed |= Bind(StringUtils::StringHash("_transparentStats" + suffix), work.GetStatsBuffer(frame));
        }
        changed |= Bind("_transparentRasterInstances"_h, scene.GetModelInstances().GetRecords().GetBuffer());
        changed |= Bind("_transparentRasterModels"_h, geometry.GetRecords().GetBuffer());
        changed |= Bind("_transparentRasterMeshes"_h, geometry.GetMeshes().GetBuffer());
        changed |= Bind("_transparentRasterLODs"_h, geometry.GetMeshLODs().GetBuffer());
        changed |= Bind("_transparentRasterSubmeshes"_h, geometry.GetSubmeshes().GetBuffer());
        changed |= Bind("_transparentRasterMeshlets"_h, geometry.GetMeshlets().GetBuffer());
        changed |= Bind("_transparentRasterPositions"_h, geometry.GetPositions().GetBuffer());
        changed |= Bind("_transparentRasterVertexAttributes"_h, geometry.GetVertexAttributes().GetBuffer());
        changed |= Bind("_transparentRasterVertexIndices"_h, geometry.GetMeshletVertexIndices().GetBuffer());
        changed |= Bind("_transparentRasterTriangles"_h, geometry.GetMeshletTriangles().GetBuffer());
        changed |= Bind("_transparentRasterMaterialTable"_h, scene.GetModelMaterialTables().GetEntries().GetBuffer());
        if (changed)
            _descriptorWarmupFrames = _renderer->GetFrameIndexCount();
        else if (_descriptorWarmupFrames > 0)
            --_descriptorWarmupFrames;
        return _descriptorWarmupFrames == 0;
    }

    void ModelTransparentSelectionPass::AddDepthPass(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                                                     const RenderScenes::RenderView& view,
                                                     const ModelView::ModelTransparentWorkResources& work,
                                                     const ModelLoading::ModelGeometryStorage& geometry,
                                                     const MaterialLoading::MaterialStorage& materials,
                                                     const RenderScenes::RenderScene& scene,
                                                     Renderer::DepthImageID selectionDepth, u8 frameIndex)
    {
        struct Data
        {
            Renderer::DepthImageMutableResource selectionDepth;
            Renderer::DepthImageResource opaqueDepth;
            Renderer::BufferResource arguments;
            Renderer::DescriptorSetResource globalSet;
            Renderer::DescriptorSetResource materialSet;
            Renderer::DescriptorSetResource depthSet;
        };
        renderGraph->AddPass<Data>("Selection Depth: " + view.GetDebugName(),
            [this, &resources, &view, &work, &geometry, &materials, &scene, selectionDepth, frameIndex](Data& data,
                Renderer::RenderGraphBuilder& builder) {
                using Usage = Renderer::BufferPassUsage;
                data.selectionDepth = builder.Write(selectionDepth, Renderer::PipelineType::GRAPHICS,
                                                    Renderer::LoadMode::CLEAR);
                data.opaqueDepth = builder.Read(view.GetDepthTarget(), Renderer::PipelineType::GRAPHICS);
                data.arguments = builder.Read(work.GetArguments(frameIndex), Usage::GRAPHICS);
                builder.Read(resources.cameras.GetBuffer(), Usage::GRAPHICS);
                for (u32 frame = 0; frame < ModelView::MODEL_TRANSPARENT_FRAME_COUNT; ++frame)
                {
                    builder.Read(work.GetVisibilityRecords(frame), Usage::GRAPHICS);
                    builder.Read(work.GetStatsBuffer(frame), Usage::GRAPHICS);
                }
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
                data.depthSet = builder.Use(_depthSet);
                return true;
            },
            [this, &view, &work, frameIndex](Data& data, Renderer::RenderGraphResources& graphResources,
                Renderer::CommandList& commandList) {
                GPU_SCOPED_PROFILER_ZONE(commandList, ModelTransparentSelectionDepth);
                data.depthSet.Bind("_selectionOpaqueDepth"_h, data.opaqueDepth);
                Renderer::RenderPassDesc pass;
                graphResources.InitializeRenderPassDesc(pass);
                pass.depthStencil = data.selectionDepth;
                const uvec2 dimensions = view.GetDimensions();
                commandList.SetViewport(0, 0, static_cast<f32>(dimensions.x), static_cast<f32>(dimensions.y), 0.0f, 1.0f);
                commandList.SetScissorRect(0, dimensions.x, 0, dimensions.y);
                commandList.BeginRenderPass(pass);
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
                    *constants = {};
                    constants->viewIndex = view.GetCameraIndex();
                    constants->resourceIndex = frameIndex;
                    constants->binIndex = bin;
                    constants->binCapacity = work.GetBinCapacity();
                    commandList.BeginPipeline(_depthPipelines[bin]);
                    commandList.PushConstant(constants, 0, sizeof(*constants));
                    commandList.BindDescriptorSet(data.globalSet, frameIndex);
                    commandList.BindDescriptorSet(data.materialSet, frameIndex);
                    commandList.BindDescriptorSet(data.depthSet, frameIndex);
                    commandList.DrawMeshTasksIndirect(data.arguments,
                        bin * sizeof(u32) * ModelView::MODEL_TRANSPARENT_DISPATCH_ARGUMENT_COUNT);
                    commandList.EndPipeline(_depthPipelines[bin]);
                }
                commandList.EndRenderPass(pass);
            });
    }

    void ModelTransparentSelectionPass::AddOutlinePass(Renderer::RenderGraph* renderGraph,
                                                       const RenderScenes::RenderView& view,
                                                       Renderer::ImageID revealage,
                                                       Renderer::DepthImageID selectionDepth, u8 frameIndex)
    {
        struct Data
        {
            Renderer::DepthImageResource selectionDepth;
            Renderer::DepthImageResource opaqueDepth;
            Renderer::ImageResource revealage;
            Renderer::ImageMutableResource color;
            Renderer::DescriptorSetResource globalSet;
            Renderer::DescriptorSetResource outlineSet;
        };
        renderGraph->AddPass<Data>("Selection Outline: " + view.GetDebugName(),
            [this, &view, revealage, selectionDepth](Data& data, Renderer::RenderGraphBuilder& builder) {
                data.selectionDepth = builder.Read(selectionDepth, Renderer::PipelineType::COMPUTE);
                data.opaqueDepth = builder.Read(view.GetDepthTarget(), Renderer::PipelineType::COMPUTE);
                data.revealage = builder.Read(revealage, Renderer::PipelineType::COMPUTE);
                data.color = builder.Write(view.GetColorTarget(), Renderer::PipelineType::COMPUTE,
                                           Renderer::LoadMode::LOAD);
                builder.Read(_gameRenderer->GetRenderResources().cameras.GetBuffer(),
                             Renderer::BufferPassUsage::COMPUTE);
                data.globalSet = builder.Use(_gameRenderer->GetRenderResources().globalDescriptorSet);
                data.outlineSet = builder.Use(_outlineSet);
                return true;
            },
            [this, &view, frameIndex](Data& data, Renderer::RenderGraphResources& graphResources,
                Renderer::CommandList& commandList) {
                GPU_SCOPED_PROFILER_ZONE(commandList, ModelTransparentSelectionOutline);
                data.outlineSet.Bind("_selectionDepth"_h, data.selectionDepth);
                data.outlineSet.Bind("_selectionOpaqueDepth"_h, data.opaqueDepth);
                data.outlineSet.Bind("_selectionRevealage"_h, data.revealage);
                data.outlineSet.Bind("_selectionSceneColor"_h, data.color);
                struct Constants
                {
                    uvec2 dimensions;
                    u32 viewIndex;
                    u32 reserved;
                    vec4 fogColor;
                    vec4 fogSettings;
                };
                Constants* constants = graphResources.FrameNew<Constants>();
                *constants = {};
                constants->dimensions = view.GetDimensions();
                constants->viewIndex = view.GetCameraIndex();
                constants->fogColor = _gameRenderer->GetMaterialRenderer()->GetFogColor();
                constants->fogSettings = _gameRenderer->GetMaterialRenderer()->GetFogSettings();
                commandList.BeginPipeline(_outlinePipeline);
                commandList.PushConstant(constants, 0, sizeof(*constants));
                commandList.BindDescriptorSet(data.globalSet, frameIndex);
                commandList.BindDescriptorSet(data.outlineSet, frameIndex);
                commandList.Dispatch((constants->dimensions.x + 7u) / 8u, (constants->dimensions.y + 7u) / 8u, 1);
                commandList.EndPipeline(_outlinePipeline);
            });
    }
} // namespace ModelPipeline
