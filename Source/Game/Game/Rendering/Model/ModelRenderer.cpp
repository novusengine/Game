#include "ModelRenderer.h"

#include "Game/Rendering/CullUtils.h"
#include "Game/Rendering/RenderUtils.h"
#include "Game/Rendering/GameRenderer.h"
#include "Game/Rendering/RenderResources.h"
#include "Game/Rendering/Debug/DebugRenderer.h"
#include "Game/Rendering/Model/ModelLoader.h"
#include "Game/Util/ServiceLocator.h"
#include "Game/Application/EnttRegistries.h"
#include "Game/ECS/Components/Model.h"
#include "Game/ECS/Components/Tags.h"
#include "Game/ECS/Singletons/TextureSingleton.h"
#include "Game/ECS/Util/Transforms.h"

#include <Base/CVarSystem/CVarSystem.h>

#include <FileFormat/Novus/Map/MapChunk.h>

#include <Input/InputManager.h>

#include <Renderer/Renderer.h>
#include <Renderer/RenderGraph.h>
#include <Renderer/Descriptors/ImageDesc.h>

#include <imgui/imgui.h>
#include <entt/entt.hpp>
#include <glm/gtx/euler_angles.hpp>

AutoCVar_Int CVAR_ModelRendererEnabled("modelRenderer.enabled", "enable modelrendering", 1, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_ModelCullingEnabled("modelRenderer.culling", "enable model culling", 1, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_ModelOcclusionCullingEnabled("modelRenderer.culling.occlusion", "enable model occlusion culling", 1, CVarFlags::EditCheckbox);

AutoCVar_Int CVAR_ModelDisableTwoStepCulling("modelRenderer.debug.disableTwoStepCulling", "disable two step culling and force all drawcalls into the geometry pass", 0, CVarFlags::EditCheckbox);

AutoCVar_Int CVAR_ModelDrawOccluders("modelRenderer.debug.drawOccluders", "enable the draw command for occluders, the culling and everything else is unaffected", 1, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_ModelDrawGeometry("modelRenderer.debug.drawGeometry", "enable the draw command for geometry, the culling and everything else is unaffected", 1, CVarFlags::EditCheckbox);

AutoCVar_Int CVAR_ModelDrawOpaqueAABBs("modelRenderer.debug.drawOpaqueAABBs", "if enabled, the culling pass will debug draw all opaque AABBs", 0, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_ModelDrawTransparentAABBs("modelRenderer.debug.drawTransparentAABBs", "if enabled, the culling pass will debug draw all transparent AABBs", 0, CVarFlags::EditCheckbox);

AutoCVar_Int CVAR_ModelValidateTransfers("validation.GPUVectors.modelRenderer", "if enabled ON START we will validate GPUVector uploads", 0, CVarFlags::EditCheckbox);

ModelRenderer::ModelRenderer(Renderer::Renderer* renderer, DebugRenderer* debugRenderer)
    : CulledRenderer(renderer, debugRenderer)
    , _renderer(renderer)
    , _debugRenderer(debugRenderer)
{
    CreatePermanentResources();

    if (CVAR_ModelValidateTransfers.Get())
    {
        _vertices.SetValidation(true);
        _indices.SetValidation(true);
        _instanceDatas.SetValidation(true);
        _instanceMatrices.SetValidation(true);
        _textureUnits.SetValidation(true);
        _boneMatrices.SetValidation(true);

        _cullingDatas.SetValidation(true);

        _opaqueCullingResources.SetValidation(true);
        _transparentCullingResources.SetValidation(true);

        _opaqueSkyboxCullingResources.SetValidation(true);
        _transparentSkyboxCullingResources.SetValidation(true);
    }
}

ModelRenderer::~ModelRenderer()
{

}

void ModelRenderer::Update(f32 deltaTime)
{
    ZoneScoped;

    if (!CVAR_ModelRendererEnabled.Get())
        return;

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;

    registry->view<ECS::Components::Transform, ECS::Components::Model, ECS::Components::DirtyTransform>().each([&](entt::entity entity, ECS::Components::Transform& transform, ECS::Components::Model& model, ECS::Components::DirtyTransform& dirtyTransform)
    {
        u32 instanceID = model.instanceID;
        if (instanceID == std::numeric_limits<u32>::max())
        {
            return;
        }

        std::vector<mat4x4>& instanceMatrices = _instanceMatrices.Get();
        mat4x4& matrix = instanceMatrices[instanceID];

        matrix = transform.GetMatrix();
        _instanceMatrices.SetDirtyElement(instanceID);
    });

    const bool cullingEnabled = CVAR_ModelCullingEnabled.Get();
    _opaqueCullingResources.Update(deltaTime, cullingEnabled);
    _transparentCullingResources.Update(deltaTime, cullingEnabled);

    _opaqueSkyboxCullingResources.Update(deltaTime, false);
    _transparentSkyboxCullingResources.Update(deltaTime, false);

    SyncToGPU();
}

void ModelRenderer::Clear()
{
    ZoneScoped;

    _modelManifests.clear();
    _modelManifestsIndex.store(0);

    _modelIDToNumInstances.clear();

    _cullingDatas.Clear();

    _vertices.Clear();
    _verticesIndex.store(0);

    _indices.Clear();
    _indicesIndex.store(0);

    _instanceDatas.Clear();
    _instanceMatrices.Clear();
    _instanceIndex.store(0);

    _textureUnits.Clear();
    _textureUnitIndex.store(0);

    _boneMatrices.Clear();
    _boneMatrixIndex.store(0);

    _animatedVertices.Clear(false);
    _animatedVerticesIndex.store(0);

    _modelDecorationSets.clear();
    _modelDecorationSetsIndex.store(0);

    _modelDecorations.clear();
    _modelDecorationsIndex.store(0);

    _opaqueCullingResources.Clear();
    _transparentCullingResources.Clear();

    _opaqueSkyboxCullingResources.Clear();
    _transparentSkyboxCullingResources.Clear();

    _renderer->UnloadTexturesInArray(_textures, 1);

    SyncToGPU();
}

void ModelRenderer::AddOccluderPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    ZoneScoped;

    if (!CVAR_ModelRendererEnabled.Get())
        return;

    if (!CVAR_ModelCullingEnabled.Get())
        return;

    if (_opaqueCullingResources.GetDrawCalls().Size() == 0)
        return;

    u32 numCascades = 0;// *CVarSystem::Get()->GetIntCVar("shadows.cascade.num");

    struct Data
    {
        Renderer::ImageMutableResource visibilityBuffer;
        Renderer::DepthImageMutableResource depth;

        Renderer::BufferMutableResource culledDrawCallsBuffer;
        Renderer::BufferMutableResource culledDrawCallsBitMaskBuffer;
        Renderer::BufferMutableResource drawCountBuffer;
        Renderer::BufferMutableResource triangleCountBuffer;
        Renderer::BufferMutableResource drawCountReadBackBuffer;
        Renderer::BufferMutableResource triangleCountReadBackBuffer;

        Renderer::DescriptorSetResource globalSet;
        Renderer::DescriptorSetResource occluderFillSet;
        Renderer::DescriptorSetResource createIndirectDescriptorSet;
        Renderer::DescriptorSetResource drawSet;
    };

    renderGraph->AddPass<Data>("Model (O) Occluders",
        [this, &resources, frameIndex](Data& data, Renderer::RenderGraphBuilder& builder)
        {
            using BufferUsage = Renderer::BufferPassUsage;

            data.visibilityBuffer = builder.Write(resources.visibilityBuffer, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);
            data.depth = builder.Write(resources.depth, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);

            builder.Read(resources.cameras.GetBuffer(), BufferUsage::GRAPHICS | BufferUsage::COMPUTE);
            builder.Read(_vertices.GetBuffer(), BufferUsage::GRAPHICS);
            builder.Read(_indices.GetBuffer(), BufferUsage::GRAPHICS);
            builder.Read(_textureUnits.GetBuffer(), BufferUsage::GRAPHICS);
            builder.Read(_instanceDatas.GetBuffer(), BufferUsage::GRAPHICS);
            builder.Read(_instanceMatrices.GetBuffer(), BufferUsage::GRAPHICS);
            builder.Read(_boneMatrices.GetBuffer(), BufferUsage::GRAPHICS | BufferUsage::COMPUTE);
            builder.Read(_opaqueCullingResources.GetDrawCallDatas().GetBuffer(), BufferUsage::GRAPHICS);

            OccluderPassSetup(data, builder, &_opaqueCullingResources, frameIndex);

            builder.Write(_animatedVertices.GetBuffer(), BufferUsage::GRAPHICS | BufferUsage::COMPUTE);

            data.globalSet = builder.Use(resources.globalDescriptorSet);

            return true; // Return true from setup to enable this pass, return false to disable it
        },
        [this, &resources, frameIndex](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, ModelOccluders);

            CulledRenderer::OccluderPassParams params;
            params.passName = "Opaque";
            params.graphResources = &graphResources;
            params.commandList = &commandList;
            params.cullingResources = &_opaqueCullingResources;

            params.frameIndex = frameIndex;
            params.rt0 = data.visibilityBuffer;
            params.depth = data.depth;

            params.culledDrawCallsBuffer = data.culledDrawCallsBuffer;
            params.culledDrawCallsBitMaskBuffer = data.culledDrawCallsBitMaskBuffer;
            params.drawCountBuffer = data.drawCountBuffer;
            params.triangleCountBuffer = data.triangleCountBuffer;
            params.drawCountReadBackBuffer = data.drawCountReadBackBuffer;
            params.triangleCountReadBackBuffer = data.triangleCountReadBackBuffer;

            params.globalDescriptorSet = data.globalSet;
            params.occluderFillDescriptorSet = data.occluderFillSet;
            params.drawDescriptorSet = data.drawSet;

            params.drawCallback = [&](const DrawParams& drawParams)
            {
                Draw(resources, frameIndex, graphResources, commandList, drawParams);
            };

            params.enableDrawing = CVAR_ModelDrawOccluders.Get();
            params.disableTwoStepCulling = CVAR_ModelDisableTwoStepCulling.Get();

            OccluderPass(params);
        });
}

void ModelRenderer::AddCullingPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    ZoneScoped;

    if (!CVAR_ModelRendererEnabled.Get())
        return;

    if (!CVAR_ModelCullingEnabled.Get())
        return;

    if (_opaqueCullingResources.GetDrawCalls().Size() == 0)
        return;

    u32 numCascades = 0;// *CVarSystem::Get()->GetIntCVar("shadows.cascade.num");

    struct Data
    {
        Renderer::ImageResource depthPyramid;

        Renderer::BufferResource prevCulledDrawCallsBitMask;

        Renderer::BufferMutableResource currentCulledDrawCallsBitMask;
        Renderer::BufferMutableResource culledDrawCallsBuffer;
        Renderer::BufferMutableResource drawCountBuffer;
        Renderer::BufferMutableResource triangleCountBuffer;
        Renderer::BufferMutableResource drawCountReadBackBuffer;
        Renderer::BufferMutableResource triangleCountReadBackBuffer;

        Renderer::DescriptorSetResource debugSet;
        Renderer::DescriptorSetResource globalSet;
        Renderer::DescriptorSetResource cullingSet;
    };

    renderGraph->AddPass<Data>("Model (O) Culling",
        [this, &resources, frameIndex](Data& data, Renderer::RenderGraphBuilder& builder) // Setup
        {
            using BufferUsage = Renderer::BufferPassUsage;

            data.depthPyramid = builder.Read(resources.depthPyramid, Renderer::PipelineType::COMPUTE);

            builder.Read(resources.cameras.GetBuffer(), BufferUsage::COMPUTE);
            builder.Read(_cullingDatas.GetBuffer(), BufferUsage::COMPUTE);
            builder.Read(_instanceMatrices.GetBuffer(), BufferUsage::COMPUTE);

            CullingPassSetup(data, builder, &_opaqueCullingResources, frameIndex);
            builder.Read(_opaqueCullingResources.GetDrawCallDatas().GetBuffer(), BufferUsage::COMPUTE);

            data.debugSet = builder.Use(_debugRenderer->GetDebugDescriptorSet());
            data.globalSet = builder.Use(resources.globalDescriptorSet);

            _debugRenderer->RegisterCullingPassBufferUsage(builder);

            return true; // Return true from setup to enable this pass, return false to disable it
        },
        [this, frameIndex](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, ModelCulling);

            CulledRenderer::CullingPassParams params;
            params.passName = "Opaque";
            params.graphResources = &graphResources;
            params.commandList = &commandList;
            params.cullingResources = &_opaqueCullingResources;
            params.frameIndex = frameIndex;

            params.depthPyramid = data.depthPyramid;

            params.prevCulledDrawCallsBitMask = data.prevCulledDrawCallsBitMask;

            params.currentCulledDrawCallsBitMask = data.currentCulledDrawCallsBitMask;
            params.culledDrawCallsBuffer = data.culledDrawCallsBuffer;
            params.drawCountBuffer = data.drawCountBuffer;
            params.triangleCountBuffer = data.triangleCountBuffer;
            params.drawCountReadBackBuffer = data.drawCountReadBackBuffer;
            params.triangleCountReadBackBuffer = data.triangleCountReadBackBuffer;

            params.debugDescriptorSet = data.debugSet;
            params.globalDescriptorSet = data.globalSet;
            params.cullingDescriptorSet = data.cullingSet;

            params.numCascades = 0;// *CVarSystem::Get()->GetIntCVar("shadows.cascade.num");
            params.occlusionCull = CVAR_ModelOcclusionCullingEnabled.Get();

            params.modelIDIsDrawCallID = false;
            params.cullingDataIsWorldspace = false;
            params.debugDrawColliders = CVAR_ModelDrawOpaqueAABBs.Get();

            params.instanceIDOffset = offsetof(DrawCallData, instanceID);
            params.modelIDOffset = offsetof(DrawCallData, modelID);
            params.drawCallDataSize = sizeof(DrawCallData);

            CullingPass(params);
        });
}

