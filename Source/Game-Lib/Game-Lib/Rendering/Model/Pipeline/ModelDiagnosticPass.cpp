#include "ModelDiagnosticPass.h"

#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Model/Asset/ModelGeometryStorage.h"
#include "Game-Lib/Rendering/Model/View/ModelViewState.h"
#include "Game-Lib/Rendering/RenderResources.h"
#include "Game-Lib/Rendering/Scene/RenderScene.h"
#include "Game-Lib/Rendering/Scene/RenderView.h"

#include <Base/CVarSystem/CVarSystem.h>

#include <Renderer/RenderGraph.h>
#include <Renderer/Renderer.h>

AutoCVar_ShowFlag CVAR_ModelMeshlets(CVarCategory::Client | CVarCategory::Rendering, "modelMeshlets",
                                     "Draw models through the meshlet renderer", ShowFlag::DISABLED);
AutoCVar_Int CVAR_ModelMeshletDebugMode(CVarCategory::Client | CVarCategory::Rendering, "modelMeshletDebugMode",
                                       "Meshlet debug mode: 0 flat, 1 normals, 2 UVs, 3 bounds, 4 normal cones", 0);

namespace ModelPipeline
{
    ModelDiagnosticPass::ModelDiagnosticPass(Renderer::Renderer* renderer, GameRenderer* gameRenderer)
        : _renderer(renderer), _descriptorSet(Renderer::DescriptorSetSlot::PER_PASS)
    {
        const Renderer::MeshShaderProperties& properties = _renderer->GetMeshShaderProperties();
        NC_ASSERT(properties.maxOutputVertices >= FileFormat::Model::MAX_MESHLET_VERTICES,
                  "Model meshlet vertex count exceeds the selected GPU limit");
        NC_ASSERT(properties.maxOutputPrimitives >= FileFormat::Model::MAX_MESHLET_TRIANGLES,
                  "Model meshlet triangle count exceeds the selected GPU limit");
        NC_ASSERT(properties.maxWorkGroupInvocations >= FileFormat::Model::MAX_MESHLET_VERTICES,
                  "Model meshlet workgroup size exceeds the selected GPU limit");

        Renderer::MeshShaderDesc meshShaderDesc;
        meshShaderDesc.shaderEntry = gameRenderer->GetShaderEntry("Model/Diagnostic.ms"_h, "Model/Diagnostic.ms");

        Renderer::PixelShaderDesc pixelShaderDesc;
        pixelShaderDesc.shaderEntry = gameRenderer->GetShaderEntry("Model/Diagnostic.ps"_h, "Model/Diagnostic.ps");

        Renderer::GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.debugName = "Model Diagnostic One Sided";
        pipelineDesc.shaderStages = Renderer::MeshPipelineStages{ .meshShader = _renderer->LoadShader(meshShaderDesc) };
        pipelineDesc.states.pixelShader = _renderer->LoadShader(pixelShaderDesc);
        pipelineDesc.states.renderTargetFormats[0] = _renderer->GetSwapChainImageFormat();
        pipelineDesc.states.depthStencilFormat = Renderer::DepthImageFormat::D32_FLOAT;
        pipelineDesc.states.depthStencilState.depthEnable = true;
        pipelineDesc.states.depthStencilState.depthWriteEnable = true;
        pipelineDesc.states.depthStencilState.depthFunc = Renderer::ComparisonFunc::GREATER_EQUAL;
        pipelineDesc.states.rasterizerState.cullMode = Renderer::CullMode::BACK;
        pipelineDesc.states.rasterizerState.frontFaceMode = Renderer::FrontFaceState::COUNTERCLOCKWISE;
        _oneSidedPipeline = _renderer->CreatePipeline(pipelineDesc);

        pipelineDesc.debugName = "Model Diagnostic Two Sided";
        pipelineDesc.states.rasterizerState.cullMode = Renderer::CullMode::NONE;
        _twoSidedPipeline = _renderer->CreatePipeline(pipelineDesc);

        _descriptorSet.RegisterPipeline(_renderer, _oneSidedPipeline);
        _descriptorSet.RegisterPipeline(_renderer, _twoSidedPipeline);
        _descriptorSet.Init(_renderer);
    }

    void ModelDiagnosticPass::BindIfChanged(StringUtils::StringHash name, Renderer::BufferID buffer,
                                             Renderer::BufferID& current)
    {
        if (buffer == current)
            return;

        _descriptorSet.Bind(name, buffer);
        current = buffer;
    }

