#include "ModelVisibilityResolvePass.h"

#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Material/MaterialStorage.h"
#include "Game-Lib/Rendering/Model/Asset/ModelGeometryStorage.h"
#include "Game-Lib/Rendering/Model/ModelRendererMode.h"
#include "Game-Lib/Rendering/Model/View/ModelViewWorkResources.h"
#include "Game-Lib/Rendering/RenderResources.h"
#include "Game-Lib/Rendering/Scene/RenderScene.h"
#include "Game-Lib/Rendering/Scene/RenderView.h"

#include <Base/CVarSystem/CVarSystem.h>
#include <Renderer/RenderGraph.h>
#include <Renderer/Renderer.h>

AutoCVar_Int CVAR_ModelVisibilityDebugMode(
    CVarCategory::Client | CVarCategory::Rendering, "modelVisibilityDebugMode",
    "Model visibility debug: 0 instance, 1 material, 2 LOD, 3 submesh, 4 meshlet, 5 triangle, 6 normal, 7 UV", 4);

namespace ModelPipeline
{
    ModelVisibilityResolvePass::ModelVisibilityResolvePass(Renderer::Renderer* renderer, GameRenderer* gameRenderer)
        : _renderer(renderer), _preEffectsSet(Renderer::DescriptorSetSlot::PER_PASS),
          _velocitySet(Renderer::DescriptorSetSlot::PER_PASS), _highlightSet(Renderer::DescriptorSetSlot::PER_PASS),
          _diagnosticSet(Renderer::DescriptorSetSlot::PER_PASS)
    {
        Renderer::ComputePipelineDesc desc;
        Renderer::ComputeShaderDesc shader;
        shader.shaderEntry = gameRenderer->GetShaderEntry("Model/VisibilityPreEffects.cs"_h,
                                                           "Model/VisibilityPreEffects.cs");
        desc.debugName = "Model Visibility Pre Effects";
        desc.computeShader = renderer->LoadShader(shader);
        _preEffectsPipeline = renderer->CreatePipeline(desc);

        shader.shaderEntry = gameRenderer->GetShaderEntry("Model/VisibilityVelocity.cs"_h,
                                                           "Model/VisibilityVelocity.cs");
        desc.debugName = "Model Visibility Velocity";
        desc.computeShader = renderer->LoadShader(shader);
        _velocityPipeline = renderer->CreatePipeline(desc);

        shader.shaderEntry = gameRenderer->GetShaderEntry("Model/OpaqueHighlight.cs"_h,
                                                           "Model/OpaqueHighlight.cs");
        desc.debugName = "Model Opaque Highlight";
        desc.computeShader = renderer->LoadShader(shader);
        _highlightPipeline = renderer->CreatePipeline(desc);

        shader.shaderEntry = gameRenderer->GetShaderEntry("Model/VisibilityResolve.cs"_h,
                                                           "Model/VisibilityResolve.cs");
        desc.debugName = "Model Visibility Resolve";
        desc.computeShader = renderer->LoadShader(shader);
        _diagnosticPipeline = renderer->CreatePipeline(desc);

        _preEffectsSet.RegisterPipeline(renderer, _preEffectsPipeline);
        _preEffectsSet.Init(renderer);
        _velocitySet.RegisterPipeline(renderer, _velocityPipeline);
        _velocitySet.Init(renderer);
        _highlightSet.RegisterPipeline(renderer, _highlightPipeline);
        _highlightSet.Init(renderer);
        _diagnosticSet.RegisterPipeline(renderer, _diagnosticPipeline);
        _diagnosticSet.Init(renderer);
    }

