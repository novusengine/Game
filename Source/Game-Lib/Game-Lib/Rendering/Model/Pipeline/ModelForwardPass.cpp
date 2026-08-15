#include "ModelForwardPass.h"

#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Light/LightRenderer.h"
#include "Game-Lib/Rendering/Material/MaterialRenderer.h"
#include "Game-Lib/Rendering/Material/MaterialStorage.h"
#include "Game-Lib/Rendering/Model/Asset/ModelGeometryStorage.h"
#include "Game-Lib/Rendering/Model/ModelRendererMode.h"
#include "Game-Lib/Rendering/Model/View/ModelViewWorkResources.h"
#include "Game-Lib/Rendering/RenderResources.h"
#include "Game-Lib/Rendering/Scene/RenderScene.h"
#include "Game-Lib/Rendering/Scene/RenderView.h"
#include "Game-Lib/Rendering/Shadow/ShadowRenderer.h"

#include <Base/CVarSystem/CVarSystem.h>
#include <Renderer/RenderGraph.h>
#include <Renderer/Renderer.h>

#include <algorithm>

namespace ModelPipeline
{
    ModelForwardPass::ModelForwardPass(Renderer::Renderer* renderer, GameRenderer* gameRenderer)
        : _renderer(renderer), _gameRenderer(gameRenderer), _descriptorSet(Renderer::DescriptorSetSlot::PER_PASS)
    {
        Renderer::MeshShaderDesc meshShader;
        meshShader.shaderEntry = gameRenderer->GetShaderEntry("Model/Forward.ms"_h, "Model/Forward.ms");
        Renderer::PixelShaderDesc pixelShader;
        pixelShader.shaderEntry = gameRenderer->GetShaderEntry("Generated/MaterialGroupsDirectForward.ps"_h,
                                                                "Generated/MaterialGroupsDirectForward.ps");

        Renderer::GraphicsPipelineDesc desc;
        desc.debugName = "Model Direct Forward One Sided";
        desc.shaderStages = Renderer::MeshPipelineStages{.meshShader = renderer->LoadShader(meshShader)};
        desc.states.pixelShader = renderer->LoadShader(pixelShader);
        desc.states.renderTargetFormats[0] = renderer->GetSwapChainImageFormat();
        desc.states.depthStencilFormat = Renderer::DepthImageFormat::D32_FLOAT;
        desc.states.depthStencilState.depthEnable = true;
        desc.states.depthStencilState.depthWriteEnable = true;
        desc.states.depthStencilState.depthFunc = Renderer::ComparisonFunc::GREATER_EQUAL;
        desc.states.rasterizerState.cullMode = Renderer::CullMode::BACK;
        desc.states.rasterizerState.frontFaceMode = Renderer::FrontFaceState::COUNTERCLOCKWISE;
        _oneSidedPipeline = renderer->CreatePipeline(desc);

        desc.debugName = "Model Direct Forward Two Sided";
        desc.states.rasterizerState.cullMode = Renderer::CullMode::NONE;
        _twoSidedPipeline = renderer->CreatePipeline(desc);

        _descriptorSet.RegisterPipeline(renderer, _oneSidedPipeline);
        _descriptorSet.RegisterPipeline(renderer, _twoSidedPipeline);
        _descriptorSet.Init(renderer);
    }

    bool ModelForwardPass::Bind(StringUtils::StringHash name, Renderer::BufferID buffer, Renderer::BufferID& current)
    {
        if (buffer == current)
            return false;
        _descriptorSet.Bind(name, buffer);
        current = buffer;
        return true;
    }