    void ModelDiagnosticPass::Upload(ModelView::ModelViewState& viewState,
                                     const ModelLoading::ModelGeometryStorage& geometry,
                                     const RenderScenes::RenderScene& scene)
    {
        viewState.SyncToGPU(_renderer);
        BindIfChanged("_diagnosticWork"_h, viewState.GetDiagnosticWork().GetBuffer(), _workBuffer);
        BindIfChanged("_modelInstances"_h, scene.GetModelInstances().GetRecords().GetBuffer(), _instanceBuffer);
        BindIfChanged("_modelMeshlets"_h, geometry.GetMeshlets().GetBuffer(), _meshletBuffer);
        BindIfChanged("_modelPositions"_h, geometry.GetPositions().GetBuffer(), _positionBuffer);
        BindIfChanged("_modelVertexAttributes"_h, geometry.GetVertexAttributes().GetBuffer(), _vertexAttributeBuffer);
        BindIfChanged("_modelMeshletVertexIndices"_h, geometry.GetMeshletVertexIndices().GetBuffer(),
                      _meshletVertexIndexBuffer);
        BindIfChanged("_modelMeshletTriangles"_h, geometry.GetMeshletTriangles().GetBuffer(), _meshletTriangleBuffer);
    }

    void ModelDiagnosticPass::AddPass(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                                      const RenderScenes::RenderView& view,
                                      const ModelView::ModelViewState& viewState,
                                      const ModelLoading::ModelGeometryStorage& geometry,
                                      const RenderScenes::RenderScene& scene, u8 frameIndex)
    {
        const u32 oneSidedCount = viewState.GetOneSidedCount();
        const u32 twoSidedCount = viewState.GetTwoSidedCount();
        if (CVAR_ModelMeshlets.Get() != ShowFlag::ENABLED || oneSidedCount + twoSidedCount == 0)
            return;

        struct Data
        {
            Renderer::ImageMutableResource color;
            Renderer::DepthImageMutableResource depth;
            Renderer::DescriptorSetResource globalSet;
            Renderer::DescriptorSetResource diagnosticSet;
        };

        renderGraph->AddPass<Data>("Model Diagnostic Meshlets",
            [this, &resources, &viewState, &geometry, &scene, &view](Data& data, Renderer::RenderGraphBuilder& builder) {
                using BufferUsage = Renderer::BufferPassUsage;
                data.color = builder.Write(view.GetColorTarget(), Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);
                data.depth = builder.Write(view.GetDepthTarget(), Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);
                builder.Read(resources.cameras.GetBuffer(), BufferUsage::GRAPHICS);
                builder.Read(viewState.GetDiagnosticWork().GetBuffer(), BufferUsage::GRAPHICS);
                builder.Read(scene.GetModelInstances().GetRecords().GetBuffer(), BufferUsage::GRAPHICS);
                builder.Read(geometry.GetMeshlets().GetBuffer(), BufferUsage::GRAPHICS);
                builder.Read(geometry.GetPositions().GetBuffer(), BufferUsage::GRAPHICS);
                builder.Read(geometry.GetVertexAttributes().GetBuffer(), BufferUsage::GRAPHICS);
                builder.Read(geometry.GetMeshletVertexIndices().GetBuffer(), BufferUsage::GRAPHICS);
                builder.Read(geometry.GetMeshletTriangles().GetBuffer(), BufferUsage::GRAPHICS);
                data.globalSet = builder.Use(resources.globalDescriptorSet);
                data.diagnosticSet = builder.Use(_descriptorSet);
                return true;
            },
            [this, &view, oneSidedCount, twoSidedCount, frameIndex](
                Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) {
                GPU_SCOPED_PROFILER_ZONE(commandList, ModelDiagnosticMeshlets);

                Renderer::RenderPassDesc renderPassDesc;
                graphResources.InitializeRenderPassDesc(renderPassDesc);
                renderPassDesc.renderTargets[0] = data.color;
                renderPassDesc.depthStencil = data.depth;
                commandList.BeginRenderPass(renderPassDesc);

                struct Constants
                {
                    u32 workOffset;
                    u32 viewIndex;
                    u32 debugMode;
                    u32 reserved;
                };

                auto draw = [&](Renderer::GraphicsPipelineID pipeline, u32 workOffset, u32 workCount) {
                    if (workCount == 0)
                        return;

                    Constants* constants = graphResources.FrameNew<Constants>();
                    constants->workOffset = workOffset;
                    constants->viewIndex = view.GetCameraIndex();
                    constants->debugMode = static_cast<u32>(glm::clamp(CVAR_ModelMeshletDebugMode.Get(), 0, 4));
                    constants->reserved = 0;
                    commandList.BeginPipeline(pipeline);
                    commandList.PushConstant(constants, 0, sizeof(Constants));
                    commandList.BindDescriptorSet(data.globalSet, frameIndex);
                    commandList.BindDescriptorSet(data.diagnosticSet, frameIndex);
                    commandList.DrawMeshTasks(workCount, 1, 1);
                    commandList.EndPipeline(pipeline);
                };

                draw(_oneSidedPipeline, 0, oneSidedCount);
                draw(_twoSidedPipeline, oneSidedCount, twoSidedCount);
                commandList.EndRenderPass(renderPassDesc);
            });
    }
} // namespace ModelPipeline