    void ModelVisibilityResolvePass::BindCommon(Renderer::DescriptorSet& set, CommonBindings& bindings,
                                                 const ModelView::ModelViewWorkResources& work,
                                                 const ModelLoading::ModelGeometryStorage& geometry,
                                                 const RenderScenes::RenderScene& scene)
    {
        auto bind = [&set](StringUtils::StringHash name, Renderer::BufferID buffer, Renderer::BufferID& current) {
            if (buffer == current)
                return;
            set.Bind(name, buffer);
            current = buffer;
        };

        bind("_resolvedModelVisibilityRecords0"_h, work.GetVisibilityRecords(0),
             bindings.frames[0].visibilityRecords);
        bind("_resolvedModelVisibilityStats0"_h, work.GetStatsBuffer(0), bindings.frames[0].workStats);
        bind("_resolvedModelVisibilityRecords1"_h, work.GetVisibilityRecords(1),
             bindings.frames[1].visibilityRecords);
        bind("_resolvedModelVisibilityStats1"_h, work.GetStatsBuffer(1), bindings.frames[1].workStats);
        bind("_resolvedModelInstances"_h, scene.GetModelInstances().GetRecords().GetBuffer(),
             bindings.modelInstances);
        bind("_resolvedModels"_h, geometry.GetRecords().GetBuffer(), bindings.models);
        bind("_resolvedModelMeshes"_h, geometry.GetMeshes().GetBuffer(), bindings.meshes);
        bind("_resolvedModelLODs"_h, geometry.GetMeshLODs().GetBuffer(), bindings.lods);
        bind("_resolvedModelSubmeshes"_h, geometry.GetSubmeshes().GetBuffer(), bindings.submeshes);
        bind("_resolvedModelMeshlets"_h, geometry.GetMeshlets().GetBuffer(), bindings.meshlets);
        bind("_resolvedModelPositions"_h, geometry.GetPositions().GetBuffer(), bindings.positions);
        bind("_resolvedModelVertexAttributes"_h, geometry.GetVertexAttributes().GetBuffer(),
             bindings.vertexAttributes);
        bind("_resolvedModelVertexIndices"_h, geometry.GetMeshletVertexIndices().GetBuffer(),
             bindings.vertexIndices);
        bind("_resolvedModelTriangles"_h, geometry.GetMeshletTriangles().GetBuffer(), bindings.triangles);
    }

    void ModelVisibilityResolvePass::Upload(const ModelView::ModelViewWorkResources& work,
                                            const ModelLoading::ModelGeometryStorage& geometry,
                                            const RenderScenes::RenderScene& scene)
    {
        BindCommon(_preEffectsSet, _preEffectsBindings, work, geometry, scene);
        BindCommon(_velocitySet, _velocityBindings, work, geometry, scene);
        BindCommon(_diagnosticSet, _diagnosticBindings.common, work, geometry, scene);
        auto bindHighlight = [this](StringUtils::StringHash name, Renderer::BufferID buffer,
                                    Renderer::BufferID& current) {
            if (buffer == current)
                return;
            _highlightSet.Bind(name, buffer);
            current = buffer;
        };
        bindHighlight("_highlightModelVisibilityRecords0"_h, work.GetVisibilityRecords(0),
                      _highlightBindings.frames[0].visibilityRecords);
        bindHighlight("_highlightModelVisibilityStats0"_h, work.GetStatsBuffer(0),
                      _highlightBindings.frames[0].workStats);
        bindHighlight("_highlightModelVisibilityRecords1"_h, work.GetVisibilityRecords(1),
                      _highlightBindings.frames[1].visibilityRecords);
        bindHighlight("_highlightModelVisibilityStats1"_h, work.GetStatsBuffer(1),
                      _highlightBindings.frames[1].workStats);
        bindHighlight("_highlightModelInstances"_h, scene.GetModelInstances().GetRecords().GetBuffer(),
                      _highlightBindings.modelInstances);
        for (u32 frame = 0; frame < ModelView::MODEL_VIEW_FRAME_COUNT; ++frame)
        {
            const Renderer::BufferID cullReasons = work.GetCullReasons(frame);
            if (_diagnosticBindings.cullReasons[frame] == cullReasons)
                continue;
            _diagnosticSet.Bind(frame == 0 ? "_resolvedModelCullReasons0"_h : "_resolvedModelCullReasons1"_h, cullReasons);
            _diagnosticBindings.cullReasons[frame] = cullReasons;
        }
        const Renderer::BufferID materialTable = scene.GetModelMaterialTables().GetEntries().GetBuffer();
        if (_diagnosticBindings.materialTable != materialTable)
        {
            _diagnosticSet.Bind("_resolvedModelMaterialTable"_h, materialTable);
            _diagnosticBindings.materialTable = materialTable;
        }
    }