void ModelRenderer::AddGeometryPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    ZoneScoped;

    if (!CVAR_ModelRendererEnabled.Get())
        return;

    if (_opaqueCullingResources.GetDrawCalls().Size() == 0)
        return;

    const bool cullingEnabled = CVAR_ModelCullingEnabled.Get();

    struct Data
    {
        Renderer::ImageMutableResource visibilityBuffer;
        Renderer::DepthImageMutableResource depth;

        Renderer::BufferMutableResource culledDrawCallsBuffer;
        Renderer::BufferMutableResource drawCountBuffer;
        Renderer::BufferMutableResource triangleCountBuffer;
        Renderer::BufferMutableResource drawCountReadBackBuffer;
        Renderer::BufferMutableResource triangleCountReadBackBuffer;

        Renderer::DescriptorSetResource globalSet;
        Renderer::DescriptorSetResource drawSet;
    };

    renderGraph->AddPass<Data>("Model (O) Geometry",
        [this, &resources, frameIndex](Data& data, Renderer::RenderGraphBuilder& builder)
        {
            using BufferUsage = Renderer::BufferPassUsage;

            data.visibilityBuffer = builder.Write(resources.visibilityBuffer, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);
            data.depth = builder.Write(resources.depth, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);

            builder.Read(resources.cameras.GetBuffer(), BufferUsage::GRAPHICS);
            builder.Read(_vertices.GetBuffer(), BufferUsage::GRAPHICS);
            builder.Read(_indices.GetBuffer(), BufferUsage::GRAPHICS);
            builder.Read(_textureUnits.GetBuffer(), BufferUsage::GRAPHICS);
            builder.Read(_instanceDatas.GetBuffer(), BufferUsage::GRAPHICS);
            builder.Read(_instanceMatrices.GetBuffer(), BufferUsage::GRAPHICS);
            builder.Read(_boneMatrices.GetBuffer(), BufferUsage::GRAPHICS | BufferUsage::COMPUTE);
            
            builder.Write(_animatedVertices.GetBuffer(), BufferUsage::GRAPHICS | BufferUsage::COMPUTE);

            GeometryPassSetup(data, builder, &_opaqueCullingResources, frameIndex);
            builder.Read(_opaqueCullingResources.GetDrawCallDatas().GetBuffer(), BufferUsage::GRAPHICS);

            data.globalSet = builder.Use(resources.globalDescriptorSet);

            return true; // Return true from setup to enable this pass, return false to disable it
        },
        [this, &resources, frameIndex, cullingEnabled](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList)
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, ModelGeometry);

            CulledRenderer::GeometryPassParams params;
            params.passName = "Opaque";
            params.graphResources = &graphResources;
            params.commandList = &commandList;
            params.cullingResources = &_opaqueCullingResources;

            params.frameIndex = frameIndex;
            params.rt0 = data.visibilityBuffer;
            params.depth = data.depth;

            params.culledDrawCallsBuffer = data.culledDrawCallsBuffer;
            params.drawCountBuffer = data.drawCountBuffer;
            params.triangleCountBuffer = data.triangleCountBuffer;
            params.drawCountReadBackBuffer = data.drawCountReadBackBuffer;
            params.triangleCountReadBackBuffer = data.triangleCountReadBackBuffer;

            params.globalDescriptorSet = data.globalSet;
            params.drawDescriptorSet = data.drawSet;

            params.drawCallback = [&](const DrawParams& drawParams)
            {
                Draw(resources, frameIndex, graphResources, commandList, drawParams);
            };

            params.enableDrawing = CVAR_ModelDrawGeometry.Get();
            params.cullingEnabled = cullingEnabled;
            params.numCascades = 0;// *CVarSystem::Get()->GetIntCVar("shadows.cascade.num");

            GeometryPass(params);
        });
}

void ModelRenderer::AddTransparencyCullingPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    ZoneScoped;

    if (!CVAR_ModelRendererEnabled.Get())
        return;

    if (!CVAR_ModelCullingEnabled.Get())
        return;

    if (_transparentCullingResources.GetDrawCalls().Size() == 0)
        return;

    u32 numCascades = 0;// *CVarSystem::Get()->GetIntCVar("shadows.cascade.num");

    struct Data
    {
        Renderer::ImageResource depthPyramid;

        Renderer::BufferMutableResource culledDrawCallsBuffer;
        Renderer::BufferMutableResource drawCountBuffer;
        Renderer::BufferMutableResource triangleCountBuffer;
        Renderer::BufferMutableResource drawCountReadBackBuffer;
        Renderer::BufferMutableResource triangleCountReadBackBuffer;

        Renderer::DescriptorSetResource debugSet;
        Renderer::DescriptorSetResource globalSet;
        Renderer::DescriptorSetResource cullingSet;
    };

    renderGraph->AddPass<Data>("Model (T) Culling",
        [this, &resources](Data& data, Renderer::RenderGraphBuilder& builder) // Setup
        {
            ZoneScoped;
            using BufferUsage = Renderer::BufferPassUsage;

            data.depthPyramid = builder.Read(resources.depthPyramid, Renderer::PipelineType::COMPUTE);

            builder.Read(resources.cameras.GetBuffer(), BufferUsage::COMPUTE);
            builder.Read(_cullingDatas.GetBuffer(), BufferUsage::COMPUTE);
            builder.Read(_instanceMatrices.GetBuffer(), BufferUsage::COMPUTE);
            builder.Read(_transparentCullingResources.GetDrawCalls().GetBuffer(), BufferUsage::COMPUTE);
            builder.Read(_transparentCullingResources.GetDrawCallDatas().GetBuffer(), BufferUsage::COMPUTE);

            data.culledDrawCallsBuffer = builder.Write(_transparentCullingResources.GetCulledDrawsBuffer(0), BufferUsage::COMPUTE);
            data.drawCountBuffer = builder.Write(_transparentCullingResources.GetDrawCountBuffer(), BufferUsage::TRANSFER | BufferUsage::COMPUTE);
            data.triangleCountBuffer = builder.Write(_transparentCullingResources.GetTriangleCountBuffer(), BufferUsage::TRANSFER | BufferUsage::COMPUTE);
            data.drawCountReadBackBuffer = builder.Write(_transparentCullingResources.GetDrawCountReadBackBuffer(), BufferUsage::TRANSFER);
            data.triangleCountReadBackBuffer = builder.Write(_transparentCullingResources.GetTriangleCountReadBackBuffer(), BufferUsage::TRANSFER);

            data.debugSet = builder.Use(_debugRenderer->GetDebugDescriptorSet());
            data.globalSet = builder.Use(resources.globalDescriptorSet);
            data.cullingSet = builder.Use(_transparentCullingResources.GetCullingDescriptorSet());

            _debugRenderer->RegisterCullingPassBufferUsage(builder);

            return true; // Return true from setup to enable this pass, return false to disable it
        },
        [this, frameIndex](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, ModelCulling);

            CulledRenderer::CullingPassParams params;
            params.passName = "Transparent";
            params.graphResources = &graphResources;
            params.commandList = &commandList;
            params.cullingResources = &_transparentCullingResources;
            params.frameIndex = frameIndex;

            params.depthPyramid = data.depthPyramid;

            params.culledDrawCallsBuffer = data.culledDrawCallsBuffer;
            params.drawCountBuffer = data.drawCountBuffer;
            params.triangleCountBuffer = data.triangleCountBuffer;
            params.drawCountReadBackBuffer = data.drawCountReadBackBuffer;
            params.triangleCountReadBackBuffer = data.triangleCountReadBackBuffer;

            params.debugDescriptorSet = data.debugSet;
            params.globalDescriptorSet = data.globalSet;
            params.cullingDescriptorSet = data.cullingSet;

            params.numCascades = 0;// *CVarSystem::Get()->GetIntCVar("shadows.cascade.num");
            params.occlusionCull = CVAR_ModelOcclusionCullingEnabled.Get();
            params.disableTwoStepCulling = true; // Transparent objects don't write depth, so we don't need to two step cull them

            params.modelIDIsDrawCallID = false;
            params.cullingDataIsWorldspace = false;
            params.debugDrawColliders = CVAR_ModelDrawTransparentAABBs.Get();

            params.instanceIDOffset = offsetof(DrawCallData, instanceID);
            params.modelIDOffset = offsetof(DrawCallData, modelID);
            params.drawCallDataSize = sizeof(DrawCallData);

            CullingPass(params);
        });
}

