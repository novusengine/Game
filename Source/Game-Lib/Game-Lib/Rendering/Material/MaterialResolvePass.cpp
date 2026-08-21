#include "MaterialResolvePass.h"

#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Light/LightRenderer.h"
#include "Game-Lib/Rendering/Material/MaterialStorage.h"
#include "Game-Lib/Rendering/Material/MaterialRenderer.h"
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

AutoCVar_Int CVAR_MaterialDebugMode(
    CVarCategory::Client | CVarCategory::Rendering, "materialDebugMode",
    "Material debug: 0 shaded, 1 material, 2 program, 3 execution group, 4 lighting model, 5 texture", 0);

namespace MaterialRendering
{
    MaterialResolvePass::MaterialResolvePass(Renderer::Renderer* renderer, GameRenderer* gameRenderer)
        : _gameRenderer(gameRenderer), _resolveSet(Renderer::DescriptorSetSlot::PER_PASS)
    {
        Renderer::ComputePipelineDesc desc;
        Renderer::ComputeShaderDesc shader;
        shader.shaderEntry = gameRenderer->GetShaderEntry("Material/MaterialResolveUnified.cs"_h,
                                                          "Material/MaterialResolveUnified.cs");
        desc.debugName = "Material Resolve Unified";
        desc.computeShader = renderer->LoadShader(shader);
        _resolvePipeline = renderer->CreatePipeline(desc);
        _resolveSet.RegisterPipeline(renderer, _resolvePipeline);
        _resolveSet.Init(renderer);
    }

    void MaterialResolvePass::BindModelResources(Renderer::DescriptorSet& set, ModelBindings& bindings,
                                                 const ModelView::ModelViewWorkResources& work,
                                                 const ModelLoading::ModelGeometryStorage& geometry,
                                                 const RenderScenes::RenderScene& scene, bool& changed)
    {
        auto Bind = [&set, &changed](StringUtils::StringHash name, Renderer::BufferID buffer,
                                     Renderer::BufferID& current) {
            if (buffer == current)
                return;
            set.Bind(name, buffer, true);
            current = buffer;
            changed = true;
        };
        Bind("_resolvedModelVisibilityRecords0"_h, work.GetVisibilityRecords(0), bindings.visibilityRecords[0]);
        Bind("_resolvedModelVisibilityStats0"_h, work.GetStatsBuffer(0), bindings.stats[0]);
        Bind("_resolvedModelVisibilityRecords1"_h, work.GetVisibilityRecords(1), bindings.visibilityRecords[1]);
        Bind("_resolvedModelVisibilityStats1"_h, work.GetStatsBuffer(1), bindings.stats[1]);
        Bind("_resolvedModelInstances"_h, scene.GetModelInstances().GetRecords().GetBuffer(), bindings.instances);
        Bind("_resolvedModels"_h, geometry.GetRecords().GetBuffer(), bindings.models);
        Bind("_resolvedModelMeshes"_h, geometry.GetMeshes().GetBuffer(), bindings.meshes);
        Bind("_resolvedModelLODs"_h, geometry.GetMeshLODs().GetBuffer(), bindings.lods);
        Bind("_resolvedModelSubmeshes"_h, geometry.GetSubmeshes().GetBuffer(), bindings.submeshes);
        Bind("_resolvedModelMeshlets"_h, geometry.GetMeshlets().GetBuffer(), bindings.meshlets);
        Bind("_resolvedModelPositions"_h, geometry.GetPositions().GetBuffer(), bindings.positions);
        Bind("_resolvedModelVertexAttributes"_h, geometry.GetVertexAttributes().GetBuffer(), bindings.attributes);
        Bind("_resolvedModelVertexIndices"_h, geometry.GetMeshletVertexIndices().GetBuffer(), bindings.indices);
        Bind("_resolvedModelTriangles"_h, geometry.GetMeshletTriangles().GetBuffer(), bindings.triangles);
        Bind("_resolvedModelMaterialTable"_h, scene.GetModelMaterialTables().GetEntries().GetBuffer(),
             bindings.materialTable);
    }

