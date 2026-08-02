#include "ModelVisibilityPass.h"

#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Model/Asset/ModelGeometryStorage.h"
#include "Game-Lib/Rendering/Model/View/ModelViewWorkResources.h"
#include "Game-Lib/Rendering/RenderResources.h"
#include "Game-Lib/Rendering/Scene/RenderScene.h"
#include "Game-Lib/Rendering/Scene/RenderView.h"

#include <Base/CVarSystem/CVarSystem.h>
#include <Renderer/RenderGraph.h>
#include <Renderer/Renderer.h>

AutoCVar_ShowFlag CVAR_ModelMeshlets(CVarCategory::Client | CVarCategory::Rendering, "modelMeshlets",
                                     "Draw models through the meshlet visibility renderer", ShowFlag::DISABLED);

namespace ModelPipeline
{
    ModelVisibilityPass::ModelVisibilityPass(Renderer::Renderer* renderer, GameRenderer* gameRenderer)
        : _renderer(renderer), _descriptorSet(Renderer::DescriptorSetSlot::PER_PASS)
    {
        const Renderer::MeshShaderProperties& properties = renderer->GetMeshShaderProperties();
        NC_ASSERT(properties.maxOutputVertices >= FileFormat::Model::MAX_MESHLET_VERTICES,
                  "Model meshlet vertex count exceeds the selected GPU limit");
        NC_ASSERT(properties.maxOutputPrimitives >= FileFormat::Model::MAX_MESHLET_TRIANGLES,
                  "Model meshlet triangle count exceeds the selected GPU limit");

        Renderer::MeshShaderDesc meshShader;
        meshShader.shaderEntry = gameRenderer->GetShaderEntry("Model/Visibility.ms"_h, "Model/Visibility.ms");
        Renderer::PixelShaderDesc pixelShader;
        pixelShader.shaderEntry = gameRenderer->GetShaderEntry("Model/Visibility.ps"_h, "Model/Visibility.ps");

        Renderer::GraphicsPipelineDesc desc;
        desc.debugName = "Model Visibility One Sided";
        desc.shaderStages = Renderer::MeshPipelineStages{ .meshShader = renderer->LoadShader(meshShader) };
        desc.states.pixelShader = renderer->LoadShader(pixelShader);
        desc.states.renderTargetFormats[0] = Renderer::ImageFormat::R32_UINT;
        desc.states.depthStencilFormat = Renderer::DepthImageFormat::D32_FLOAT;
        desc.states.depthStencilState.depthEnable = true;
        desc.states.depthStencilState.depthWriteEnable = true;
        desc.states.depthStencilState.depthFunc = Renderer::ComparisonFunc::GREATER_EQUAL;
        desc.states.rasterizerState.cullMode = Renderer::CullMode::BACK;
        desc.states.rasterizerState.frontFaceMode = Renderer::FrontFaceState::COUNTERCLOCKWISE;
        _oneSidedPipeline = renderer->CreatePipeline(desc);

        desc.debugName = "Model Visibility Two Sided";
        desc.states.rasterizerState.cullMode = Renderer::CullMode::NONE;
        _twoSidedPipeline = renderer->CreatePipeline(desc);

        _descriptorSet.RegisterPipeline(renderer, _oneSidedPipeline);
        _descriptorSet.RegisterPipeline(renderer, _twoSidedPipeline);
        _descriptorSet.Init(renderer);
    }

    void ModelVisibilityPass::BindIfChanged(StringUtils::StringHash name, Renderer::BufferID buffer,
                                             Renderer::BufferID& current)
    {
        if (buffer == current)
            return;
        _descriptorSet.Bind(name, buffer);
        current = buffer;
    }