void ModelRenderer::AddTransparencyGeometryPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    ZoneScoped;

    if (!CVAR_ModelRendererEnabled.Get())
        return;

    if (_transparentCullingResources.GetDrawCalls().Size() == 0)
        return;

    const bool cullingEnabled = CVAR_ModelCullingEnabled.Get();

    struct Data
    {
        Renderer::ImageMutableResource transparency;
        Renderer::ImageMutableResource transparencyWeights;
        Renderer::DepthImageMutableResource depth;

        Renderer::BufferMutableResource culledDrawCallsBuffer;
        Renderer::BufferMutableResource drawCountBuffer;
        Renderer::BufferMutableResource triangleCountBuffer;
        Renderer::BufferMutableResource drawCountReadBackBuffer;
        Renderer::BufferMutableResource triangleCountReadBackBuffer;

        Renderer::DescriptorSetResource globalSet;
        Renderer::DescriptorSetResource drawSet;
    };

    renderGraph->AddPass<Data>("Model (T) Geometry",
        [this, &resources](Data& data, Renderer::RenderGraphBuilder& builder)
        {
            using BufferUsage = Renderer::BufferPassUsage;

            data.transparency = builder.Write(resources.transparency, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);
            data.transparencyWeights = builder.Write(resources.transparencyWeights, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);
            data.depth = builder.Write(resources.depth, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);

            builder.Read(resources.cameras.GetBuffer(), BufferUsage::GRAPHICS);
            builder.Read(_vertices.GetBuffer(), BufferUsage::GRAPHICS);
            builder.Read(_indices.GetBuffer(), BufferUsage::GRAPHICS);
            builder.Read(_textureUnits.GetBuffer(), BufferUsage::GRAPHICS);
            builder.Read(_instanceDatas.GetBuffer(), BufferUsage::GRAPHICS);
            builder.Read(_instanceMatrices.GetBuffer(), BufferUsage::GRAPHICS);
            builder.Read(_boneMatrices.GetBuffer(), BufferUsage::GRAPHICS | BufferUsage::COMPUTE);
            builder.Read(_transparentCullingResources.GetDrawCalls().GetBuffer(), BufferUsage::GRAPHICS);
            builder.Read(_transparentCullingResources.GetDrawCallDatas().GetBuffer(), BufferUsage::GRAPHICS);

            builder.Write(_animatedVertices.GetBuffer(), BufferUsage::GRAPHICS | BufferUsage::COMPUTE);

            data.culledDrawCallsBuffer = builder.Write(_transparentCullingResources.GetCulledDrawsBuffer(0), BufferUsage::GRAPHICS);
            data.drawCountBuffer = builder.Write(_transparentCullingResources.GetDrawCountBuffer(), BufferUsage::TRANSFER | BufferUsage::GRAPHICS);
            data.triangleCountBuffer = builder.Write(_transparentCullingResources.GetTriangleCountBuffer(), BufferUsage::TRANSFER | BufferUsage::GRAPHICS);
            data.drawCountReadBackBuffer = builder.Write(_transparentCullingResources.GetDrawCountReadBackBuffer(), BufferUsage::TRANSFER);
            data.triangleCountReadBackBuffer = builder.Write(_transparentCullingResources.GetTriangleCountReadBackBuffer(), BufferUsage::TRANSFER);

            data.globalSet = builder.Use(resources.globalDescriptorSet);
            data.drawSet = builder.Use(_transparentCullingResources.GetGeometryPassDescriptorSet());

            return true; // Return true from setup to enable this pass, return false to disable it
        },
        [this, &resources, frameIndex, cullingEnabled](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList)
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, ModelGeometry);

            CulledRenderer::GeometryPassParams params;
            params.passName = "Transparent";
            params.graphResources = &graphResources;
            params.commandList = &commandList;
            params.cullingResources = &_transparentCullingResources;

            params.frameIndex = frameIndex;
            params.rt0 = data.transparency;
            params.rt1 = data.transparencyWeights;
            params.depth = data.depth;

            params.culledDrawCallsBuffer = data.culledDrawCallsBuffer;
            params.drawCountBuffer = data.drawCountBuffer;
            params.triangleCountBuffer = data.triangleCountBuffer;
            params.drawCountReadBackBuffer = data.drawCountReadBackBuffer;
            params.triangleCountReadBackBuffer = data.triangleCountReadBackBuffer;

            params.globalDescriptorSet = data.globalSet;
            params.drawDescriptorSet = data.drawSet;

            params.drawCallback = [&](const DrawParams& drawParams)
            {
                DrawTransparent(resources, frameIndex, graphResources, commandList, drawParams);
            };

            params.enableDrawing = CVAR_ModelDrawGeometry.Get();
            params.cullingEnabled = cullingEnabled;
            params.numCascades = 0;// *CVarSystem::Get()->GetIntCVar("shadows.cascade.num");

            GeometryPass(params);
        });
}

void ModelRenderer::AddSkyboxPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    ZoneScoped;

    if (!CVAR_ModelRendererEnabled.Get())
        return;

    if (_opaqueSkyboxCullingResources.GetDrawCalls().Size() > 0)
    {
        struct Data
        {
            Renderer::ImageMutableResource color;
            Renderer::DepthImageMutableResource depth;

            Renderer::BufferMutableResource drawCallsBuffer;
            Renderer::BufferMutableResource culledDrawCallsBuffer;
            Renderer::BufferMutableResource drawCountBuffer;
            Renderer::BufferMutableResource triangleCountBuffer;
            Renderer::BufferMutableResource drawCountReadBackBuffer;
            Renderer::BufferMutableResource triangleCountReadBackBuffer;

            Renderer::DescriptorSetResource globalSet;
            Renderer::DescriptorSetResource drawSet;
        };

        renderGraph->AddPass<Data>("Skybox Models",
            [this, &resources](Data& data, Renderer::RenderGraphBuilder& builder)
            {
                using BufferUsage = Renderer::BufferPassUsage;

                data.color = builder.Write(resources.skyboxColor, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);
                data.depth = builder.Write(resources.skyboxDepth, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);

                builder.Read(resources.cameras.GetBuffer(), BufferUsage::GRAPHICS);
                builder.Read(_vertices.GetBuffer(), BufferUsage::GRAPHICS);
                builder.Read(_indices.GetBuffer(), BufferUsage::GRAPHICS);
                builder.Read(_textureUnits.GetBuffer(), BufferUsage::GRAPHICS);
                builder.Read(_instanceDatas.GetBuffer(), BufferUsage::GRAPHICS);
                builder.Read(_instanceMatrices.GetBuffer(), BufferUsage::GRAPHICS);
                builder.Read(_boneMatrices.GetBuffer(), BufferUsage::GRAPHICS);
                builder.Read(_opaqueSkyboxCullingResources.GetDrawCallDatas().GetBuffer(), BufferUsage::GRAPHICS);

                builder.Write(_animatedVertices.GetBuffer(), BufferUsage::GRAPHICS);

                data.drawCallsBuffer = builder.Write(_opaqueSkyboxCullingResources.GetDrawCalls().GetBuffer(), BufferUsage::GRAPHICS);
                data.culledDrawCallsBuffer = builder.Write(_opaqueSkyboxCullingResources.GetCulledDrawsBuffer(0), BufferUsage::GRAPHICS);
                data.drawCountBuffer = builder.Write(_opaqueSkyboxCullingResources.GetDrawCountBuffer(), BufferUsage::TRANSFER | BufferUsage::GRAPHICS);
                data.triangleCountBuffer = builder.Write(_opaqueSkyboxCullingResources.GetTriangleCountBuffer(), BufferUsage::TRANSFER | BufferUsage::GRAPHICS);
                data.drawCountReadBackBuffer = builder.Write(_opaqueSkyboxCullingResources.GetDrawCountReadBackBuffer(), BufferUsage::TRANSFER);
                data.triangleCountReadBackBuffer = builder.Write(_opaqueSkyboxCullingResources.GetTriangleCountReadBackBuffer(), BufferUsage::TRANSFER);

                data.globalSet = builder.Use(resources.globalDescriptorSet);
                data.drawSet = builder.Use(_opaqueSkyboxCullingResources.GetGeometryPassDescriptorSet());

                return true; // Return true from setup to enable this pass, return false to disable it
            },
            [this, &resources, frameIndex](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList)
            {
                GPU_SCOPED_PROFILER_ZONE(commandList, SkyboxModels);

                CulledRenderer::GeometryPassParams params;
                params.passName = "Skybox Models";
                params.graphResources = &graphResources;
                params.commandList = &commandList;
                params.cullingResources = &_opaqueSkyboxCullingResources;

                params.frameIndex = frameIndex;
                params.rt0 = data.color;
                params.depth = data.depth;

                params.drawCallsBuffer = data.drawCallsBuffer;
                params.culledDrawCallsBuffer = data.culledDrawCallsBuffer;
                params.drawCountBuffer = data.drawCountBuffer;
                params.triangleCountBuffer = data.triangleCountBuffer;
                params.drawCountReadBackBuffer = data.drawCountReadBackBuffer;
                params.triangleCountReadBackBuffer = data.triangleCountReadBackBuffer;

                params.globalDescriptorSet = data.globalSet;
                params.drawDescriptorSet = data.drawSet;

                params.drawCallback = [&](const DrawParams& drawParams)
                {
                    DrawSkybox(resources, frameIndex, graphResources, commandList, drawParams, false);
                };

                params.enableDrawing = CVAR_ModelDrawGeometry.Get();
                params.cullingEnabled = false;
                params.numCascades = 0;// *CVarSystem::Get()->GetIntCVar("shadows.cascade.num");

                GeometryPass(params);
            });
    }

    if (_transparentSkyboxCullingResources.GetDrawCalls().Size() > 0)
    {
        struct Data
        {
            Renderer::ImageMutableResource color;
            Renderer::DepthImageMutableResource depth;

            Renderer::BufferMutableResource drawCallsBuffer;
            Renderer::BufferMutableResource culledDrawCallsBuffer;
            Renderer::BufferMutableResource drawCountBuffer;
            Renderer::BufferMutableResource triangleCountBuffer;
            Renderer::BufferMutableResource drawCountReadBackBuffer;
            Renderer::BufferMutableResource triangleCountReadBackBuffer;

            Renderer::DescriptorSetResource globalSet;
            Renderer::DescriptorSetResource drawSet;
        };

        renderGraph->AddPass<Data>("Skybox Models (T)",
            [this, &resources](Data& data, Renderer::RenderGraphBuilder& builder)
            {
                using BufferUsage = Renderer::BufferPassUsage;

                data.color = builder.Write(resources.skyboxColor, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);
                data.depth = builder.Write(resources.skyboxDepth, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);

                builder.Read(resources.cameras.GetBuffer(), BufferUsage::GRAPHICS);
                builder.Read(_vertices.GetBuffer(), BufferUsage::GRAPHICS);
                builder.Read(_indices.GetBuffer(), BufferUsage::GRAPHICS);
                builder.Read(_textureUnits.GetBuffer(), BufferUsage::GRAPHICS);
                builder.Read(_instanceDatas.GetBuffer(), BufferUsage::GRAPHICS);
                builder.Read(_instanceMatrices.GetBuffer(), BufferUsage::GRAPHICS);
                builder.Read(_boneMatrices.GetBuffer(), BufferUsage::GRAPHICS);
                builder.Read(_transparentSkyboxCullingResources.GetDrawCallDatas().GetBuffer(), BufferUsage::GRAPHICS);

                builder.Write(_animatedVertices.GetBuffer(), BufferUsage::GRAPHICS);

                data.drawCallsBuffer = builder.Write(_transparentSkyboxCullingResources.GetDrawCalls().GetBuffer(), BufferUsage::GRAPHICS);
                data.culledDrawCallsBuffer = builder.Write(_transparentSkyboxCullingResources.GetCulledDrawsBuffer(0), BufferUsage::GRAPHICS);
                data.drawCountBuffer = builder.Write(_transparentSkyboxCullingResources.GetDrawCountBuffer(), BufferUsage::TRANSFER | BufferUsage::GRAPHICS);
                data.triangleCountBuffer = builder.Write(_transparentSkyboxCullingResources.GetTriangleCountBuffer(), BufferUsage::TRANSFER | BufferUsage::GRAPHICS);
                data.drawCountReadBackBuffer = builder.Write(_transparentSkyboxCullingResources.GetDrawCountReadBackBuffer(), BufferUsage::TRANSFER);
                data.triangleCountReadBackBuffer = builder.Write(_transparentSkyboxCullingResources.GetTriangleCountReadBackBuffer(), BufferUsage::TRANSFER);

                data.globalSet = builder.Use(resources.globalDescriptorSet);
                data.drawSet = builder.Use(_transparentSkyboxCullingResources.GetGeometryPassDescriptorSet());

                return true; // Return true from setup to enable this pass, return false to disable it
            },
            [this, &resources, frameIndex](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList)
            {
                GPU_SCOPED_PROFILER_ZONE(commandList, SkyboxTransparency);

                CulledRenderer::GeometryPassParams params;
                params.passName = "Skybox Models (T)";
                params.graphResources = &graphResources;
                params.commandList = &commandList;
                params.cullingResources = &_transparentSkyboxCullingResources;

                params.frameIndex = frameIndex;
                params.rt0 = data.color;
                params.depth = data.depth;

                params.drawCallsBuffer = data.drawCallsBuffer;
                params.culledDrawCallsBuffer = data.culledDrawCallsBuffer;
                params.drawCountBuffer = data.drawCountBuffer;
                params.triangleCountBuffer = data.triangleCountBuffer;
                params.drawCountReadBackBuffer = data.drawCountReadBackBuffer;
                params.triangleCountReadBackBuffer = data.triangleCountReadBackBuffer;

                params.globalDescriptorSet = data.globalSet;
                params.drawDescriptorSet = data.drawSet;

                params.drawCallback = [&](const DrawParams& drawParams)
                {
                    DrawSkybox(resources, frameIndex, graphResources, commandList, drawParams, true);
                };

                params.enableDrawing = CVAR_ModelDrawGeometry.Get();
                params.cullingEnabled = false;
                params.numCascades = 0;// *CVarSystem::Get()->GetIntCVar("shadows.cascade.num");

                GeometryPass(params);
            });
    }
}