    void ModelVisibilityResolvePass::RegisterCommonUsage(Renderer::RenderGraphBuilder& builder,
                                                          const ModelView::ModelViewWorkResources& work,
                                                          const ModelLoading::ModelGeometryStorage& geometry,
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
    }

    void ModelVisibilityResolvePass::AddPreEffectsPass(
        Renderer::RenderGraph* renderGraph, RenderResources& resources, const RenderScenes::RenderView& view,
        const ModelView::ModelViewWorkResources& work, const ModelLoading::ModelGeometryStorage& geometry,
        const RenderScenes::RenderScene& scene, u8 frameIndex)
    {
        if (!ModelRendering::UseMeshletModelRenderer())
            return;
        struct Data
        {
            Renderer::ImageResource visibility;
            Renderer::ImageMutableResource normals;
            Renderer::DescriptorSetResource globalSet;
            Renderer::DescriptorSetResource resolveSet;
        };
        renderGraph->AddPass<Data>("Model PreEffects: " + view.GetDebugName(),
            [this, &resources, &view, &work, &geometry, &scene](Data& data, Renderer::RenderGraphBuilder& builder) {
                data.visibility = builder.Read(view.GetVisibilityTarget(), Renderer::PipelineType::COMPUTE);
                const Renderer::LoadMode loadMode = view.ShouldClearTargets() ? Renderer::LoadMode::CLEAR : Renderer::LoadMode::LOAD;
                data.normals = builder.Write(view.GetNormalTarget(), Renderer::PipelineType::COMPUTE, loadMode);
                builder.Read(resources.cameras.GetBuffer(), Renderer::BufferPassUsage::COMPUTE);
                RegisterCommonUsage(builder, work, geometry, scene);
                data.globalSet = builder.Use(resources.globalDescriptorSet);
                data.resolveSet = builder.Use(_preEffectsSet);
                return true;
            },
            [this, &view, frameIndex](Data& data, Renderer::RenderGraphResources& graphResources,
                                          Renderer::CommandList& commandList) {
                GPU_SCOPED_PROFILER_ZONE(commandList, ModelVisibilityPreEffects);
                commandList.BeginPipeline(_preEffectsPipeline);
                data.resolveSet.Bind("_visibilityBuffer"_h, data.visibility);
                data.resolveSet.Bind("_modelPackedNormals"_h, data.normals);
                commandList.BindDescriptorSet(data.globalSet, frameIndex);
                commandList.BindDescriptorSet(data.resolveSet, frameIndex);
                const vec2 size = static_cast<vec2>(view.GetDimensions());
                struct Constants { vec4 renderInfo; u32 resourceIndex; u32 viewIndex; };
                Constants* constants = graphResources.FrameNew<Constants>();
                constants->renderInfo = vec4(size, 1.0f / size);
                constants->resourceIndex = frameIndex;
                constants->viewIndex = view.GetCameraIndex();
                commandList.PushConstant(constants, 0, sizeof(Constants));
                commandList.Dispatch((static_cast<u32>(size.x) + 7u) / 8u,
                                     (static_cast<u32>(size.y) + 7u) / 8u, 1);
                commandList.EndPipeline(_preEffectsPipeline);
            }, Renderer::RenderPassFlags::None);

        if (view.GetMotionTarget() == Renderer::ImageID::Invalid())
            return;
        struct VelocityData
        {
            Renderer::ImageResource visibility;
            Renderer::ImageMutableResource motion;
            Renderer::DescriptorSetResource globalSet;
            Renderer::DescriptorSetResource resolveSet;
        };
        renderGraph->AddPass<VelocityData>("Model Velocity: " + view.GetDebugName(),
            [this, &resources, &view, &work, &geometry, &scene](VelocityData& data, Renderer::RenderGraphBuilder& builder) {
                data.visibility = builder.Read(view.GetVisibilityTarget(), Renderer::PipelineType::COMPUTE);
                data.motion = builder.Write(view.GetMotionTarget(), Renderer::PipelineType::COMPUTE, Renderer::LoadMode::LOAD);
                builder.Read(resources.cameras.GetBuffer(), Renderer::BufferPassUsage::COMPUTE);
                RegisterCommonUsage(builder, work, geometry, scene);
                data.globalSet = builder.Use(resources.globalDescriptorSet);
                data.resolveSet = builder.Use(_velocitySet);
                return true;
            },
            [this, &view, frameIndex](VelocityData& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) {
                GPU_SCOPED_PROFILER_ZONE(commandList, ModelVisibilityVelocity);
                commandList.BeginPipeline(_velocityPipeline);
                data.resolveSet.Bind("_visibilityBuffer"_h, data.visibility);
                data.resolveSet.Bind("_modelMotionVectors"_h, data.motion);
                commandList.BindDescriptorSet(data.globalSet, frameIndex);
                commandList.BindDescriptorSet(data.resolveSet, frameIndex);
                const vec2 size = static_cast<vec2>(view.GetDimensions());
                struct Constants { vec4 renderInfo; mat4x4 previousWorldToClip; u32 resourceIndex; u32 viewIndex; u32 cameraValid; };
                Constants* constants = graphResources.FrameNew<Constants>();
                constants->renderInfo = vec4(size, 1.0f / size);
                constants->previousWorldToClip = view.GetPreviousWorldToClip();
                constants->resourceIndex = frameIndex;
                constants->viewIndex = view.GetCameraIndex();
                constants->cameraValid = view.IsTemporalCameraValid() ? 1u : 0u;
                commandList.PushConstant(constants, 0, sizeof(Constants));
                commandList.Dispatch((static_cast<u32>(size.x) + 7u) / 8u, (static_cast<u32>(size.y) + 7u) / 8u, 1u);
                commandList.EndPipeline(_velocityPipeline);
            }, Renderer::RenderPassFlags::None);
    }