    void ModelVisibilityPass::Upload(const ModelView::ModelViewWorkResources& work,
                                     const ModelLoading::ModelGeometryStorage& geometry,
                                     const RenderScenes::RenderScene& scene)
    {
        if (_queueGeneration != work.GetQueueGeneration())
        {
            _queueGeneration = work.GetQueueGeneration();
            for (FrameBindings& frame : _bindings.frames)
            {
                frame.oneSidedQueue = Renderer::BufferID::Invalid();
                frame.twoSidedQueue = Renderer::BufferID::Invalid();
                frame.visibilityRecords = Renderer::BufferID::Invalid();
                frame.workStats = Renderer::BufferID::Invalid();
            }
        }

        BindIfChanged("_modelRasterQueueOneSided0"_h, work.GetQueue(ModelView::MODEL_RASTER_ONE_SIDED, 0),
                      _bindings.frames[0].oneSidedQueue);
        BindIfChanged("_modelRasterQueueTwoSided0"_h, work.GetQueue(ModelView::MODEL_RASTER_TWO_SIDED, 0),
                      _bindings.frames[0].twoSidedQueue);
        BindIfChanged("_modelVisibilityRecords0"_h, work.GetVisibilityRecords(0),
                      _bindings.frames[0].visibilityRecords);
        BindIfChanged("_modelVisibilityStats0"_h, work.GetStatsBuffer(0), _bindings.frames[0].workStats);
        BindIfChanged("_modelRasterQueueOneSided1"_h, work.GetQueue(ModelView::MODEL_RASTER_ONE_SIDED, 1),
                      _bindings.frames[1].oneSidedQueue);
        BindIfChanged("_modelRasterQueueTwoSided1"_h, work.GetQueue(ModelView::MODEL_RASTER_TWO_SIDED, 1),
                      _bindings.frames[1].twoSidedQueue);
        BindIfChanged("_modelVisibilityRecords1"_h, work.GetVisibilityRecords(1),
                      _bindings.frames[1].visibilityRecords);
        BindIfChanged("_modelVisibilityStats1"_h, work.GetStatsBuffer(1), _bindings.frames[1].workStats);

        BindIfChanged("_modelVisibilityInstances"_h, scene.GetModelInstances().GetRecords().GetBuffer(),
                      _bindings.modelInstances);
        BindIfChanged("_modelVisibilityModels"_h, geometry.GetRecords().GetBuffer(), _bindings.models);
        BindIfChanged("_modelVisibilityMeshes"_h, geometry.GetMeshes().GetBuffer(), _bindings.meshes);
        BindIfChanged("_modelVisibilityLODs"_h, geometry.GetMeshLODs().GetBuffer(), _bindings.lods);
        BindIfChanged("_modelVisibilitySubmeshes"_h, geometry.GetSubmeshes().GetBuffer(), _bindings.submeshes);
        BindIfChanged("_modelVisibilityMeshlets"_h, geometry.GetMeshlets().GetBuffer(), _bindings.meshlets);
        BindIfChanged("_modelVisibilityPositions"_h, geometry.GetPositions().GetBuffer(), _bindings.positions);
        BindIfChanged("_modelVisibilityVertexAttributes"_h, geometry.GetVertexAttributes().GetBuffer(),
                      _bindings.vertexAttributes);
        BindIfChanged("_modelVisibilityVertexIndices"_h, geometry.GetMeshletVertexIndices().GetBuffer(),
                      _bindings.vertexIndices);
        BindIfChanged("_modelVisibilityTriangles"_h, geometry.GetMeshletTriangles().GetBuffer(),
                      _bindings.triangles);
    }

    void ModelVisibilityPass::AddPass(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                                      const RenderScenes::RenderView& view,
                                      const ModelView::ModelViewWorkResources& work,
                                      const ModelLoading::ModelGeometryStorage& geometry,
                                      const RenderScenes::RenderScene& scene, u8 frameIndex)
    {
        if (CVAR_ModelMeshlets.Get() != ShowFlag::ENABLED)
            return;

        struct Data
        {
            Renderer::ImageMutableResource visibility;
            Renderer::DepthImageMutableResource depth;
            Renderer::BufferResource arguments;
            Renderer::DescriptorSetResource globalSet;
            Renderer::DescriptorSetResource visibilitySet;
        };

        renderGraph->AddPass<Data>("Model Visibility",
            [this, &resources, &view, &work, &geometry, &scene, frameIndex](Data& data, Renderer::RenderGraphBuilder& builder) {
                using Usage = Renderer::BufferPassUsage;
                data.visibility = builder.Write(resources.visibilityBuffer, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);
                data.depth = builder.Write(view.GetDepthTarget(), Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);
                builder.Read(resources.cameras.GetBuffer(), Usage::GRAPHICS);
                for (u32 frame = 0; frame < ModelView::MODEL_VIEW_FRAME_COUNT; ++frame)
                {
                    builder.Read(work.GetQueue(ModelView::MODEL_RASTER_ONE_SIDED, frame), Usage::GRAPHICS);
                    builder.Read(work.GetQueue(ModelView::MODEL_RASTER_TWO_SIDED, frame), Usage::GRAPHICS);
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
                data.globalSet = builder.Use(resources.globalDescriptorSet);
                data.visibilitySet = builder.Use(_descriptorSet);
                return true;
            },
            [this, &view, frameIndex](Data& data, Renderer::RenderGraphResources& graphResources,
                                     Renderer::CommandList& commandList) {
                GPU_SCOPED_PROFILER_ZONE(commandList, ModelVisibility);
                Renderer::RenderPassDesc pass;
                graphResources.InitializeRenderPassDesc(pass);
                pass.renderTargets[0] = data.visibility;
                pass.depthStencil = data.depth;
                commandList.BeginRenderPass(pass);

                struct Constants { u32 queueIndex; u32 viewIndex; u32 resourceIndex; };
                auto draw = [&](Renderer::GraphicsPipelineID pipeline, u32 queueIndex) {
                    Constants* constants = graphResources.FrameNew<Constants>();
                    constants->queueIndex = queueIndex;
                    constants->viewIndex = view.GetCameraIndex();
                    constants->resourceIndex = frameIndex;
                    commandList.BeginPipeline(pipeline);
                    commandList.PushConstant(constants, 0, sizeof(Constants));
                    commandList.BindDescriptorSet(data.globalSet, frameIndex);
                    commandList.BindDescriptorSet(data.visibilitySet, frameIndex);
                    commandList.DrawMeshTasksIndirect(
                        data.arguments, queueIndex * sizeof(u32) * ModelView::MODEL_DISPATCH_ARGUMENT_COUNT);
                    commandList.EndPipeline(pipeline);
                };
                draw(_oneSidedPipeline, ModelView::MODEL_RASTER_ONE_SIDED);
                draw(_twoSidedPipeline, ModelView::MODEL_RASTER_TWO_SIDED);
                commandList.EndRenderPass(pass);
            });
    }
} // namespace ModelPipeline