    bool MaterialResolvePass::Upload(const ModelView::ModelViewWorkResources& work,
                                     const ModelLoading::ModelGeometryStorage& geometry,
                                     const RenderScenes::RenderScene& scene)
    {
        bool changed = false;
        BindModelResources(_resolveSet, _resolveBindings.model, work, geometry, scene, changed);
        return !_resolveSet.HasPendingBufferWrites();
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

    void MaterialResolvePass::AddResolvePass(
        Renderer::RenderGraph* renderGraph, RenderResources& renderResources, const RenderScenes::RenderView& view,
        const ModelView::ModelViewWorkResources& work,
        const ModelLoading::ModelGeometryStorage& geometry, const MaterialLoading::MaterialStorage& materials,
        const RenderScenes::RenderScene& scene, u8 frameIndex)
    {
        if (!ModelRendering::UseMeshletModelRenderer())
            return;
        struct Data
        {
            Renderer::ImageResource visibility;
            Renderer::ImageResource ambientVisibility;
            Renderer::ImageResource svsmPagePool;
            Renderer::ImageResource svsmDynamicPagePool;
            Renderer::ImageMutableResource color;
            Renderer::DescriptorSetResource globalSet;
            Renderer::DescriptorSetResource lightSet;
            Renderer::DescriptorSetResource materialSet;
            Renderer::DescriptorSetResource resolveSet;
        };
        renderGraph->AddPass<Data>("Material Resolve: " + view.GetDebugName(),
            [this, &renderResources, &view, &work, &geometry, &materials, &scene](
                Data& data, Renderer::RenderGraphBuilder& builder) {
                using Usage = Renderer::BufferPassUsage;
                data.visibility = builder.Read(view.GetVisibilityTarget(), Renderer::PipelineType::COMPUTE);
                data.ambientVisibility = builder.Read(renderResources.ssaoTarget, Renderer::PipelineType::COMPUTE);
                const Renderer::LoadMode loadMode = view.ShouldClearTargets() ? Renderer::LoadMode::CLEAR : Renderer::LoadMode::LOAD;
                data.color = builder.Write(view.GetColorTarget(), Renderer::PipelineType::COMPUTE, loadMode);
                builder.Read(renderResources.cameras.GetBuffer(), Usage::COMPUTE);
                builder.Read(_gameRenderer->GetMaterialRenderer()->GetDirectionalLightBuffer(), Usage::COMPUTE);
                ShadowRenderer* shadowRenderer = _gameRenderer->GetShadowRenderer();
                data.svsmPagePool = builder.Read(shadowRenderer->GetSVSMPagePoolOrPlaceholder(), Renderer::PipelineType::COMPUTE);
                data.svsmDynamicPagePool = builder.Read(shadowRenderer->GetSVSMDynamicPagePoolOrPlaceholder(), Renderer::PipelineType::COMPUTE);
                builder.Read(shadowRenderer->GetSVSMDataBuffer(), Usage::COMPUTE);
                builder.Read(shadowRenderer->GetSVSMPageTableBuffer(), Usage::COMPUTE);
                builder.Read(shadowRenderer->GetSVSMDynamicPageTableBuffer(), Usage::COMPUTE);
                RegisterModelUsage(builder, work, geometry, materials, scene);
                _gameRenderer->GetLightRenderer()->RegisterMaterialPassBufferUsage(builder);
                builder.Read(materials.GetMaterials().GetBuffer(), Usage::COMPUTE);
                builder.Read(materials.GetParameterStorage().GetBuffer().GetBuffer(), Usage::COMPUTE);
                data.globalSet = builder.Use(renderResources.globalDescriptorSet);
                data.lightSet = builder.Use(renderResources.lightDescriptorSet);
                data.materialSet = builder.Use(renderResources.materialDescriptorSet);
                data.resolveSet = builder.Use(_resolveSet);
                return true;
            },
            [this, &view, frameIndex](Data& data, Renderer::RenderGraphResources& graphResources,
                                     Renderer::CommandList& commandList) {
                GPU_SCOPED_PROFILER_ZONE(commandList, MaterialResolve);
                const uvec2 renderSize = view.GetDimensions();
                struct Constants
                {
                    vec4 renderInfo;
                    vec4 shadowSettings;
                    vec4 fogColor;
                    vec4 fogSettings;
                    u32 resourceIndex;
                    u32 viewIndex;
                    u32 debugMode;
                    u32 numDirectionalLights;
                    u32 shadowsReady;
                    u32 sampleAmbientVisibility;
                };
                data.resolveSet.Bind("_visibilityBuffer"_h, data.visibility);
                data.resolveSet.Bind("_materialColor"_h, data.color);
                data.resolveSet.Bind("_ambientVisibility"_h, data.ambientVisibility);
                CVarSystem* cvarSystem = CVarSystem::Get();
                const f32 shadowStrength = static_cast<f32>(*cvarSystem->GetFloatCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowStrength"));
                ShadowRenderer* shadowRenderer = _gameRenderer->GetShadowRenderer();
                const bool shadowsReady = view.UsesWorldShadows() && static_cast<u32>(*cvarSystem->GetIntCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowEnabled")) != 0u && shadowStrength > 0.0f && shadowRenderer->GetSVSMPagePool() != Renderer::ImageID::Invalid();
                const f32 shadowNormalOffsetBias = static_cast<f32>(*cvarSystem->GetFloatCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowNormalOffsetBias"));
                const f32 svsmConstantBias = static_cast<f32>(*cvarSystem->GetFloatCVar(CVarCategory::Client | CVarCategory::Rendering, "svsmConstantBias"));
                commandList.BeginPipeline(_resolvePipeline);
                commandList.BindDescriptorSet(data.globalSet, frameIndex);
                commandList.BindDescriptorSet(data.lightSet, frameIndex);
                commandList.BindDescriptorSet(data.materialSet, frameIndex);
                commandList.BindDescriptorSet(data.resolveSet, frameIndex);
                Constants* constants = graphResources.FrameNew<Constants>();
                constants->renderInfo = vec4(renderSize, 1.0f / vec2(renderSize));
                constants->shadowSettings = vec4(shadowStrength, shadowNormalOffsetBias, svsmConstantBias, 0.0f);
                constants->fogColor = _gameRenderer->GetMaterialRenderer()->GetFogColor();
                constants->fogSettings = _gameRenderer->GetMaterialRenderer()->GetFogSettings();
                constants->resourceIndex = frameIndex;
                constants->viewIndex = view.GetCameraIndex();
                constants->debugMode = static_cast<u32>(glm::clamp(CVAR_MaterialDebugMode.Get(), 0, 5));
                constants->numDirectionalLights = _gameRenderer->GetMaterialRenderer()->GetNumDirectionalLights();
                constants->shadowsReady = shadowsReady ? 1u : 0u;
                constants->sampleAmbientVisibility = 0u;
                commandList.PushConstant(constants, 0, sizeof(Constants));
                commandList.Dispatch((renderSize.x + 7u) / 8u, (renderSize.y + 7u) / 8u, 1u);
                commandList.EndPipeline(_resolvePipeline);
            });

    }

    void MaterialResolvePass::AddRetainedOutputPass(Renderer::RenderGraph* renderGraph,
                                                     const RenderScenes::RenderView& view)
    {
        if (view.GetRetainedOutput() == Renderer::TextureID::Invalid())
            return;
        struct CopyData { Renderer::ImageResource color; };
        renderGraph->AddPass<CopyData>("Retain: " + view.GetDebugName(),
            [&view](CopyData& data, Renderer::RenderGraphBuilder& builder) {
                data.color = builder.Read(view.GetColorTarget(), Renderer::PipelineType::COMPUTE);
                return true;
            },
            [&view](CopyData& data, Renderer::RenderGraphResources&, Renderer::CommandList& commandList) {
                commandList.ImageBarrier(data.color);
                commandList.CopyImageToTexture(view.GetRetainedOutput(), view.GetColorTarget(), view.GetDimensions());
            }, Renderer::RenderPassFlags::SideEffect);
    }
} // namespace MaterialRendering