void ModelRenderer::RegisterMaterialPassBufferUsage(Renderer::RenderGraphBuilder& builder)
{
    using BufferUsage = Renderer::BufferPassUsage;

    builder.Read(_opaqueCullingResources.GetDrawCalls().GetBuffer(), BufferUsage::COMPUTE);
    builder.Read(_opaqueCullingResources.GetDrawCallDatas().GetBuffer(), BufferUsage::COMPUTE);
    builder.Read(_vertices.GetBuffer(), BufferUsage::COMPUTE);
    builder.Read(_indices.GetBuffer(), BufferUsage::COMPUTE);
    builder.Read(_textureUnits.GetBuffer(), BufferUsage::COMPUTE);
    builder.Read(_instanceDatas.GetBuffer(), BufferUsage::COMPUTE);
    builder.Read(_instanceMatrices.GetBuffer(), BufferUsage::COMPUTE);
    builder.Read(_boneMatrices.GetBuffer(), BufferUsage::COMPUTE);
    builder.Write(_animatedVertices.GetBuffer(), BufferUsage::COMPUTE);
}

u32 ModelRenderer::GetInstanceIDFromDrawCallID(u32 drawCallID, bool isOpaque)
{
    Renderer::GPUVector<DrawCallData>& drawCallDatas = (isOpaque) ? _opaqueCullingResources.GetDrawCallDatas() : _transparentCullingResources.GetDrawCallDatas();

    if (drawCallDatas.Size() < drawCallID)
    {
        DebugHandler::PrintFatal("ModelRenderer : Tried to get InstanceID from invalid {0} DrawCallID {1}", isOpaque ? "Opaque" : "Transparent", drawCallID);
    }

    return drawCallDatas.Get()[drawCallID].instanceID;
}

void ModelRenderer::Reserve(const ReserveInfo& reserveInfo)
{
    _modelIDToNumInstances.resize(_modelIDToNumInstances.size() + reserveInfo.numModels);
    _modelManifests.resize(_modelManifests.size() + reserveInfo.numModels);

    _cullingDatas.Grow(reserveInfo.numModels);

    _vertices.Grow(reserveInfo.numVertices);
    _indices.Grow(reserveInfo.numIndices);

    _instanceDatas.Grow(reserveInfo.numInstances);
    _instanceMatrices.Grow(reserveInfo.numInstances);
    _instanceIDToOpaqueDrawCallOffset.resize(_instanceIDToOpaqueDrawCallOffset.size() + reserveInfo.numInstances);
    _instanceIDToTransparentDrawCallOffset.resize(_instanceIDToTransparentDrawCallOffset.size() + reserveInfo.numInstances);

    _textureUnits.Grow(reserveInfo.numTextureUnits);

    u32 numBoneMatrices = static_cast<u32>(_boneMatrices.Size());
    _boneMatrices.Grow(reserveInfo.numBones);

    std::vector<glm::mat4>& boneMatrices = _boneMatrices.Get();
    for (u32 i = numBoneMatrices; i < reserveInfo.numBones; ++i)
    {
        boneMatrices[i] = glm::mat4(1.0f);
    }

    u32 numDecorationSets = static_cast<u32>(_modelDecorationSets.size());
    _modelDecorationSets.resize(numDecorationSets + reserveInfo.numDecorationSets);

    u32 numDecorations = static_cast<u32>(_modelDecorations.size());
    _modelDecorations.resize(numDecorations + reserveInfo.numDecorations);

    _opaqueCullingResources.Grow(reserveInfo.numOpaqueDrawcalls);
    _transparentCullingResources.Grow(reserveInfo.numTransparentDrawcalls);

    _opaqueSkyboxCullingResources.Grow(reserveInfo.numOpaqueDrawcalls);
    _transparentSkyboxCullingResources.Grow(reserveInfo.numTransparentDrawcalls);

    u32 numUniqueOpaqueDrawCalls = static_cast<u32>(_modelOpaqueDrawCallTemplates.size());
    _modelOpaqueDrawCallTemplates.resize(numUniqueOpaqueDrawCalls + reserveInfo.numUniqueOpaqueDrawcalls);
    _modelOpaqueDrawCallDataTemplates.resize(numUniqueOpaqueDrawCalls + reserveInfo.numUniqueOpaqueDrawcalls);

    u32 numUniqueTransparentDrawCalls = static_cast<u32>(_modelTransparentDrawCallTemplates.size());
    _modelTransparentDrawCallTemplates.resize(numUniqueTransparentDrawCalls + reserveInfo.numUniqueTransparentDrawcalls);
    _modelTransparentDrawCallDataTemplates.resize(numUniqueTransparentDrawCalls + reserveInfo.numUniqueTransparentDrawcalls);
}

void ModelRenderer::FitBuffersAfterLoad()
{
    u32 numModelsUsed = _modelManifestsIndex.load();
    _cullingDatas.Resize(numModelsUsed);

    u32 numVerticesUsed = _verticesIndex.load();
    _vertices.Resize(numVerticesUsed);

    u32 numIndicesUsed = _indicesIndex.load();
    _indices.Resize(numIndicesUsed);

    u32 numInstancesUsed = _instanceIndex.load();
    _instanceDatas.Resize(numInstancesUsed);
    _instanceMatrices.Resize(numInstancesUsed);
    _instanceIDToOpaqueDrawCallOffset.resize(numInstancesUsed);
    _instanceIDToTransparentDrawCallOffset.resize(numInstancesUsed);

    u32 numTextureUnitsUsed = _textureUnitIndex.load();
    _textureUnits.Resize(numTextureUnitsUsed);

    u32 numBoneMatricesUsed = _boneMatrixIndex.load();
    _boneMatrices.Resize(numBoneMatricesUsed);

    u32 numDecorationSetsUsed = _modelDecorationSetsIndex.load();
    _modelDecorationSets.resize(numDecorationSetsUsed);

    u32 numDecorationsUsed = _modelDecorationsIndex.load();
    _modelDecorations.resize(numDecorationsUsed);

    _opaqueCullingResources.FitBuffersAfterLoad();
    _transparentCullingResources.FitBuffersAfterLoad();

    _opaqueSkyboxCullingResources.FitBuffersAfterLoad();
    _transparentSkyboxCullingResources.FitBuffersAfterLoad();

    u32 numUniqueOpaqueDrawCalls = _modelOpaqueDrawCallTemplateIndex.load();
    _modelOpaqueDrawCallTemplates.resize(numUniqueOpaqueDrawCalls);
    _modelOpaqueDrawCallDataTemplates.resize(numUniqueOpaqueDrawCalls);

    u32 numUniqueTransparentDrawCalls = _modelTransparentDrawCallTemplateIndex.load();
    _modelTransparentDrawCallTemplates.resize(numUniqueTransparentDrawCalls);
    _modelTransparentDrawCallDataTemplates.resize(numUniqueTransparentDrawCalls);
}