    void ModelVisibilityResolvePass::AddOpaqueHighlightPass(
        Renderer::RenderGraph* renderGraph, const RenderScenes::RenderView& view,
        const ModelView::ModelViewWorkResources& work, const RenderScenes::RenderScene& scene, u8 frameIndex)
    {
        if (!ModelRendering::UseMeshletModelRenderer() || !scene.HasModelHighlights())
            return;

        struct Data
        {
            Renderer::ImageResource visibility;
            Renderer::ImageMutableResource color;
            Renderer::DescriptorSetResource highlightSet;
        };
        renderGraph->AddPass<Data>("Opaque Highlights: " + view.GetDebugName(),
            [this, &view, &work, &scene](Data& data, Renderer::RenderGraphBuilder& builder) {
                data.visibility = builder.Read(view.GetVisibilityTarget(), Renderer::PipelineType::COMPUTE);
                data.color = builder.Write(view.GetColorTarget(), Renderer::PipelineType::COMPUTE,
                                           Renderer::LoadMode::LOAD);
                for (u32 frame = 0; frame < ModelView::MODEL_VIEW_FRAME_COUNT; ++frame)
                {
                    builder.Read(work.GetVisibilityRecords(frame), Renderer::BufferPassUsage::COMPUTE);
                    builder.Read(work.GetStatsBuffer(frame), Renderer::BufferPassUsage::COMPUTE);
                }
                builder.Read(scene.GetModelInstances().GetRecords().GetBuffer(),
                             Renderer::BufferPassUsage::COMPUTE);
                data.highlightSet = builder.Use(_highlightSet);
                return true;
            },
            [this, &view, frameIndex](Data& data, Renderer::RenderGraphResources& graphResources,
                                     Renderer::CommandList& commandList) {
                GPU_SCOPED_PROFILER_ZONE(commandList, OpaqueHighlights);
                commandList.BeginPipeline(_highlightPipeline);
                data.highlightSet.Bind("_highlightVisibility"_h, data.visibility);
                data.highlightSet.Bind("_highlightColor"_h, data.color);
                commandList.BindDescriptorSet(data.highlightSet, frameIndex);
                struct Constants { uvec2 renderSize; u32 resourceIndex; u32 padding; };
                Constants* constants = graphResources.FrameNew<Constants>();
                constants->renderSize = view.GetDimensions();
                constants->resourceIndex = frameIndex;
                constants->padding = 0u;
                commandList.PushConstant(constants, 0, sizeof(Constants));
                commandList.Dispatch((constants->renderSize.x + 7u) / 8u,
                                     (constants->renderSize.y + 7u) / 8u, 1u);
                commandList.EndPipeline(_highlightPipeline);
            });
    }