    bool ModelForwardPass::Upload(const ModelView::ModelViewWorkResources& work,
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
                frame.visibilityRecords = Renderer::BufferID::Invalid();
                frame.workStats = Renderer::BufferID::Invalid();
            }
            _bindings.modelInstances = Renderer::BufferID::Invalid();
            _bindings.models = Renderer::BufferID::Invalid();
            _bindings.meshes = Renderer::BufferID::Invalid();
            _bindings.lods = Renderer::BufferID::Invalid();
            _bindings.submeshes = Renderer::BufferID::Invalid();
            _bindings.meshlets = Renderer::BufferID::Invalid();
            _bindings.positions = Renderer::BufferID::Invalid();
            _bindings.vertexAttributes = Renderer::BufferID::Invalid();
            _bindings.vertexIndices = Renderer::BufferID::Invalid();
            _bindings.triangles = Renderer::BufferID::Invalid();
            _bindings.materialTable = Renderer::BufferID::Invalid();
            _descriptorWarmupFrames = ModelView::MODEL_VIEW_FRAME_COUNT;
        }

        bool changed = false;
        static constexpr StringUtils::StringHash
            QUEUE_NAMES[ModelView::MODEL_VIEW_FRAME_COUNT][ModelView::MODEL_RASTER_CLASS_COUNT] = {
                {"_modelRasterQueueSolidOneSided0"_h, "_modelRasterQueueSolidTwoSided0"_h,
                 "_modelRasterQueueAlphaTestOneSided0"_h, "_modelRasterQueueAlphaTestTwoSided0"_h},
                {"_modelRasterQueueSolidOneSided1"_h, "_modelRasterQueueSolidTwoSided1"_h,
                 "_modelRasterQueueAlphaTestOneSided1"_h, "_modelRasterQueueAlphaTestTwoSided1"_h}};
        for (u32 frame = 0; frame < ModelView::MODEL_VIEW_FRAME_COUNT; ++frame)
        {
            for (u32 rasterClass = 0; rasterClass < ModelView::MODEL_RASTER_CLASS_COUNT; ++rasterClass)
                changed |= Bind(QUEUE_NAMES[frame][rasterClass], work.GetQueue(rasterClass, frame),
                                _bindings.frames[frame].rasterQueues[rasterClass]);
        }
        changed |= Bind("_modelVisibilityRecords0"_h, work.GetVisibilityRecords(0), _bindings.frames[0].visibilityRecords);
        changed |= Bind("_modelVisibilityStats0"_h, work.GetStatsBuffer(0), _bindings.frames[0].workStats);
        changed |= Bind("_modelVisibilityRecords1"_h, work.GetVisibilityRecords(1), _bindings.frames[1].visibilityRecords);
        changed |= Bind("_modelVisibilityStats1"_h, work.GetStatsBuffer(1), _bindings.frames[1].workStats);
        changed |= Bind("_modelVisibilityInstances"_h, scene.GetModelInstances().GetRecords().GetBuffer(), _bindings.modelInstances);
        changed |= Bind("_modelVisibilityModels"_h, geometry.GetRecords().GetBuffer(), _bindings.models);
        changed |= Bind("_modelVisibilityMeshes"_h, geometry.GetMeshes().GetBuffer(), _bindings.meshes);
        changed |= Bind("_modelVisibilityLODs"_h, geometry.GetMeshLODs().GetBuffer(), _bindings.lods);
        changed |= Bind("_modelVisibilitySubmeshes"_h, geometry.GetSubmeshes().GetBuffer(), _bindings.submeshes);
        changed |= Bind("_modelVisibilityMeshlets"_h, geometry.GetMeshlets().GetBuffer(), _bindings.meshlets);
        changed |= Bind("_modelVisibilityPositions"_h, geometry.GetPositions().GetBuffer(), _bindings.positions);
        changed |= Bind("_modelVisibilityVertexAttributes"_h, geometry.GetVertexAttributes().GetBuffer(), _bindings.vertexAttributes);
        changed |= Bind("_modelVisibilityVertexIndices"_h, geometry.GetMeshletVertexIndices().GetBuffer(), _bindings.vertexIndices);
        changed |= Bind("_modelVisibilityTriangles"_h, geometry.GetMeshletTriangles().GetBuffer(), _bindings.triangles);
        changed |= Bind("_modelVisibilityMaterialTable"_h, scene.GetModelMaterialTables().GetEntries().GetBuffer(), _bindings.materialTable);
        if (changed)
            _descriptorWarmupFrames = ModelView::MODEL_VIEW_FRAME_COUNT;
        if (_descriptorWarmupFrames > 0)
        {
            --_descriptorWarmupFrames;
            return false;
        }
        return true;
    }

    void ModelForwardPass::AddPass(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                                   const RenderScenes::RenderView& view,
                                   const ModelView::ModelViewWorkResources& work,
                                   const ModelLoading::ModelGeometryStorage& geometry,
                                   const MaterialLoading::MaterialStorage& materials,
                                   const RenderScenes::RenderScene& scene, u8 frameIndex)
    {
        if (!ModelRendering::UseMeshletModelRenderer())
            return;

        struct Data
        {
            Renderer::ImageMutableResource color;
            Renderer::DepthImageMutableResource depth;
            Renderer::ImageResource ambientVisibility;
            Renderer::ImageResource svsmPagePool;
            Renderer::ImageResource svsmDynamicPagePool;
            Renderer::BufferResource arguments;
            Renderer::DescriptorSetResource globalSet;
            Renderer::DescriptorSetResource lightSet;
            Renderer::DescriptorSetResource materialSet;
            Renderer::DescriptorSetResource forwardSet;
        };

        std::string passName = "Forward Models: " + view.GetDebugName();
        passName.resize(std::min<size_t>(passName.size(), 31));
        renderGraph->AddPass<Data>(passName,
            [this, &resources, &view, &work, &geometry, &materials, &scene, frameIndex](Data& data,
                Renderer::RenderGraphBuilder& builder) {
                using Usage = Renderer::BufferPassUsage;
                const Renderer::LoadMode loadMode = view.ShouldClearTargets() ? Renderer::LoadMode::CLEAR : Renderer::LoadMode::LOAD;
                data.color = builder.Write(view.GetColorTarget(), Renderer::PipelineType::GRAPHICS, loadMode);
                data.depth = builder.Write(view.GetDepthTarget(), Renderer::PipelineType::GRAPHICS, loadMode);
                data.ambientVisibility = builder.Read(resources.ssaoTarget, Renderer::PipelineType::GRAPHICS);
                builder.Read(resources.cameras.GetBuffer(), Usage::GRAPHICS);
                builder.Read(_gameRenderer->GetMaterialRenderer()->GetDirectionalLightBuffer(), Usage::GRAPHICS);
                ShadowRenderer* shadows = _gameRenderer->GetShadowRenderer();
                data.svsmPagePool = builder.Read(shadows->GetSVSMPagePoolOrPlaceholder(), Renderer::PipelineType::GRAPHICS);
                data.svsmDynamicPagePool = builder.Read(shadows->GetSVSMDynamicPagePoolOrPlaceholder(), Renderer::PipelineType::GRAPHICS);
                builder.Read(shadows->GetSVSMDataBuffer(), Usage::GRAPHICS);
                builder.Read(shadows->GetSVSMPageTableBuffer(), Usage::GRAPHICS);
                builder.Read(shadows->GetSVSMDynamicPageTableBuffer(), Usage::GRAPHICS);
                for (u32 frame = 0; frame < ModelView::MODEL_VIEW_FRAME_COUNT; ++frame)
                {
                    for (u32 rasterClass = 0; rasterClass < ModelView::MODEL_RASTER_CLASS_COUNT; ++rasterClass)
                        builder.Read(work.GetQueue(rasterClass, frame), Usage::GRAPHICS);
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
                builder.Read(scene.GetModelMaterialTables().GetEntries().GetBuffer(), Usage::GRAPHICS);
                builder.Read(materials.GetMaterialInstances().GetBuffer(), Usage::GRAPHICS);
                builder.Read(materials.GetMaterials().GetBuffer(), Usage::GRAPHICS);
                builder.Read(materials.GetParameterStorage().GetBuffer().GetBuffer(), Usage::GRAPHICS);
                builder.Read(materials.GetTextureIndices().GetBuffer(), Usage::GRAPHICS);
                builder.Read(materials.GetSamplerIDs().GetBuffer(), Usage::GRAPHICS);
                data.globalSet = builder.Use(resources.globalDescriptorSet);
                data.lightSet = builder.Use(resources.lightDescriptorSet);
                data.materialSet = builder.Use(resources.materialDescriptorSet);
                data.forwardSet = builder.Use(_descriptorSet);
                return true;
            },
            [this, &view, frameIndex](Data& data, Renderer::RenderGraphResources& graphResources,
                Renderer::CommandList& commandList) {
                GPU_SCOPED_PROFILER_ZONE(commandList, ModelDirectForward);
                data.forwardSet.Bind("_ambientVisibility"_h, data.ambientVisibility);
                Renderer::RenderPassDesc pass;
                graphResources.InitializeRenderPassDesc(pass);
                pass.renderTargets[0] = data.color;
                pass.depthStencil = data.depth;
                const uvec2 dimensions = view.GetDimensions();
                commandList.SetViewport(0, 0, static_cast<f32>(dimensions.x), static_cast<f32>(dimensions.y), 0.0f, 1.0f);
                commandList.SetScissorRect(0, dimensions.x, 0, dimensions.y);
                commandList.BeginRenderPass(pass);

                CVarSystem* cvars = CVarSystem::Get();
                const f32 shadowStrength = static_cast<f32>(*cvars->GetFloatCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowStrength"));
                ShadowRenderer* shadows = _gameRenderer->GetShadowRenderer();
                const bool shadowsReady = view.UsesWorldShadows() && *cvars->GetIntCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowEnabled") != 0 && shadowStrength > 0.0f && shadows->GetSVSMPagePool() != Renderer::ImageID::Invalid();
                struct Constants
                {
                    vec4 shadowSettings;
                    u32 viewIndex;
                    u32 numDirectionalLights;
                    u32 shadowsReady;
                    u32 resourceIndex;
                    u32 queueIndex;
                    u32 rasterClass;
                    u32 reserved0;
                    u32 reserved1;
                };
                auto draw = [&](Renderer::GraphicsPipelineID pipeline, u32 queueIndex) {
                    Constants* constants = graphResources.FrameNew<Constants>();
                    constants->shadowSettings = vec4(shadowStrength,
                        *cvars->GetFloatCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowNormalOffsetBias"),
                        *cvars->GetFloatCVar(CVarCategory::Client | CVarCategory::Rendering, "svsmConstantBias"), 0.0f);
                    constants->viewIndex = view.GetCameraIndex();
                    constants->numDirectionalLights = _gameRenderer->GetMaterialRenderer()->GetNumDirectionalLights();
                    constants->shadowsReady = shadowsReady ? 1u : 0u;
                    constants->resourceIndex = frameIndex;
                    constants->queueIndex = queueIndex;
                    constants->rasterClass = queueIndex >= ModelView::MODEL_RASTER_ALPHA_TEST_ONE_SIDED ? 1u : 0u;
                    constants->reserved0 = 0;
                    constants->reserved1 = 0;
                    commandList.BeginPipeline(pipeline);
                    commandList.PushConstant(constants, 0, sizeof(*constants));
                    commandList.BindDescriptorSet(data.globalSet, frameIndex);
                    commandList.BindDescriptorSet(data.lightSet, frameIndex);
                    commandList.BindDescriptorSet(data.materialSet, frameIndex);
                    commandList.BindDescriptorSet(data.forwardSet, frameIndex);
                    commandList.DrawMeshTasksIndirect(data.arguments, queueIndex * sizeof(u32) * ModelView::MODEL_DISPATCH_ARGUMENT_COUNT);
                    commandList.EndPipeline(pipeline);
                };
                draw(_oneSidedPipeline, ModelView::MODEL_RASTER_SOLID_ONE_SIDED);
                draw(_twoSidedPipeline, ModelView::MODEL_RASTER_SOLID_TWO_SIDED);
                draw(_oneSidedPipeline, ModelView::MODEL_RASTER_ALPHA_TEST_ONE_SIDED);
                draw(_twoSidedPipeline, ModelView::MODEL_RASTER_ALPHA_TEST_TWO_SIDED);
                commandList.EndRenderPass(pass);
            });
    }
} // namespace ModelPipeline