u32 ModelRenderer::LoadModel(const std::string& name, Model::ComplexModel& model)
{
    EnttRegistries* registries = ServiceLocator::GetEnttRegistries();

    entt::registry* registry = registries->gameRegistry;

    entt::registry::context& ctx = registry->ctx();
    ECS::Singletons::TextureSingleton& textureSingleton = ctx.get<ECS::Singletons::TextureSingleton>();

    // Add ModelManifest
    u32 modelManifestIndex = _modelManifestsIndex.fetch_add(1);
    ModelManifest& modelManifest = _modelManifests[modelManifestIndex];

    modelManifest.debugName = name;

    // Add CullingData
    {
        std::vector<Model::ComplexModel::CullingData>& cullingDatas = _cullingDatas.Get();
        Model::ComplexModel::CullingData& cullingData = cullingDatas[modelManifestIndex];
        cullingData = model.cullingData;
    }

    // Add vertices
    {
        modelManifest.numVertices = model.modelHeader.numVertices;
        modelManifest.vertexOffset = _verticesIndex.fetch_add(modelManifest.numVertices);

        if (modelManifest.numVertices)
        {
            std::vector<Model::ComplexModel::Vertex>& vertices = _vertices.Get();

            u32 numModelVertices = static_cast<u32>(model.vertices.size());
            assert(modelManifest.numVertices == numModelVertices);

            void* dst = &vertices[modelManifest.vertexOffset];
            void* src = model.vertices.data();
            size_t size = sizeof(Model::ComplexModel::Vertex) * numModelVertices;

            if (modelManifest.vertexOffset + numModelVertices > vertices.size())
            {
                DebugHandler::PrintFatal("ModelRenderer : Tried to memcpy vertices outside array");
            }

            memcpy(dst, src, size);
        }
    }

    // Add indices
    {
        modelManifest.numIndices = model.modelHeader.numIndices;
        modelManifest.indexOffset = _indicesIndex.fetch_add(modelManifest.numIndices);

        if (modelManifest.numIndices)
        {
            std::vector<u16>& indices = _indices.Get();

            void* dst = &indices[modelManifest.indexOffset];
            void* src = model.modelData.indices.data();
            size_t size = sizeof(u16) * model.modelData.indices.size();

            if (modelManifest.indexOffset + model.modelData.indices.size() > indices.size())
            {
                DebugHandler::PrintFatal("ModelRenderer : Tried to memcpy vertices outside array");
            }

            memcpy(dst, src, size);
        }
    }

    // Add TextureUnits and DrawCalls
    {
        modelManifest.numOpaqueDrawCalls = model.modelHeader.numOpaqueRenderBatches;
        modelManifest.opaqueDrawCallTemplateOffset = _modelOpaqueDrawCallTemplateIndex.fetch_add(modelManifest.numOpaqueDrawCalls);

        modelManifest.numTransparentDrawCalls = model.modelHeader.numTransparentRenderBatches;
        modelManifest.transparentDrawCallTemplateOffset = _modelTransparentDrawCallTemplateIndex.fetch_add(modelManifest.numTransparentDrawCalls);

        u32 numAddedIndices = 0;

        u32 numAddedOpaqueDrawCalls = 0;
        u32 numAddedTransparentDrawCalls = 0;

        for (auto& renderBatch : model.modelData.renderBatches)
        {
            u32 textureUnitBaseIndex = _textureUnitIndex.fetch_add(static_cast<u32>(renderBatch.textureUnits.size()));
            u16 numUnlitTextureUnits = 0;

            for (u32 i = 0; i < renderBatch.textureUnits.size(); i++)
            {
                // Texture Unit
                TextureUnit& textureUnit = _textureUnits.Get()[textureUnitBaseIndex + i];

                Model::ComplexModel::TextureUnit& cTextureUnit = renderBatch.textureUnits[i];
                Model::ComplexModel::Material& cMaterial = model.materials[cTextureUnit.materialIndex];

                u16 materialFlag = *reinterpret_cast<u16*>(&cMaterial.flags) << 1;
                u16 blendingMode = static_cast<u16>(cMaterial.blendingMode) << 11;

                textureUnit.data = static_cast<u16>(cTextureUnit.flags.IsProjectedTexture) | materialFlag | blendingMode;
                textureUnit.materialType = cTextureUnit.shaderID;
                textureUnit.textureTransformIds[0] = MODEL_INVALID_TEXTURE_TRANSFORM_ID; // complexTextureUnit.textureTransformIndices[0];
                textureUnit.textureTransformIds[1] = MODEL_INVALID_TEXTURE_TRANSFORM_ID; // complexTextureUnit.textureTransformIndices[1];

                numUnlitTextureUnits += (materialFlag & 0x2) > 0;

                // Textures
                for (u32 j = 0; j < cTextureUnit.textureCount && j < 2; j++)
                {
                    std::scoped_lock lock(_textureLoadMutex);

                    Renderer::TextureDesc textureDesc;
                    u16 textureIndex = model.textureIndexLookupTable[cTextureUnit.textureIndexStart + j];

                    Model::ComplexModel::Texture& cTexture = model.textures[textureIndex];
                    if (cTexture.type == Model::ComplexModel::Texture::Type::None)
                    {
                        textureDesc.path = textureSingleton.textureHashToPath[cTexture.textureHash];
                    }
                    else if (cTexture.type == Model::ComplexModel::Texture::Type::Skin)
                    {
                        static const u32 defaultSkinHash = "textures/bakednpctextures/creaturedisplayextra-00872.dds"_h;
                        textureDesc.path = textureSingleton.textureHashToPath[defaultSkinHash];
                    }
                    else if (cTexture.type == Model::ComplexModel::Texture::Type::CharacterHair)
                    {
                        static const u32 defaultHairHash = "character/human/female/humanfemalehairlongwavy.dds"_h;
                        textureDesc.path = textureSingleton.textureHashToPath[defaultHairHash];
                    }

                    if (textureDesc.path.size() > 0)
                    {
                        Renderer::TextureID textureID = _renderer->LoadTextureIntoArray(textureDesc, _textures, textureUnit.textureIds[j]);
                        textureSingleton.textureHashToTextureID[cTexture.textureHash] = static_cast<Renderer::TextureID::type>(textureID);

                        DebugHandler::Assert(textureUnit.textureIds[j] < Renderer::Settings::MAX_TEXTURES, "ModelRenderer : LoadModel overflowed the {0} textures we have support for", Renderer::Settings::MAX_TEXTURES);
                    }
                }
            }

            // Draw Calls
            u32& numAddedDrawCalls = (renderBatch.isTransparent) ? numAddedTransparentDrawCalls : numAddedOpaqueDrawCalls;
            u32& drawCallTemplateOffset = (renderBatch.isTransparent) ? modelManifest.transparentDrawCallTemplateOffset : modelManifest.opaqueDrawCallTemplateOffset;

            u32 curDrawCallOffset = drawCallTemplateOffset + numAddedDrawCalls;

            bool isAllowedGroupID = true;

            switch (renderBatch.groupID)
            {
            case 0: // Base
            case 2: // Bald Head
            case 401: // Gloves
            case 501: // Boots
            case 702: // Ears
            case 1301: // Legs
            case 1501: // Upper Back
                //case 1703: // DK Eye Glow (Needs further support to be animated)
                break;

            default:
            {
                isAllowedGroupID = false;
                break;
            }
            }

            Renderer::IndexedIndirectDraw& drawCallTemplate = (renderBatch.isTransparent) ? _modelTransparentDrawCallTemplates[curDrawCallOffset] : _modelOpaqueDrawCallTemplates[curDrawCallOffset];
            drawCallTemplate.indexCount = renderBatch.indexCount;
            drawCallTemplate.instanceCount = 1 * isAllowedGroupID;
            drawCallTemplate.firstIndex = modelManifest.indexOffset + renderBatch.indexStart;
            drawCallTemplate.vertexOffset = modelManifest.vertexOffset + renderBatch.vertexStart;
            drawCallTemplate.firstInstance = 0; // Is set during AddInstance

            DrawCallData& drawCallData = (renderBatch.isTransparent) ? _modelTransparentDrawCallDataTemplates[curDrawCallOffset] : _modelOpaqueDrawCallDataTemplates[curDrawCallOffset];
            drawCallData.instanceID = 0; // Is set during AddInstance
            drawCallData.textureUnitOffset = textureUnitBaseIndex;
            drawCallData.numTextureUnits = static_cast<u16>(renderBatch.textureUnits.size());
            drawCallData.numUnlitTextureUnits = numUnlitTextureUnits;

            numAddedDrawCalls++;
        }
    }

    // Set Animated Data
    {
        modelManifest.numBones = static_cast<u32>(model.bones.size());
        modelManifest.numTextureTransforms = static_cast<u32>(model.textureTransforms.size());

        modelManifest.isAnimated = model.sequences.size() > 0 && modelManifest.numBones > 0;
    }

    // Add Decoration Data
    {
        modelManifest.numDecorationSets = model.modelHeader.numDecorationSets;
        modelManifest.decorationSetOffset = _modelDecorationSetsIndex.fetch_add(modelManifest.numDecorationSets);

        if (modelManifest.numDecorationSets)
        {
            std::vector<Model::ComplexModel::DecorationSet>& decorationSets = _modelDecorationSets;

            void* dst = &decorationSets[modelManifest.decorationSetOffset];
            void* src = model.decorationSets.data();
            size_t size = sizeof(Model::ComplexModel::DecorationSet) * model.decorationSets.size();

            if (modelManifest.decorationSetOffset + model.decorationSets.size() > decorationSets.size())
            {
                DebugHandler::PrintFatal("ModelRenderer : Tried to memcpy decorationSets outside array");
            }

            memcpy(dst, src, size);
        }

        modelManifest.numDecorations = model.modelHeader.numDecorations;
        modelManifest.decorationOffset = _modelDecorationsIndex.fetch_add(modelManifest.numDecorations);

        if (modelManifest.numDecorations)
        {
            std::vector<Model::ComplexModel::Decoration>& decorations = _modelDecorations;

            void* dst = &decorations[modelManifest.decorationOffset];
            void* src = model.decorations.data();
            size_t size = sizeof(Model::ComplexModel::Decoration) * model.decorations.size();

            if (modelManifest.decorationOffset + model.decorations.size() > decorations.size())
            {
                DebugHandler::PrintFatal("ModelRenderer : Tried to memcpy decorations outside array");
            }

            memcpy(dst, src, size);
        }
    }

    return modelManifestIndex;
}

u32 ModelRenderer::AddPlacementInstance(entt::entity entityID, u32 modelID, const Terrain::Placement& placement)
{
    vec3 scale = vec3(placement.scale) / 1024.0f;

    // Add Instance matrix
    mat4x4 rotationMatrix = glm::toMat4(placement.rotation);
    mat4x4 scaleMatrix = glm::scale(mat4x4(1.0f), scale);
    mat4x4 instanceMatrix = glm::translate(mat4x4(1.0f), placement.position) * rotationMatrix * scaleMatrix;

    u32 instanceID = AddInstance(entityID, modelID, instanceMatrix);

    ModelManifest& manifest = _modelManifests[modelID];

    // Add Decorations
    if (manifest.numDecorationSets && manifest.numDecorations)
    {
        if (placement.doodadSet == std::numeric_limits<u16>().max())
        {
            ModelLoader* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();

            // Load 0th doodadSet if it exists
            const Model::ComplexModel::DecorationSet& manifestDecorationSet = _modelDecorationSets[manifest.decorationSetOffset];

            for (u32 i = 0; i < manifestDecorationSet.count; i++)
            {
                const Model::ComplexModel::Decoration& manifestDecoration = _modelDecorations[manifest.decorationOffset + (manifestDecorationSet.index + i)];
                modelLoader->LoadDecoration(instanceID, manifestDecoration);
            }
        }
        else
        {
            if (placement.doodadSet < manifest.numDecorationSets)
            {
                ModelLoader* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();

                const Model::ComplexModel::DecorationSet& manifestDecorationSet = _modelDecorationSets[manifest.decorationSetOffset + placement.doodadSet];

                for (u32 i = 0; i < manifestDecorationSet.count; i++)
                {
                    const Model::ComplexModel::Decoration& manifestDecoration = _modelDecorations[manifest.decorationOffset + (manifestDecorationSet.index + i)];
                    modelLoader->LoadDecoration(instanceID, manifestDecoration);
                }
            }
        }
    }

    return instanceID;
}

u32 ModelRenderer::AddInstance(entt::entity entityID, u32 modelID, const mat4x4& transformMatrix)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    bool isSkybox = registry->all_of<ECS::Components::SkyboxModelTag>(entityID);

    ModelManifest& manifest = _modelManifests[modelID];

    u32 modelInstanceIndex = 0;
    {
        std::scoped_lock lock(_modelIDToNumInstancesMutex);
        modelInstanceIndex = _modelIDToNumInstances[modelID]++;
    }

    u32 instanceID = _instanceIndex.fetch_add(1);

    // Add InstanceData
    {
        InstanceData& instanceData = _instanceDatas.Get()[instanceID];

        instanceData.modelID = modelID;
        instanceData.modelVertexOffset = manifest.vertexOffset;

        if (manifest.isAnimated)
        {
            i32* animationSystemEnabled = CVarSystem::Get()->GetIntCVar("animationSystem.enabled"_h);
            if (animationSystemEnabled && *animationSystemEnabled == 1)
            {
                u32 animatedVertexOffset = _animatedVerticesIndex.fetch_add(manifest.numVertices);
                instanceData.animatedVertexOffset = animatedVertexOffset;
            }
        }
    }

    // Add Instance matrix
    {
        mat4x4& instanceMatrix = _instanceMatrices.Get()[instanceID];
        instanceMatrix = transformMatrix;
    }

    // Set up Opaque DrawCalls and DrawCallDatas
    if (manifest.numOpaqueDrawCalls > 0)
    {
        CullingResourcesIndexed<DrawCallData>& opaqueCullingResources = (isSkybox) ? _opaqueSkyboxCullingResources : _opaqueCullingResources;

        std::vector<Renderer::IndexedIndirectDraw>& opaqueDrawCalls = opaqueCullingResources.GetDrawCalls().Get();
        std::vector<DrawCallData>& opaqueDrawCallDatas = opaqueCullingResources.GetDrawCallDatas().Get();

        u32 opaqueDrawCallOffset = opaqueCullingResources.GetDrawCallsIndex().fetch_add(manifest.numOpaqueDrawCalls);
        _instanceIDToOpaqueDrawCallOffset[instanceID] = opaqueDrawCallOffset;

        // Copy DrawCalls
        {
            Renderer::IndexedIndirectDraw* dst = &opaqueDrawCalls[opaqueDrawCallOffset];
            Renderer::IndexedIndirectDraw* src = &_modelOpaqueDrawCallTemplates[manifest.opaqueDrawCallTemplateOffset];
            size_t size = manifest.numOpaqueDrawCalls * sizeof(Renderer::IndexedIndirectDraw);
            memcpy(dst, src, size);
        }

        // Copy DrawCallDatas
        {
            DrawCallData* dst = &opaqueDrawCallDatas[opaqueDrawCallOffset];
            DrawCallData* src = &_modelOpaqueDrawCallDataTemplates[manifest.opaqueDrawCallTemplateOffset];
            size_t size = manifest.numOpaqueDrawCalls * sizeof(DrawCallData);
            memcpy(dst, src, size);
        }

        // Modify the per-instance data
        for (u32 i = 0; i < manifest.numOpaqueDrawCalls; i++)
        {
            u32 opaqueIndex = opaqueDrawCallOffset + i;

            Renderer::IndexedIndirectDraw& drawCall = opaqueDrawCalls[opaqueIndex];
            drawCall.firstInstance = opaqueIndex;

            DrawCallData& drawCallData = opaqueDrawCallDatas[opaqueIndex];
            drawCallData.instanceID = instanceID;
            drawCallData.modelID = modelID;
        }
    }

    // Set up Transparent DrawCalls and DrawCallDatas
    if (manifest.numTransparentDrawCalls > 0)
    {
        CullingResourcesIndexed<DrawCallData>& transparentCullingResources = (isSkybox) ? _transparentSkyboxCullingResources : _transparentCullingResources;

        std::vector<Renderer::IndexedIndirectDraw>& transparentDrawCalls = transparentCullingResources.GetDrawCalls().Get();
        std::vector<DrawCallData>& transparentDrawCallDatas = transparentCullingResources.GetDrawCallDatas().Get();

        u32 transparentDrawCallOffset = transparentCullingResources.GetDrawCallsIndex().fetch_add(manifest.numTransparentDrawCalls);
        _instanceIDToTransparentDrawCallOffset[instanceID] = transparentDrawCallOffset;

        // Copy DrawCalls
        {
            Renderer::IndexedIndirectDraw* dst = &transparentDrawCalls[transparentDrawCallOffset];
            Renderer::IndexedIndirectDraw* src = &_modelTransparentDrawCallTemplates[manifest.transparentDrawCallTemplateOffset];
            size_t size = manifest.numTransparentDrawCalls * sizeof(Renderer::IndexedIndirectDraw);
            memcpy(dst, src, size);
        }

        // Copy DrawCallDatas
        {
            DrawCallData* dst = &transparentDrawCallDatas[transparentDrawCallOffset];
            DrawCallData* src = &_modelTransparentDrawCallDataTemplates[manifest.transparentDrawCallTemplateOffset];
            size_t size = manifest.numTransparentDrawCalls * sizeof(DrawCallData);
            memcpy(dst, src, size);
        }

        // Modify the per-instance data
        for (u32 i = 0; i < manifest.numTransparentDrawCalls; i++)
        {
            u32 transparentIndex = transparentDrawCallOffset + i;

            Renderer::IndexedIndirectDraw& drawCall = transparentDrawCalls[transparentIndex];
            drawCall.firstInstance = transparentIndex;

            DrawCallData& drawCallData = transparentDrawCallDatas[transparentIndex];
            drawCallData.instanceID = instanceID;
            drawCallData.modelID = modelID;
        }
    }

    return instanceID;
}