    void ModelVisibilityResolvePass::AddDiagnosticPass(
        Renderer::RenderGraph* renderGraph, RenderResources& resources, const RenderScenes::RenderView& view,
        const ModelView::ModelViewWorkResources& work, const ModelLoading::ModelGeometryStorage& geometry,
        const MaterialLoading::MaterialStorage& materials, const RenderScenes::RenderScene& scene, u8 frameIndex)
    {
        if (!ModelRendering::UseMeshletModelRenderer())
            return;
        struct Data
        {
            Renderer::ImageResource visibility;
            Renderer::ImageMutableResource color;
            Renderer::DescriptorSetResource globalSet;
            Renderer::DescriptorSetResource materialSet;
            Renderer::DescriptorSetResource resolveSet;
        };
        renderGraph->AddPass<Data>("Model Diagnostic: " + view.GetDebugName(),
            [this, &resources, &view, &work, &geometry, &materials, &scene](Data& data, Renderer::RenderGraphBuilder& builder) {
                data.visibility = builder.Read(view.GetVisibilityTarget(), Renderer::PipelineType::COMPUTE);
                data.color = builder.Write(view.GetColorTarget(), Renderer::PipelineType::COMPUTE, Renderer::LoadMode::LOAD);
                builder.Read(resources.cameras.GetBuffer(), Renderer::BufferPassUsage::COMPUTE);
                RegisterCommonUsage(builder, work, geometry, scene);
                builder.Read(scene.GetModelMaterialTables().GetEntries().GetBuffer(), Renderer::BufferPassUsage::COMPUTE);
                builder.Read(work.GetCullReasons(0), Renderer::BufferPassUsage::COMPUTE);
                builder.Read(work.GetCullReasons(1), Renderer::BufferPassUsage::COMPUTE);
                builder.Read(materials.GetMaterialInstances().GetBuffer(), Renderer::BufferPassUsage::COMPUTE);
                data.globalSet = builder.Use(resources.globalDescriptorSet);
                data.materialSet = builder.Use(resources.materialDescriptorSet);
                data.resolveSet = builder.Use(_diagnosticSet);
                return true;
            },
            [this, &view, frameIndex](Data& data, Renderer::RenderGraphResources& graphResources,
                                          Renderer::CommandList& commandList) {
                GPU_SCOPED_PROFILER_ZONE(commandList, ModelVisibilityDiagnosticResolve);
                commandList.BeginPipeline(_diagnosticPipeline);
                data.resolveSet.Bind("_visibilityBuffer"_h, data.visibility);
                data.resolveSet.Bind("_modelVisibilityResolvedColor"_h, data.color);
                commandList.BindDescriptorSet(data.globalSet, frameIndex);
                commandList.BindDescriptorSet(data.materialSet, frameIndex);
                commandList.BindDescriptorSet(data.resolveSet, frameIndex);
                const vec2 size = static_cast<vec2>(view.GetDimensions());
                struct Constants { vec4 renderInfo; u32 resourceIndex; u32 viewIndex; u32 debugMode; };
                Constants* constants = graphResources.FrameNew<Constants>();
                constants->renderInfo = vec4(size, 1.0f / size);
                constants->resourceIndex = frameIndex;
                constants->viewIndex = view.GetCameraIndex();
                constants->debugMode = ModelRendering::ShowModelCullReasons() ? 8u : static_cast<u32>(glm::clamp(CVAR_ModelVisibilityDebugMode.Get(), 0, 7));
                commandList.PushConstant(constants, 0, sizeof(Constants));
                commandList.Dispatch((static_cast<u32>(size.x) + 7u) / 8u,
                                     (static_cast<u32>(size.y) + 7u) / 8u, 1);
                commandList.EndPipeline(_diagnosticPipeline);
            });
    }
} // namespace ModelPipeline
