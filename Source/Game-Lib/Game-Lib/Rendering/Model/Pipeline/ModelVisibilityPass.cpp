#include "ModelVisibilityPass.h"

#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Material/MaterialStorage.h"
#include "Game-Lib/Rendering/Model/Asset/ModelGeometryStorage.h"
#include "Game-Lib/Rendering/Model/View/ModelViewWorkResources.h"
#include "Game-Lib/Rendering/RenderResources.h"
#include "Game-Lib/Rendering/Scene/RenderScene.h"
#include "Game-Lib/Rendering/Scene/RenderView.h"

#include <Base/CVarSystem/CVarSystem.h>
#include <Renderer/RenderGraph.h>
#include <Renderer/Renderer.h>

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

        const std::vector<Renderer::PermutationField> solidPermutation = {{"ALPHA_TEST", "0"}};
        const std::vector<Renderer::PermutationField> alphaTestPermutation = {{"ALPHA_TEST", "1"}};
        Renderer::MeshShaderDesc solidMeshShader;
        solidMeshShader.shaderEntry = gameRenderer->GetShaderEntry(
            Renderer::GetShaderEntryNameHash("Model/Visibility.ms", solidPermutation), "Model/Visibility.ms");
        Renderer::MeshShaderDesc alphaTestMeshShader;
        alphaTestMeshShader.shaderEntry = gameRenderer->GetShaderEntry(
            Renderer::GetShaderEntryNameHash("Model/Visibility.ms", alphaTestPermutation), "Model/Visibility.ms");
        Renderer::PixelShaderDesc solidPixelShader;
        solidPixelShader.shaderEntry = gameRenderer->GetShaderEntry("Model/Visibility.ps"_h, "Model/Visibility.ps");
        Renderer::PixelShaderDesc pixelShader;
        pixelShader.shaderEntry =
            gameRenderer->GetShaderEntry("Model/VisibilityAlphaTest.ps"_h, "Model/VisibilityAlphaTest.ps");

        Renderer::GraphicsPipelineDesc desc;
        desc.debugName = "Model Visibility One Sided";
        desc.shaderStages = Renderer::MeshPipelineStages{.meshShader = renderer->LoadShader(solidMeshShader)};
        desc.states.pixelShader = renderer->LoadShader(solidPixelShader);
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

        desc.debugName = "Model Visibility Alpha Test One Sided";
        desc.shaderStages = Renderer::MeshPipelineStages{.meshShader = renderer->LoadShader(alphaTestMeshShader)};
        desc.states.pixelShader = renderer->LoadShader(pixelShader);
        desc.states.rasterizerState.cullMode = Renderer::CullMode::BACK;
        _alphaTestOneSidedPipeline = renderer->CreatePipeline(desc);
        desc.debugName = "Model Visibility Alpha Test Two Sided";
        desc.states.rasterizerState.cullMode = Renderer::CullMode::NONE;
        _alphaTestTwoSidedPipeline = renderer->CreatePipeline(desc);

        _descriptorSet.RegisterPipeline(renderer, _oneSidedPipeline);
        _descriptorSet.RegisterPipeline(renderer, _twoSidedPipeline);
        _descriptorSet.RegisterPipeline(renderer, _alphaTestOneSidedPipeline);
        _descriptorSet.RegisterPipeline(renderer, _alphaTestTwoSidedPipeline);
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
                for (Renderer::BufferID& queue : frame.rasterQueues)
                    queue = Renderer::BufferID::Invalid();
                for (Renderer::BufferID& queue : frame.geometryQueues)
                    queue = Renderer::BufferID::Invalid();
                frame.visibilityRecords = Renderer::BufferID::Invalid();
                frame.workStats = Renderer::BufferID::Invalid();
            }
        }

        static constexpr StringUtils::StringHash
            QUEUE_NAMES[ModelView::MODEL_VIEW_FRAME_COUNT][ModelView::MODEL_RASTER_CLASS_COUNT] = {
                {"_modelRasterQueueSolidOneSided0"_h, "_modelRasterQueueSolidTwoSided0"_h,
                 "_modelRasterQueueAlphaTestOneSided0"_h, "_modelRasterQueueAlphaTestTwoSided0"_h},
                {"_modelRasterQueueSolidOneSided1"_h, "_modelRasterQueueSolidTwoSided1"_h,
                 "_modelRasterQueueAlphaTestOneSided1"_h, "_modelRasterQueueAlphaTestTwoSided1"_h}};
        for (u32 frame = 0; frame < ModelView::MODEL_VIEW_FRAME_COUNT; ++frame)
            for (u32 rasterClass = 0; rasterClass < ModelView::MODEL_RASTER_CLASS_COUNT; ++rasterClass)
            {
                BindIfChanged(QUEUE_NAMES[frame][rasterClass], work.GetQueue(rasterClass, frame),
                              _bindings.frames[frame].rasterQueues[rasterClass]);
                static constexpr StringUtils::StringHash
                    GEOMETRY_QUEUE_NAMES[ModelView::MODEL_VIEW_FRAME_COUNT][ModelView::MODEL_RASTER_CLASS_COUNT] = {
                        {"_modelGeometryQueueSolidOneSided0"_h, "_modelGeometryQueueSolidTwoSided0"_h,
                         "_modelGeometryQueueAlphaTestOneSided0"_h, "_modelGeometryQueueAlphaTestTwoSided0"_h},
                        {"_modelGeometryQueueSolidOneSided1"_h, "_modelGeometryQueueSolidTwoSided1"_h,
                         "_modelGeometryQueueAlphaTestOneSided1"_h, "_modelGeometryQueueAlphaTestTwoSided1"_h}};
                BindIfChanged(GEOMETRY_QUEUE_NAMES[frame][rasterClass], work.GetGeometryQueue(rasterClass, frame),
                              _bindings.frames[frame].geometryQueues[rasterClass]);
            }
        BindIfChanged("_modelVisibilityRecords0"_h, work.GetVisibilityRecords(0),
                      _bindings.frames[0].visibilityRecords);
        BindIfChanged("_modelVisibilityStats0"_h, work.GetStatsBuffer(0), _bindings.frames[0].workStats);
        BindIfChanged("_modelVisibilityRecords1"_h, work.GetVisibilityRecords(1),
                      _bindings.frames[1].visibilityRecords);
        BindIfChanged("_modelVisibilityStats1"_h, work.GetStatsBuffer(1), _bindings.frames[1].workStats);

        _geometryBindings.Bind(_descriptorSet, geometry, scene);
    }

    void ModelVisibilityPass::AddPass(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                                      const RenderScenes::RenderView& view,
                                      const ModelView::ModelViewWorkResources& work,
                                      const ModelLoading::ModelGeometryStorage& geometry,
                                      const MaterialLoading::MaterialStorage& materials,
                                      const RenderScenes::RenderScene& scene, u8 frameIndex, u8 phase)
    {
        const u8 resourceIndex = phase == 1 ? (frameIndex + 1u) % ModelView::MODEL_VIEW_FRAME_COUNT : frameIndex;
        const bool geometryQueue = phase == 2;

        struct Data
        {
            Renderer::ImageMutableResource visibility;
            Renderer::DepthImageMutableResource depth;
            Renderer::BufferResource arguments;
            Renderer::DescriptorSetResource globalSet;
            Renderer::DescriptorSetResource materialSet;
            Renderer::DescriptorSetResource visibilitySet;
        };

        std::string passName = "Model Visibility: ";
        if (phase == 1)
            passName = "Model Occluders: ";
        else if (phase == 2)
            passName = "Model Geometry: ";
        renderGraph->AddPass<Data>(
            passName + view.GetDebugName(),
            [this, &resources, &view, &work, &geometry, &materials, &scene,
             resourceIndex, geometryQueue](Data& data, Renderer::RenderGraphBuilder& builder) {
                using Usage = Renderer::BufferPassUsage;
                const Renderer::LoadMode loadMode = view.ShouldClearTargets() ? Renderer::LoadMode::CLEAR : Renderer::LoadMode::LOAD;
                data.visibility = builder.Write(view.GetVisibilityTarget(), Renderer::PipelineType::GRAPHICS,
                                                loadMode);
                data.depth =
                    builder.Write(view.GetDepthTarget(), Renderer::PipelineType::GRAPHICS, loadMode);
                builder.Read(resources.cameras.GetBuffer(), Usage::GRAPHICS);
                for (u32 frame = 0; frame < ModelView::MODEL_VIEW_FRAME_COUNT; ++frame)
                {
                    for (u32 rasterClass = 0; rasterClass < ModelView::MODEL_RASTER_CLASS_COUNT; ++rasterClass)
                    {
                        builder.Read(work.GetQueue(rasterClass, frame), Usage::GRAPHICS);
                        builder.Read(work.GetGeometryQueue(rasterClass, frame), Usage::GRAPHICS);
                    }
                    builder.Read(work.GetVisibilityRecords(frame), Usage::GRAPHICS);
                    builder.Read(work.GetStatsBuffer(frame), Usage::GRAPHICS);
                }
                data.arguments = builder.Read(geometryQueue ? work.GetGeometryArguments(resourceIndex) :
                                                             work.GetArguments(resourceIndex), Usage::GRAPHICS);
                ModelGeometryBindings::RegisterUsage(builder, geometry, scene, Usage::GRAPHICS);
                builder.Read(materials.GetMaterialInstances().GetBuffer(), Usage::GRAPHICS);
                builder.Read(materials.GetMaterials().GetBuffer(), Usage::GRAPHICS);
                builder.Read(materials.GetParameterStorage().GetBuffer().GetBuffer(), Usage::GRAPHICS);
                builder.Read(materials.GetTextureIndices().GetBuffer(), Usage::GRAPHICS);
                builder.Read(materials.GetSamplerIDs().GetBuffer(), Usage::GRAPHICS);
                data.globalSet = builder.Use(resources.globalDescriptorSet);
                data.materialSet = builder.Use(resources.materialDescriptorSet);
                data.visibilitySet = builder.Use(_descriptorSet);
                return true;
            },
            [this, &view, frameIndex, resourceIndex, phase, geometryQueue](Data& data, Renderer::RenderGraphResources& graphResources,
                                      Renderer::CommandList& commandList) {
                GPU_SCOPED_PROFILER_ZONE(commandList, ModelVisibility);
                Renderer::RenderPassDesc pass;
                graphResources.InitializeRenderPassDesc(pass);
                pass.renderTargets[0] = data.visibility;
                pass.depthStencil = data.depth;
                const uvec2 dimensions = view.GetDimensions();
                commandList.SetViewport(0, 0, static_cast<f32>(dimensions.x), static_cast<f32>(dimensions.y), 0.0f, 1.0f);
                commandList.SetScissorRect(0, dimensions.x, 0, dimensions.y);
                commandList.BeginRenderPass(pass);

                struct Constants
                {
                    u32 queueIndex;
                    u32 viewIndex;
                    u32 resourceIndex;
                    u32 previousResource;
                    u32 geometryQueue;
                };
                auto Draw = [&](Renderer::GraphicsPipelineID pipeline, u32 queueIndex, bool usesMaterials) {
                    Constants* constants = graphResources.FrameNew<Constants>();
                    constants->queueIndex = queueIndex;
                    constants->viewIndex = view.GetCameraIndex();
                    constants->resourceIndex = resourceIndex;
                    constants->previousResource = phase == 1 ? 1u : 0u;
                    constants->geometryQueue = geometryQueue ? 1u : 0u;
                    commandList.BeginPipeline(pipeline);
                    commandList.PushConstant(constants, 0, sizeof(Constants));
                    commandList.BindDescriptorSet(data.globalSet, frameIndex);
                    if (usesMaterials)
                        commandList.BindDescriptorSet(data.materialSet, frameIndex);
                    commandList.BindDescriptorSet(data.visibilitySet, frameIndex);
                    commandList.DrawMeshTasksIndirect(data.arguments, queueIndex * sizeof(u32) *
                                                                          ModelView::MODEL_DISPATCH_ARGUMENT_COUNT);
                    commandList.EndPipeline(pipeline);
                };
                Draw(_oneSidedPipeline, ModelView::MODEL_RASTER_SOLID_ONE_SIDED, false);
                Draw(_twoSidedPipeline, ModelView::MODEL_RASTER_SOLID_TWO_SIDED, false);
                Draw(_alphaTestOneSidedPipeline, ModelView::MODEL_RASTER_ALPHA_TEST_ONE_SIDED, true);
                Draw(_alphaTestTwoSidedPipeline, ModelView::MODEL_RASTER_ALPHA_TEST_TWO_SIDED, true);
                commandList.EndRenderPass(pass);
            });
    }
} // namespace ModelPipeline