void ModelRenderer::ModifyInstance(entt::entity entityID, u32 instanceID, u32 modelID, const mat4x4& transformMatrix)
{
    InstanceData& instanceData = _instanceDatas.Get()[instanceID];

    u32 oldModelID = instanceData.modelID;

    if (modelID == oldModelID)
        return;

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    bool isSkybox = registry->all_of<ECS::Components::SkyboxModelTag>(entityID);

    u32 oldOpaqueNumDrawCalls = std::numeric_limits<u32>().max();
    u32 oldOpaqueBaseIndex = std::numeric_limits<u32>().max();
    u32 oldTransparentNumDrawCalls = std::numeric_limits<u32>().max();
    u32 oldTransparentBaseIndex = std::numeric_limits<u32>().max();

    if (oldModelID != std::numeric_limits<u32>().max())
    {
        std::scoped_lock lock(_modelIDToNumInstancesMutex);

        ModelManifest& oldManifest = _modelManifests[oldModelID];

        _modelIDToNumInstances[oldModelID]--;
        oldOpaqueNumDrawCalls = oldManifest.numOpaqueDrawCalls;
        oldOpaqueBaseIndex = _instanceIDToOpaqueDrawCallOffset[instanceID];
        oldTransparentNumDrawCalls = oldManifest.numTransparentDrawCalls;
        oldTransparentBaseIndex = _instanceIDToTransparentDrawCallOffset[instanceID];
    }

    // Get the correct culling resources
    CullingResourcesIndexed<DrawCallData>& opaqueCullingResources = (isSkybox) ? _opaqueSkyboxCullingResources : _opaqueCullingResources;

    std::vector<Renderer::IndexedIndirectDraw>& opaqueDrawCalls = opaqueCullingResources.GetDrawCalls().Get();
    std::vector<DrawCallData>& opaqueDrawCallDatas = opaqueCullingResources.GetDrawCallDatas().Get();

    CullingResourcesIndexed<DrawCallData>& transparentCullingResources = (isSkybox) ? _transparentSkyboxCullingResources : _transparentCullingResources;

    std::vector<Renderer::IndexedIndirectDraw>& transparentDrawCalls = transparentCullingResources.GetDrawCalls().Get();
    std::vector<DrawCallData>& transparentDrawCallDatas = transparentCullingResources.GetDrawCallDatas().Get();

    // Update the instancedatas modelID
    instanceData.modelID = modelID;

    // Set up new drawcalls if the modelID is valid
    if (modelID != std::numeric_limits<u32>().max())
    {
        ModelManifest& manifest = _modelManifests[modelID];

        u32 modelInstanceIndex = 0;
        {
            std::scoped_lock lock(_modelIDToNumInstancesMutex);
            modelInstanceIndex = _modelIDToNumInstances[modelID]++;
        }

        // Modify InstanceData
        {
            instanceData.modelVertexOffset = manifest.vertexOffset;

            if (manifest.isAnimated)
            {
                i32* animationSystemEnabled = CVarSystem::Get()->GetIntCVar("animationSystem.enabled"_h);
                if (animationSystemEnabled && *animationSystemEnabled == 1)
                {
                    u32 animatedVertexOffset = _animatedVerticesIndex.fetch_add(manifest.numVertices);
                    instanceData.animatedVertexOffset = animatedVertexOffset;
                }
            }
            else
            {
                instanceData.animatedVertexOffset = std::numeric_limits<u32>().max();
            }

            _instanceDatas.SetDirtyElement(instanceID);
        }

        // Setup Instance matrix
        {
            mat4x4& instanceMatrix = _instanceMatrices.Get()[instanceID];
            instanceMatrix = transformMatrix;

            _instanceMatrices.SetDirtyElement(instanceID);
        }

        // Set up Opaque DrawCalls and DrawCallDatas
        if (manifest.numOpaqueDrawCalls > 0)
        {
            u32 opaqueDrawCallOffset = opaqueCullingResources.GetDrawCallsIndex().fetch_add(manifest.numOpaqueDrawCalls);
            _instanceIDToOpaqueDrawCallOffset[instanceID] = opaqueDrawCallOffset;

            // Copy DrawCalls
            {
                Renderer::IndexedIndirectDraw* dst = &opaqueDrawCalls[opaqueDrawCallOffset];
                Renderer::IndexedIndirectDraw* src = &_modelOpaqueDrawCallTemplates[manifest.opaqueDrawCallTemplateOffset];
                size_t size = manifest.numOpaqueDrawCalls * sizeof(Renderer::IndexedIndirectDraw);
                memcpy(dst, src, size);
            }

            // Copy DrawCallDatas
            {
                DrawCallData* dst = &opaqueDrawCallDatas[opaqueDrawCallOffset];
                DrawCallData* src = &_modelOpaqueDrawCallDataTemplates[manifest.opaqueDrawCallTemplateOffset];
                size_t size = manifest.numOpaqueDrawCalls * sizeof(DrawCallData);
                memcpy(dst, src, size);
            }

            // Modify the per-instance data
            for (u32 i = 0; i < manifest.numOpaqueDrawCalls; i++)
            {
                u32 opaqueIndex = opaqueDrawCallOffset + i;

                Renderer::IndexedIndirectDraw& drawCall = opaqueDrawCalls[opaqueIndex];
                drawCall.firstInstance = opaqueIndex;

                DrawCallData& drawCallData = opaqueDrawCallDatas[opaqueIndex];
                drawCallData.instanceID = instanceID;
                drawCallData.modelID = modelID;
            }
        }

        // Set up Transparent DrawCalls and DrawCallDatas
        if (manifest.numTransparentDrawCalls > 0)
        {
            u32 transparentDrawCallOffset = transparentCullingResources.GetDrawCallsIndex().fetch_add(manifest.numTransparentDrawCalls);
            _instanceIDToTransparentDrawCallOffset[instanceID] = transparentDrawCallOffset;

            // Copy DrawCalls
            {
                Renderer::IndexedIndirectDraw* dst = &transparentDrawCalls[transparentDrawCallOffset];
                Renderer::IndexedIndirectDraw* src = &_modelTransparentDrawCallTemplates[manifest.transparentDrawCallTemplateOffset];
                size_t size = manifest.numTransparentDrawCalls * sizeof(Renderer::IndexedIndirectDraw);
                memcpy(dst, src, size);
            }

            // Copy DrawCallDatas
            {
                DrawCallData* dst = &transparentDrawCallDatas[transparentDrawCallOffset];
                DrawCallData* src = &_modelTransparentDrawCallDataTemplates[manifest.transparentDrawCallTemplateOffset];
                size_t size = manifest.numTransparentDrawCalls * sizeof(DrawCallData);
                memcpy(dst, src, size);
            }

            // Modify the per-instance data
            for (u32 i = 0; i < manifest.numTransparentDrawCalls; i++)
            {
                u32 transparentIndex = transparentDrawCallOffset + i;

                Renderer::IndexedIndirectDraw& drawCall = transparentDrawCalls[transparentIndex];
                drawCall.firstInstance = transparentIndex;

                DrawCallData& drawCallData = transparentDrawCallDatas[transparentIndex];
                drawCallData.instanceID = instanceID;
                drawCallData.modelID = modelID;
            }
        }
    }

    // Modify the old per-instance data
    if (oldModelID != std::numeric_limits<u32>().max())
    {
        if (oldOpaqueNumDrawCalls > 0)
        {
            for (u32 i = 0; i < oldOpaqueNumDrawCalls; i++)
            {
                u32 opaqueIndex = oldOpaqueBaseIndex + i;

                Renderer::IndexedIndirectDraw& drawCall = opaqueDrawCalls[opaqueIndex];
                drawCall.instanceCount = 0;

                DrawCallData& drawCallData = opaqueDrawCallDatas[opaqueIndex];
                drawCallData.instanceID = std::numeric_limits<u32>().max();
            }

            opaqueCullingResources.GetDrawCalls().SetDirtyElements(oldOpaqueBaseIndex, oldOpaqueNumDrawCalls);
            opaqueCullingResources.GetDrawCallDatas().SetDirtyElements(oldOpaqueBaseIndex, oldOpaqueNumDrawCalls);
        }

        // Modify the old per-instance data
        if (oldTransparentNumDrawCalls > 0)
        {
            for (u32 i = 0; i < oldTransparentNumDrawCalls; i++)
            {
                u32 transparentIndex = oldTransparentBaseIndex + i;

                Renderer::IndexedIndirectDraw& drawCall = transparentDrawCalls[transparentIndex];
                drawCall.instanceCount = 0;

                DrawCallData& drawCallData = transparentDrawCallDatas[transparentIndex];
                drawCallData.instanceID = std::numeric_limits<u32>().max();
            }

            transparentCullingResources.GetDrawCalls().SetDirtyElements(oldTransparentBaseIndex, oldTransparentNumDrawCalls);
            transparentCullingResources.GetDrawCallDatas().SetDirtyElements(oldTransparentBaseIndex, oldTransparentNumDrawCalls);
        }
    }
}

bool ModelRenderer::AddAnimationInstance(u32 instanceID)
{
    std::vector<InstanceData>& instanceDatas = _instanceDatas.Get();

    if (instanceID >= instanceDatas.size())
    {
        return false;
    }

    InstanceData& instanceData = instanceDatas[instanceID];

    if (instanceData.animatedVertexOffset == InstanceData::InvalidID)
    {
        return false;
    }

    if (instanceData.boneMatrixOffset != InstanceData::InvalidID)
    {
        return false;
    }

    const ModelManifest& modelManifest = _modelManifests[instanceData.modelID];
    instanceData.boneMatrixOffset = _boneMatrixIndex.fetch_add(modelManifest.numBones);

    return true;
}

bool ModelRenderer::SetBoneMatricesAsDirty(u32 instanceID, u32 localBoneIndex, u32 count, mat4x4* boneMatrixArray)
{
    std::vector<InstanceData>& instanceDatas = _instanceDatas.Get();
    if (instanceID >= instanceDatas.size())
    {
        return false;
    }

    InstanceData& instanceData = instanceDatas[instanceID];
    if (instanceData.boneMatrixOffset == InstanceData::InvalidID)
    {
        return false;
    }

    const ModelManifest& modelManifest = _modelManifests[instanceData.modelID];

    u32 globalBoneIndex = instanceData.boneMatrixOffset + localBoneIndex;
    u32 endGlobalBoneIndex = globalBoneIndex + (count - 1);

    // Check if the bone range is valid
    if (endGlobalBoneIndex > instanceData.boneMatrixOffset + modelManifest.numBones)
    {
        return false;
    }

    if (count == 1)
    {
        _boneMatrices.Get()[globalBoneIndex] = *boneMatrixArray;
        _boneMatrices.SetDirtyElement(globalBoneIndex);
    }
    else
    {
        memcpy(&_boneMatrices.Get()[globalBoneIndex], boneMatrixArray, count * sizeof(mat4x4));
        _boneMatrices.SetDirtyElements(globalBoneIndex, count);
    }

    return true;
}

void ModelRenderer::CreatePermanentResources()
{
    ZoneScoped;
    Renderer::TextureArrayDesc textureArrayDesc;
    textureArrayDesc.size = Renderer::Settings::MAX_TEXTURES;

    _textures = _renderer->CreateTextureArray(textureArrayDesc);
    _opaqueCullingResources.GetGeometryPassDescriptorSet().Bind("_modelTextures"_h, _textures);
    _transparentCullingResources.GetGeometryPassDescriptorSet().Bind("_modelTextures"_h, _textures);
    _opaqueSkyboxCullingResources.GetGeometryPassDescriptorSet().Bind("_modelTextures"_h, _textures);
    _transparentSkyboxCullingResources.GetGeometryPassDescriptorSet().Bind("_modelTextures"_h, _textures);
    _materialPassDescriptorSet.Bind("_modelTextures"_h, _textures);

    Renderer::DataTextureDesc dataTextureDesc;
    dataTextureDesc.width = 1;
    dataTextureDesc.height = 1;
    dataTextureDesc.format = Renderer::ImageFormat::R8G8B8A8_UNORM_SRGB;
    dataTextureDesc.data = new u8[4]{ 200, 200, 200, 255 };
    dataTextureDesc.debugName = "Model DebugTexture";

    u32 arrayIndex = 0;
    _renderer->CreateDataTextureIntoArray(dataTextureDesc, _textures, arrayIndex);

    Renderer::TextureDesc debugTextureDesc;
    debugTextureDesc.path = "Data/Texture/spells/frankcube.dds";

    _renderer->LoadTextureIntoArray(debugTextureDesc, _textures, arrayIndex);

    Renderer::SamplerDesc samplerDesc;
    samplerDesc.enabled = true;
    samplerDesc.filter = Renderer::SamplerFilter::MIN_MAG_MIP_LINEAR;
    samplerDesc.addressU = Renderer::TextureAddressMode::WRAP;
    samplerDesc.addressV = Renderer::TextureAddressMode::WRAP;
    samplerDesc.addressW = Renderer::TextureAddressMode::CLAMP;
    samplerDesc.shaderVisibility = Renderer::ShaderVisibility::PIXEL;

    _sampler = _renderer->CreateSampler(samplerDesc);
    _opaqueCullingResources.GetGeometryPassDescriptorSet().Bind("_sampler"_h, _sampler);
    _transparentCullingResources.GetGeometryPassDescriptorSet().Bind("_sampler"_h, _sampler);
    _opaqueSkyboxCullingResources.GetGeometryPassDescriptorSet().Bind("_sampler"_h, _sampler);
    _transparentSkyboxCullingResources.GetGeometryPassDescriptorSet().Bind("_sampler"_h, _sampler);

    CullingResourcesIndexed<DrawCallData>::InitParams initParams;
    initParams.renderer = _renderer;
    initParams.bufferNamePrefix = "OpaqueModels";
    initParams.materialPassDescriptorSet = &_materialPassDescriptorSet;
    initParams.enableTwoStepCulling = true;
    _opaqueCullingResources.Init(initParams);

    initParams.bufferNamePrefix = "TransparentModels";
    initParams.materialPassDescriptorSet = nullptr;
    initParams.enableTwoStepCulling = false;
    _transparentCullingResources.Init(initParams);

    initParams.bufferNamePrefix = "OpaqueSkyboxModels";
    initParams.materialPassDescriptorSet = nullptr;
    initParams.enableTwoStepCulling = false;
    _opaqueSkyboxCullingResources.Init(initParams);

    initParams.bufferNamePrefix = "TransparentSkyboxModels";
    initParams.materialPassDescriptorSet = nullptr;
    initParams.enableTwoStepCulling = false;
    _transparentSkyboxCullingResources.Init(initParams);
}

void ModelRenderer::SyncToGPU()
{
    CulledRenderer::SyncToGPU();

    // Sync Vertex buffer to GPU
    {
        _vertices.SetDebugName("ModelVertexBuffer");
        _vertices.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
        if (_vertices.SyncToGPU(_renderer))
        {
            _opaqueCullingResources.GetGeometryPassDescriptorSet().Bind("_packedModelVertices"_h, _vertices.GetBuffer());
            _transparentCullingResources.GetGeometryPassDescriptorSet().Bind("_packedModelVertices"_h, _vertices.GetBuffer());
            _opaqueSkyboxCullingResources.GetGeometryPassDescriptorSet().Bind("_packedModelVertices"_h, _vertices.GetBuffer());
            _transparentSkyboxCullingResources.GetGeometryPassDescriptorSet().Bind("_packedModelVertices"_h, _vertices.GetBuffer());
            _materialPassDescriptorSet.Bind("_packedModelVertices"_h, _vertices.GetBuffer());
        }
    }

    // Sync Animated Vertex buffer to GPU
    {
        _animatedVertices.SetDebugName("ModelAnimatedVertexBuffer");
        _animatedVertices.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

        size_t currentSizeInBuffer = _animatedVertices.Size();
        size_t numAnimatedVertices = _animatedVerticesIndex;
        size_t byteSize = numAnimatedVertices * sizeof(PackedAnimatedVertexPositions);

        if (byteSize > currentSizeInBuffer)
        {
            _animatedVertices.Resize(numAnimatedVertices);
        }

        if (_animatedVertices.SyncToGPU(_renderer))
        {
            _opaqueCullingResources.GetGeometryPassDescriptorSet().Bind("_animatedModelVertexPositions"_h, _animatedVertices.GetBuffer());
            _transparentCullingResources.GetGeometryPassDescriptorSet().Bind("_animatedModelVertexPositions"_h, _animatedVertices.GetBuffer());
            _opaqueSkyboxCullingResources.GetGeometryPassDescriptorSet().Bind("_animatedModelVertexPositions"_h, _animatedVertices.GetBuffer());
            _transparentSkyboxCullingResources.GetGeometryPassDescriptorSet().Bind("_animatedModelVertexPositions"_h, _animatedVertices.GetBuffer());
            _materialPassDescriptorSet.Bind("_animatedModelVertexPositions"_h, _animatedVertices.GetBuffer());
        }
    }

    // Sync Index buffer to GPU
    {
        _indices.SetDebugName("ModelIndexBuffer");
        _indices.SetUsage(Renderer::BufferUsage::INDEX_BUFFER | Renderer::BufferUsage::STORAGE_BUFFER);

        if (_indices.SyncToGPU(_renderer))
        {
            _opaqueCullingResources.GetGeometryPassDescriptorSet().Bind("_modelIndices"_h, _indices.GetBuffer());
            _transparentCullingResources.GetGeometryPassDescriptorSet().Bind("_modelIndices"_h, _indices.GetBuffer());
            _opaqueSkyboxCullingResources.GetGeometryPassDescriptorSet().Bind("_modelIndices"_h, _indices.GetBuffer());
            _transparentSkyboxCullingResources.GetGeometryPassDescriptorSet().Bind("_modelIndices"_h, _indices.GetBuffer());
            _materialPassDescriptorSet.Bind("_modelIndices"_h, _indices.GetBuffer());
        }
    }

    // Sync TextureUnit buffer to GPU
    {
        _textureUnits.SetDebugName("ModelTextureUnitBuffer");
        _textureUnits.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
        if (_textureUnits.SyncToGPU(_renderer))
        {
            _opaqueCullingResources.GetGeometryPassDescriptorSet().Bind("_modelTextureUnits"_h, _textureUnits.GetBuffer());
            _transparentCullingResources.GetGeometryPassDescriptorSet().Bind("_modelTextureUnits"_h, _textureUnits.GetBuffer());
            _opaqueSkyboxCullingResources.GetGeometryPassDescriptorSet().Bind("_modelTextureUnits"_h, _textureUnits.GetBuffer());
            _transparentSkyboxCullingResources.GetGeometryPassDescriptorSet().Bind("_modelTextureUnits"_h, _textureUnits.GetBuffer());
            _materialPassDescriptorSet.Bind("_modelTextureUnits"_h, _textureUnits.GetBuffer());
        }
    }

    // Sync InstanceDatas buffer to GPU
    {
        _instanceDatas.SetDebugName("ModelInstanceDatas");
        _instanceDatas.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
        if (_instanceDatas.SyncToGPU(_renderer))
        {
            _opaqueCullingResources.GetCullingDescriptorSet().Bind("_modelInstanceDatas"_h, _instanceDatas.GetBuffer());
            _opaqueCullingResources.GetGeometryPassDescriptorSet().Bind("_modelInstanceDatas"_h, _instanceDatas.GetBuffer());

            _transparentCullingResources.GetCullingDescriptorSet().Bind("_modelInstanceDatas"_h, _instanceDatas.GetBuffer());
            _transparentCullingResources.GetGeometryPassDescriptorSet().Bind("_modelInstanceDatas"_h, _instanceDatas.GetBuffer());

            _opaqueSkyboxCullingResources.GetGeometryPassDescriptorSet().Bind("_modelInstanceDatas"_h, _instanceDatas.GetBuffer());
            _transparentSkyboxCullingResources.GetGeometryPassDescriptorSet().Bind("_modelInstanceDatas"_h, _instanceDatas.GetBuffer());

            _materialPassDescriptorSet.Bind("_modelInstanceDatas"_h, _instanceDatas.GetBuffer());
        }
    }

    // Sync InstanceMatrices buffer to GPU
    {
        _instanceMatrices.SetDebugName("ModelInstanceMatrices");
        _instanceMatrices.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
        if (_instanceMatrices.SyncToGPU(_renderer))
        {
            _opaqueCullingResources.GetCullingDescriptorSet().Bind("_instanceMatrices"_h, _instanceMatrices.GetBuffer());
            _transparentCullingResources.GetCullingDescriptorSet().Bind("_instanceMatrices"_h, _instanceMatrices.GetBuffer());
            //_animationPrepassDescriptorSet.Bind("_modelInstanceMatrices"_h, _instanceMatrices.GetBuffer());

            _opaqueCullingResources.GetGeometryPassDescriptorSet().Bind("_modelInstanceMatrices"_h, _instanceMatrices.GetBuffer());
            _transparentCullingResources.GetGeometryPassDescriptorSet().Bind("_modelInstanceMatrices"_h, _instanceMatrices.GetBuffer());
            _opaqueSkyboxCullingResources.GetGeometryPassDescriptorSet().Bind("_modelInstanceMatrices"_h, _instanceMatrices.GetBuffer());
            _transparentSkyboxCullingResources.GetGeometryPassDescriptorSet().Bind("_modelInstanceMatrices"_h, _instanceMatrices.GetBuffer());
            _materialPassDescriptorSet.Bind("_modelInstanceMatrices"_h, _instanceMatrices.GetBuffer());
        }
    }

    // Sync BoneMatrices buffer to GPU
    {
        _boneMatrices.SetDebugName("ModelInstanceBoneMatrices");
        _boneMatrices.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
        if (_boneMatrices.SyncToGPU(_renderer))
        {
            //_animationPrepassDescriptorSet.Bind("_instanceBoneMatrices"_h, _boneMatrices.GetBuffer());

            _opaqueCullingResources.GetGeometryPassDescriptorSet().Bind("_instanceBoneMatrices"_h, _boneMatrices.GetBuffer());
            _transparentCullingResources.GetGeometryPassDescriptorSet().Bind("_instanceBoneMatrices"_h, _boneMatrices.GetBuffer());
            _opaqueSkyboxCullingResources.GetGeometryPassDescriptorSet().Bind("_instanceBoneMatrices"_h, _boneMatrices.GetBuffer());
            _transparentSkyboxCullingResources.GetGeometryPassDescriptorSet().Bind("_instanceBoneMatrices"_h, _boneMatrices.GetBuffer());
            _materialPassDescriptorSet.Bind("_instanceBoneMatrices"_h, _boneMatrices.GetBuffer());
        }
    }

    _opaqueCullingResources.SyncToGPU();
    _transparentCullingResources.SyncToGPU();
    _opaqueSkyboxCullingResources.SyncToGPU();
    _transparentSkyboxCullingResources.SyncToGPU();

    BindCullingResource(_opaqueCullingResources);
    BindCullingResource(_transparentCullingResources);
    BindCullingResource(_opaqueSkyboxCullingResources);
    BindCullingResource(_transparentSkyboxCullingResources);
}

void ModelRenderer::Draw(const RenderResources& resources, u8 frameIndex, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, const DrawParams& params)
{
    Renderer::GraphicsPipelineDesc pipelineDesc;
    graphResources.InitializePipelineDesc(pipelineDesc);

    // Shaders
    Renderer::VertexShaderDesc vertexShaderDesc;
    vertexShaderDesc.path = "Model/Draw.vs.hlsl";
    vertexShaderDesc.AddPermutationField("EDITOR_PASS", "0");
    vertexShaderDesc.AddPermutationField("SHADOW_PASS", params.shadowPass ? "1" : "0");
    vertexShaderDesc.AddPermutationField("SUPPORTS_EXTENDED_TEXTURES", _renderer->HasExtendedTextureSupport() ? "1" : "0");

    pipelineDesc.states.vertexShader = _renderer->LoadShader(vertexShaderDesc);

    Renderer::PixelShaderDesc pixelShaderDesc;
    pixelShaderDesc.path = "Model/Draw.ps.hlsl";
    pixelShaderDesc.AddPermutationField("SHADOW_PASS", params.shadowPass ? "1" : "0");
    pixelShaderDesc.AddPermutationField("SUPPORTS_EXTENDED_TEXTURES", _renderer->HasExtendedTextureSupport() ? "1" : "0");
    pipelineDesc.states.pixelShader = _renderer->LoadShader(pixelShaderDesc);

    // Depth state
    pipelineDesc.states.depthStencilState.depthEnable = true;
    pipelineDesc.states.depthStencilState.depthWriteEnable = true;
    pipelineDesc.states.depthStencilState.depthFunc = Renderer::ComparisonFunc::GREATER;

    // Rasterizer state
    pipelineDesc.states.rasterizerState.cullMode = Renderer::CullMode::BACK;
    pipelineDesc.states.rasterizerState.frontFaceMode = Renderer::Settings::FRONT_FACE_STATE;
    pipelineDesc.states.rasterizerState.depthBiasEnabled = params.shadowPass;
    pipelineDesc.states.rasterizerState.depthClampEnabled = params.shadowPass;

    // Render targets
    if (!params.shadowPass)
    {
        pipelineDesc.renderTargets[0] = params.rt0;
    }
    if (params.rt1 != Renderer::ImageMutableResource::Invalid())
    {
        pipelineDesc.renderTargets[1] = params.rt1;
    }
    pipelineDesc.depthStencil = params.depth;

    // Draw
    Renderer::GraphicsPipelineID pipeline = _renderer->CreatePipeline(pipelineDesc); // This will compile the pipeline and return the ID, or just return ID of cached pipeline
    commandList.BeginPipeline(pipeline);

    /* TODO: Shadows
    if (params.shadowPass)
    {
        struct PushConstants
        {
            u32 cascadeIndex;
        };

        PushConstants* constants = graphResources.FrameNew<PushConstants>();

        constants->cascadeIndex = params.shadowCascade;
        commandList.PushConstant(constants, 0, sizeof(PushConstants));
    }*/

    commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, params.globalDescriptorSet, frameIndex);
    //commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::SHADOWS, &resources.shadowDescriptorSet, frameIndex);
    commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::MODEL, params.drawDescriptorSet, frameIndex);

    commandList.SetIndexBuffer(_indices.GetBuffer(), Renderer::IndexFormat::UInt16);

    if (params.cullingEnabled)
    {
        u32 drawCountBufferOffset = params.drawCountIndex * sizeof(u32);
        commandList.DrawIndexedIndirectCount(params.argumentBuffer, 0, params.drawCountBuffer, drawCountBufferOffset, params.numMaxDrawCalls);
    }
    else
    {
        commandList.DrawIndexedIndirect(params.argumentBuffer, 0, params.numMaxDrawCalls);
    }

    commandList.EndPipeline(pipeline);
}

void ModelRenderer::DrawTransparent(const RenderResources& resources, u8 frameIndex, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, const DrawParams& params)
{
    Renderer::GraphicsPipelineDesc pipelineDesc;
    graphResources.InitializePipelineDesc(pipelineDesc);

    // Shaders
    Renderer::VertexShaderDesc vertexShaderDesc;
    vertexShaderDesc.path = "Model/DrawTransparent.vs.hlsl";
    vertexShaderDesc.AddPermutationField("SUPPORTS_EXTENDED_TEXTURES", _renderer->HasExtendedTextureSupport() ? "1" : "0");

    pipelineDesc.states.vertexShader = _renderer->LoadShader(vertexShaderDesc);

    Renderer::PixelShaderDesc pixelShaderDesc;
    pixelShaderDesc.path = "Model/DrawTransparent.ps.hlsl";
    pixelShaderDesc.AddPermutationField("SUPPORTS_EXTENDED_TEXTURES", _renderer->HasExtendedTextureSupport() ? "1" : "0");

    pipelineDesc.states.pixelShader = _renderer->LoadShader(pixelShaderDesc);

    // Depth state
    pipelineDesc.states.depthStencilState.depthEnable = true;
    pipelineDesc.states.depthStencilState.depthFunc = Renderer::ComparisonFunc::GREATER;

    // Rasterizer state
    pipelineDesc.states.rasterizerState.cullMode = Renderer::CullMode::BACK;
    pipelineDesc.states.rasterizerState.frontFaceMode = Renderer::Settings::FRONT_FACE_STATE;

    // Blend state
    pipelineDesc.states.blendState.independentBlendEnable = true;

    pipelineDesc.states.blendState.renderTargets[0].blendEnable = true;
    pipelineDesc.states.blendState.renderTargets[0].blendOp = Renderer::BlendOp::ADD;
    pipelineDesc.states.blendState.renderTargets[0].srcBlend = Renderer::BlendMode::ONE;
    pipelineDesc.states.blendState.renderTargets[0].destBlend = Renderer::BlendMode::ONE;
    pipelineDesc.states.blendState.renderTargets[0].srcBlendAlpha = Renderer::BlendMode::ONE;
    pipelineDesc.states.blendState.renderTargets[0].destBlendAlpha = Renderer::BlendMode::ONE;
    pipelineDesc.states.blendState.renderTargets[0].blendOpAlpha = Renderer::BlendOp::ADD;

    pipelineDesc.states.blendState.renderTargets[1].blendEnable = true;
    pipelineDesc.states.blendState.renderTargets[1].blendOp = Renderer::BlendOp::ADD;
    pipelineDesc.states.blendState.renderTargets[1].srcBlend = Renderer::BlendMode::ZERO;
    pipelineDesc.states.blendState.renderTargets[1].destBlend = Renderer::BlendMode::INV_SRC_ALPHA;
    pipelineDesc.states.blendState.renderTargets[1].srcBlendAlpha = Renderer::BlendMode::ZERO;
    pipelineDesc.states.blendState.renderTargets[1].destBlendAlpha = Renderer::BlendMode::INV_SRC_ALPHA;
    pipelineDesc.states.blendState.renderTargets[1].blendOpAlpha = Renderer::BlendOp::ADD;

    // Render targets
    pipelineDesc.renderTargets[0] = params.rt0;
    if (params.rt1 != Renderer::ImageMutableResource::Invalid())
    {
        pipelineDesc.renderTargets[1] = params.rt1;
    }
    pipelineDesc.depthStencil = params.depth;

    // Draw
    Renderer::GraphicsPipelineID pipeline = _renderer->CreatePipeline(pipelineDesc); // This will compile the pipeline and return the ID, or just return ID of cached pipeline
    commandList.BeginPipeline(pipeline);

    commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, params.globalDescriptorSet, frameIndex);
    commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::MODEL, params.drawDescriptorSet, frameIndex);

    commandList.SetIndexBuffer(_indices.GetBuffer(), Renderer::IndexFormat::UInt16);

    if (params.cullingEnabled)
    {
        u32 drawCountBufferOffset = params.drawCountIndex * sizeof(u32);
        commandList.DrawIndexedIndirectCount(params.argumentBuffer, 0, params.drawCountBuffer, drawCountBufferOffset, params.numMaxDrawCalls);
    }
    else
    {
        commandList.DrawIndexedIndirect(params.argumentBuffer, 0, params.numMaxDrawCalls);
    }

    commandList.EndPipeline(pipeline);
}

void ModelRenderer::DrawSkybox(const RenderResources& resources, u8 frameIndex, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, const DrawParams& params, bool isTransparent)
{
    Renderer::GraphicsPipelineDesc pipelineDesc;
    graphResources.InitializePipelineDesc(pipelineDesc);

    // Shaders
    Renderer::VertexShaderDesc vertexShaderDesc;
    vertexShaderDesc.path = "Model/DrawSkybox.vs.hlsl";
    vertexShaderDesc.AddPermutationField("SUPPORTS_EXTENDED_TEXTURES", _renderer->HasExtendedTextureSupport() ? "1" : "0");

    pipelineDesc.states.vertexShader = _renderer->LoadShader(vertexShaderDesc);

    Renderer::PixelShaderDesc pixelShaderDesc;
    pixelShaderDesc.path = "Model/DrawSkybox.ps.hlsl";
    pixelShaderDesc.AddPermutationField("SUPPORTS_EXTENDED_TEXTURES", _renderer->HasExtendedTextureSupport() ? "1" : "0");

    pipelineDesc.states.pixelShader = _renderer->LoadShader(pixelShaderDesc);

    // Depth state
    pipelineDesc.states.depthStencilState.depthEnable = true;
    pipelineDesc.states.depthStencilState.depthFunc = Renderer::ComparisonFunc::GREATER;
    pipelineDesc.states.depthStencilState.depthWriteEnable = !isTransparent;

    // Blend state
    if (isTransparent)
    {
        pipelineDesc.states.blendState.renderTargets[0].blendEnable = true;
        pipelineDesc.states.blendState.renderTargets[0].blendOp = Renderer::BlendOp::ADD;
        pipelineDesc.states.blendState.renderTargets[0].srcBlend = Renderer::BlendMode::ONE;
        pipelineDesc.states.blendState.renderTargets[0].destBlend = Renderer::BlendMode::INV_SRC_ALPHA;
        pipelineDesc.states.blendState.renderTargets[0].srcBlendAlpha = Renderer::BlendMode::ONE;
        pipelineDesc.states.blendState.renderTargets[0].destBlendAlpha = Renderer::BlendMode::INV_SRC_ALPHA;
        pipelineDesc.states.blendState.renderTargets[0].blendOpAlpha = Renderer::BlendOp::ADD;
    }

    // Rasterizer state
    pipelineDesc.states.rasterizerState.cullMode = Renderer::CullMode::BACK;
    pipelineDesc.states.rasterizerState.frontFaceMode = Renderer::Settings::FRONT_FACE_STATE;

    // Render targets
    pipelineDesc.renderTargets[0] = params.rt0;
    pipelineDesc.depthStencil = params.depth;

    // Draw
    Renderer::GraphicsPipelineID pipeline = _renderer->CreatePipeline(pipelineDesc); // This will compile the pipeline and return the ID, or just return ID of cached pipeline
    commandList.BeginPipeline(pipeline);

    commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, params.globalDescriptorSet, frameIndex);
    commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::MODEL, params.drawDescriptorSet, frameIndex);

    commandList.SetIndexBuffer(_indices.GetBuffer(), Renderer::IndexFormat::UInt16);

    if (params.cullingEnabled)
    {
        u32 drawCountBufferOffset = params.drawCountIndex * sizeof(u32);
        commandList.DrawIndexedIndirectCount(params.argumentBuffer, 0, params.drawCountBuffer, drawCountBufferOffset, params.numMaxDrawCalls);
    }
    else
    {
        commandList.DrawIndexedIndirect(params.argumentBuffer, 0, params.numMaxDrawCalls);
    }

    commandList.EndPipeline(pipeline);
}