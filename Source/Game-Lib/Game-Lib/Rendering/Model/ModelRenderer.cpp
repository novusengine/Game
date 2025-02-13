#include "ModelRenderer.h"

#include "Game-Lib/Rendering/CullUtils.h"
#include "Game-Lib/Rendering/RenderUtils.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/RenderResources.h"
#include "Game-Lib/Rendering/Debug/DebugRenderer.h"
#include "Game-Lib/Rendering/Model/ModelLoader.h"
#include "Game-Lib/Util/ServiceLocator.h"
#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Components/Model.h"
#include "Game-Lib/ECS/Components/Tags.h"
#include "Game-Lib/ECS/Singletons/ClientDBCollection.h"
#include "Game-Lib/ECS/Singletons/TextureSingleton.h"
#include "Game-Lib/ECS/Util/Transforms.h"

#include <Base/CVarSystem/CVarSystem.h>

#include <FileFormat/Novus/Map/MapChunk.h>

#include <Input/InputManager.h>

#include <Renderer/Renderer.h>
#include <Renderer/RenderGraph.h>
#include <Renderer/Descriptors/ImageDesc.h>

#include <imgui/imgui.h>
#include <entt/entt.hpp>
#include <glm/gtx/euler_angles.hpp>

AutoCVar_Int CVAR_ModelRendererEnabled(CVarCategory::Client | CVarCategory::Rendering, "modelEnabled", "enable modelrendering", 1, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_ModelCullingEnabled(CVarCategory::Client | CVarCategory::Rendering, "modelCulling", "enable model culling", 1, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_ModelOcclusionCullingEnabled(CVarCategory::Client | CVarCategory::Rendering, "modelOcclusionCulling", "enable model occlusion culling", 1, CVarFlags::EditCheckbox);

AutoCVar_Int CVAR_ModelDisableTwoStepCulling(CVarCategory::Client | CVarCategory::Rendering, "modelDisableTwoStepCulling", "disable two step culling and force all drawcalls into the geometry pass", 0, CVarFlags::EditCheckbox);

AutoCVar_Int CVAR_ModelDrawOccluders(CVarCategory::Client | CVarCategory::Rendering, "modelDrawOccluders", "enable the draw command for occluders, the culling and everything else is unaffected", 1, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_ModelDrawGeometry(CVarCategory::Client | CVarCategory::Rendering, "modelDrawGeometry", "enable the draw command for geometry, the culling and everything else is unaffected", 1, CVarFlags::EditCheckbox);

AutoCVar_Int CVAR_ModelDrawOpaqueAABBs(CVarCategory::Client | CVarCategory::Rendering, "modelDrawOpaqueAABBs", "if enabled, the culling pass will debug draw all opaque AABBs", 0, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_ModelDrawTransparentAABBs(CVarCategory::Client | CVarCategory::Rendering, "modelDrawTransparentAABBs", "if enabled, the culling pass will debug draw all transparent AABBs", 0, CVarFlags::EditCheckbox);

AutoCVar_Int CVAR_ModelValidateTransfers(CVarCategory::Client | CVarCategory::Rendering, "modelValidateGPUVectors", "if enabled ON START we will validate GPUVector uploads", 0, CVarFlags::EditCheckbox);

AutoCVar_Int CVAR_ModelsCastShadow(CVarCategory::Client | CVarCategory::Rendering, "shadowModelsCastShadow", "should Models cast shadows", 1, CVarFlags::EditCheckbox);

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
        _textureTransformMatrices.SetValidation(true);

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
    ZoneScopedN("ModelRenderer::Update");

    if (!CVAR_ModelRendererEnabled.Get())
        return;

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;

    {
        ZoneScopedN("Update Transform Matrices");

        registry->view<ECS::Components::Transform, ECS::Components::Model, ECS::Components::DirtyTransform>().each([&](entt::entity entity, ECS::Components::Transform& transform, ECS::Components::Model& model, ECS::Components::DirtyTransform& dirtyTransform)
        {
            u32 instanceID = model.instanceID;
            if (instanceID == std::numeric_limits<u32>::max())
            {
                return;
            }

            mat4x4& matrix = _instanceMatrices[instanceID];

            matrix = transform.GetMatrix();
            _instanceMatrices.SetDirtyElement(instanceID);
        });
    }

    {
        ZoneScopedN("Update Culling Resources");

        const bool cullingEnabled = CVAR_ModelCullingEnabled.Get();
        _opaqueCullingResources.Update(deltaTime, cullingEnabled);
        _transparentCullingResources.Update(deltaTime, cullingEnabled);

        _opaqueSkyboxCullingResources.Update(deltaTime, false);
        _transparentSkyboxCullingResources.Update(deltaTime, false);
    }

    u32 numTextureLoads = static_cast<u32>(_textureLoadRequests.try_dequeue_bulk(_textureLoadWork.begin(), 256));
    if (numTextureLoads > 0)
    {
        ZoneScopedN("Texture Load Requests");
    
        auto& textureSingleton = registry->ctx().get<ECS::Singletons::TextureSingleton>();
    
        Renderer::TextureDesc textureDesc;
        textureDesc.path.reserve(128);
    
        {
            ZoneScopedN("Texture Loading");
    
            for (u32 i = 0; i < numTextureLoads; i++)
            {
                ZoneScopedN("Texture Load");
                const TextureLoadRequest& textureLoad = _textureLoadWork[i];
                TextureUnit& textureUnit = _textureUnits[textureLoad.textureUnitOffset];
    
                if (!textureSingleton.textureHashToPath.contains(textureLoad.textureHash))
                    continue;
    
                textureDesc.path = textureSingleton.textureHashToPath[textureLoad.textureHash];
    
                Renderer::TextureID textureID = _renderer->LoadTextureIntoArray(textureDesc, _textures, textureUnit.textureIds[textureLoad.textureIndex]);
                NC_ASSERT(textureUnit.textureIds[textureLoad.textureIndex] < Renderer::Settings::MAX_TEXTURES, "ModelRenderer : LoadModel overflowed the {0} textures we have support for", Renderer::Settings::MAX_TEXTURES);
    
                _dirtyTextureUnitOffsets.insert(textureLoad.textureUnitOffset);
            }
        }
    
        {
            ZoneScopedN("Set Dirty Texture Units");
    
            for (u32 textureUnitOffset : _dirtyTextureUnitOffsets)
            {
                _textureUnits.SetDirtyElement(textureUnitOffset);
            }
    
            _dirtyTextureUnitOffsets.clear();
        }
    }

    u32 numChangeGroupRequests = static_cast<u32>(_changeGroupRequests.try_dequeue_bulk(_changeGroupWork.begin(), 256));
    if (numChangeGroupRequests > 0)
    {
        ZoneScopedN("Change Group Requests");

        for (u32 i = 0; i < numChangeGroupRequests; i++)
        {
            ChangeGroupRequest& changeGroupRequest = _changeGroupWork[i];

            InstanceManifest& instanceManifest = _instanceManifests[changeGroupRequest.instanceID];

            if (changeGroupRequest.enable)
            {
                instanceManifest.enabledGroupIDs.insert(changeGroupRequest.groupIDStart);
            }
            else
            {
                if (changeGroupRequest.groupIDEnd == 0)
                {
                    instanceManifest.enabledGroupIDs.erase(changeGroupRequest.groupIDStart);
                }
                else
                {
                    for (auto it = instanceManifest.enabledGroupIDs.begin(); it != instanceManifest.enabledGroupIDs.end();)
                    {
                        u32 groupID = *it;
                        if (groupID >= changeGroupRequest.groupIDStart && groupID <= changeGroupRequest.groupIDEnd)
                        {
                            it = instanceManifest.enabledGroupIDs.erase(it);
                        }
                        else
                        {
                            ++it;
                        }
                    }
                }
            }
        }

        _instancesDirty = true;
    }

    CompactInstanceRefs();
    SyncToGPU();
    _instancesDirty = false;
}

void ModelRenderer::Clear()
{
    ZoneScoped;

    _modelManifests.clear();
    _modelIDToNumInstances.clear();

    _cullingDatas.Clear();
    _vertices.Clear();
    _indices.Clear();

    _instanceManifests.clear();
    _instanceDatas.Clear();
    _instanceMatrices.Clear();

    _textureDatas.Clear();
    _textureUnits.Clear();

    _displayInfoManifests.clear();

    _boneMatrices.Clear();
    _textureTransformMatrices.Clear();

    _animatedVertices.Clear(false);
    _animatedVerticesIndex.store(0);

    _modelDecorationSets.clear();
    _modelDecorations.clear();

    _opaqueCullingResources.Clear();
    _transparentCullingResources.Clear();

    _opaqueSkyboxCullingResources.Clear();
    _transparentSkyboxCullingResources.Clear();

    TextureLoadRequest textureLoadRequest;
    while (_textureLoadRequests.try_dequeue(textureLoadRequest)) {}
    _dirtyTextureUnitOffsets.clear();

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

    if (_opaqueCullingResources.GetDrawCalls().Count() == 0)
        return;

    CVarSystem* cvarSystem = CVarSystem::Get();

    u32 numCascades = 0;
    if (CVAR_ModelsCastShadow.Get() == 1)
    {
        numCascades = *cvarSystem->GetIntCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowCascadeNum");
    }

    struct Data
    {
        Renderer::ImageMutableResource visibilityBuffer;
        Renderer::DepthImageMutableResource depth[Renderer::Settings::MAX_VIEWS];

        Renderer::BufferMutableResource culledDrawCallsBuffer;
        Renderer::BufferMutableResource culledDrawCallCountBuffer;

        Renderer::BufferMutableResource culledDrawCallsBitMaskBuffer;
        Renderer::BufferMutableResource prevCulledDrawCallsBitMaskBuffer;

        Renderer::BufferMutableResource culledInstanceCountsBuffer;

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
        [this, &resources, frameIndex, numCascades](Data& data, Renderer::RenderGraphBuilder& builder)
        {
            using BufferUsage = Renderer::BufferPassUsage;

            data.visibilityBuffer = builder.Write(resources.visibilityBuffer, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);
            data.depth[0] = builder.Write(resources.depth, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);
            for (u32 i = 1; i < numCascades + 1; i++)
            {
                data.depth[i] = builder.Write(resources.shadowDepthCascades[i - 1], Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);
            }

            builder.Read(resources.cameras.GetBuffer(), BufferUsage::GRAPHICS | BufferUsage::COMPUTE);
            builder.Read(_vertices.GetBuffer(), BufferUsage::GRAPHICS);
            builder.Read(_indices.GetBuffer(), BufferUsage::GRAPHICS);
            builder.Read(_textureDatas.GetBuffer(), BufferUsage::GRAPHICS);
            builder.Read(_textureUnits.GetBuffer(), BufferUsage::GRAPHICS);
            builder.Read(_instanceDatas.GetBuffer(), BufferUsage::GRAPHICS);
            builder.Read(_instanceMatrices.GetBuffer(), BufferUsage::GRAPHICS);
            builder.Read(_boneMatrices.GetBuffer(), BufferUsage::GRAPHICS | BufferUsage::COMPUTE);
            builder.Read(_textureTransformMatrices.GetBuffer(), BufferUsage::GRAPHICS | BufferUsage::COMPUTE);

            OccluderPassSetup(data, builder, &_opaqueCullingResources, frameIndex);
            data.culledDrawCallCountBuffer = builder.Write(_opaqueCullingResources.GetCulledDrawCallCountBuffer(), BufferUsage::TRANSFER | BufferUsage::COMPUTE | BufferUsage::GRAPHICS);
            data.culledInstanceCountsBuffer = builder.Write(_opaqueCullingResources.GetCulledInstanceCountsBuffer(), BufferUsage::TRANSFER | BufferUsage::COMPUTE);

            builder.Read(_opaqueCullingResources.GetDrawCallDatas().GetBuffer(), BufferUsage::GRAPHICS | BufferUsage::COMPUTE);

            builder.Write(_animatedVertices.GetBuffer(), BufferUsage::GRAPHICS | BufferUsage::COMPUTE);

            data.globalSet = builder.Use(resources.globalDescriptorSet);

            return true; // Return true from setup to enable this pass, return false to disable it
        },
        [this, &resources, frameIndex, numCascades, cvarSystem](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, ModelOccluders);

            CulledRenderer::OccluderPassParams params;
            params.passName = "Opaque";
            params.graphResources = &graphResources;
            params.commandList = &commandList;
            params.cullingResources = &_opaqueCullingResources;

            params.frameIndex = frameIndex;
            params.rt0 = data.visibilityBuffer;
            for (u32 i = 0; i < numCascades + 1; i++)
            {
                params.depth[i] = data.depth[i];
            }

            params.culledDrawCallsBuffer = data.culledDrawCallsBuffer;
            params.culledDrawCallCountBuffer = data.culledDrawCallCountBuffer;
            params.culledDrawCallsBitMaskBuffer = data.culledDrawCallsBitMaskBuffer;
            params.prevCulledDrawCallsBitMaskBuffer = data.prevCulledDrawCallsBitMaskBuffer;
            params.culledInstanceCountsBuffer = data.culledInstanceCountsBuffer;

            params.drawCountBuffer = data.drawCountBuffer;
            params.triangleCountBuffer = data.triangleCountBuffer;
            params.drawCountReadBackBuffer = data.drawCountReadBackBuffer;
            params.triangleCountReadBackBuffer = data.triangleCountReadBackBuffer;

            params.globalDescriptorSet = data.globalSet;
            params.occluderFillDescriptorSet = data.occluderFillSet;
            params.createIndirectDescriptorSet = data.createIndirectDescriptorSet;
            params.drawDescriptorSet = data.drawSet;

            params.drawCallback = [&](const DrawParams& drawParams)
            {
                Draw(resources, frameIndex, graphResources, commandList, drawParams);
            };

            params.baseInstanceLookupOffset = offsetof(DrawCallData, DrawCallData::baseInstanceLookupOffset);
            params.drawCallDataSize = sizeof(DrawCallData);
            
            params.numCascades = numCascades;

            params.biasConstantFactor = static_cast<f32>(*cvarSystem->GetFloatCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowDepthBiasConstant"));
            params.biasClamp = static_cast<f32>(*cvarSystem->GetFloatCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowDepthBiasClamp"));
            params.biasSlopeFactor = static_cast<f32>(*cvarSystem->GetFloatCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowDepthBiasSlope"));

            params.enableDrawing = CVAR_ModelDrawOccluders.Get();
            params.disableTwoStepCulling = CVAR_ModelDisableTwoStepCulling.Get();
            params.isIndexed = true;
            params.useInstancedCulling = true;

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

    if (_opaqueCullingResources.GetDrawCalls().Count() == 0)
        return;

    struct Data
    {
        Renderer::ImageResource depthPyramid;

        Renderer::BufferResource prevCulledDrawCallsBitMask;

        Renderer::BufferMutableResource currentCulledDrawCallsBitMask;
        Renderer::BufferMutableResource culledInstanceCountsBuffer;
        Renderer::BufferMutableResource culledDrawCallsBuffer;
        Renderer::BufferMutableResource culledDrawCallCountBuffer;
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
            data.prevCulledDrawCallsBitMask = builder.Read(_opaqueCullingResources.GetCulledDrawCallsBitMaskBuffer(!frameIndex), BufferUsage::COMPUTE);
            data.currentCulledDrawCallsBitMask = builder.Write(_opaqueCullingResources.GetCulledDrawCallsBitMaskBuffer(frameIndex), BufferUsage::COMPUTE);

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
            params.culledInstanceCountsBuffer = data.culledInstanceCountsBuffer;
            params.culledDrawCallsBuffer = data.culledDrawCallsBuffer;
            params.culledDrawCallCountBuffer = data.culledDrawCallCountBuffer;

            params.drawCountBuffer = data.drawCountBuffer;
            params.triangleCountBuffer = data.triangleCountBuffer;
            params.drawCountReadBackBuffer = data.drawCountReadBackBuffer;
            params.triangleCountReadBackBuffer = data.triangleCountReadBackBuffer;

            params.debugDescriptorSet = data.debugSet;
            params.globalDescriptorSet = data.globalSet;
            params.cullingDescriptorSet = data.cullingSet;

            params.numCascades = *CVarSystem::Get()->GetIntCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowCascadeNum"_h);
            params.occlusionCull = CVAR_ModelOcclusionCullingEnabled.Get();
            params.disableTwoStepCulling = CVAR_ModelDisableTwoStepCulling.Get();

            params.cullingDataIsWorldspace = false;
            params.debugDrawColliders = CVAR_ModelDrawOpaqueAABBs.Get();

            params.baseInstanceLookupOffset = offsetof(DrawCallData, baseInstanceLookupOffset);
            params.modelIDOffset = offsetof(DrawCallData, modelID);
            params.drawCallDataSize = sizeof(DrawCallData);

            params.useInstancedCulling = true;

            CullingPass(params);
        });
}

void ModelRenderer::AddGeometryPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    ZoneScoped;

    if (!CVAR_ModelRendererEnabled.Get())
        return;

    if (_opaqueCullingResources.GetDrawCalls().Count() == 0)
        return;

    CVarSystem* cvarSystem = CVarSystem::Get();

    const bool cullingEnabled = CVAR_ModelCullingEnabled.Get();
    u32 numCascades = 0;
    if (CVAR_ModelsCastShadow.Get() == 1)
    {
        numCascades = *cvarSystem->GetIntCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowCascadeNum");
    }

    struct Data
    {
        Renderer::ImageMutableResource visibilityBuffer;
        Renderer::DepthImageMutableResource depth[Renderer::Settings::MAX_VIEWS];

        Renderer::BufferMutableResource drawCallsBuffer;
        Renderer::BufferMutableResource culledDrawCallsBuffer;
        Renderer::BufferMutableResource culledDrawCallCountBuffer;
        Renderer::BufferMutableResource culledDrawCallsBitMaskBuffer;
        Renderer::BufferMutableResource prevCulledDrawCallsBitMaskBuffer;

        Renderer::BufferMutableResource drawCountBuffer;
        Renderer::BufferMutableResource triangleCountBuffer;
        Renderer::BufferMutableResource drawCountReadBackBuffer;
        Renderer::BufferMutableResource triangleCountReadBackBuffer;

        Renderer::DescriptorSetResource globalSet;
        Renderer::DescriptorSetResource fillSet;
        Renderer::DescriptorSetResource drawSet;
    };

    renderGraph->AddPass<Data>("Model (O) Geometry",
        [this, &resources, frameIndex, numCascades](Data& data, Renderer::RenderGraphBuilder& builder)
        {
            using BufferUsage = Renderer::BufferPassUsage;

            data.visibilityBuffer = builder.Write(resources.visibilityBuffer, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);
            data.depth[0] = builder.Write(resources.depth, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);
            for (u32 i = 1; i < numCascades + 1; i++)
            {
                data.depth[i] = builder.Write(resources.shadowDepthCascades[i - 1], Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);
            }

            builder.Read(resources.cameras.GetBuffer(), BufferUsage::GRAPHICS  | BufferUsage::COMPUTE);
            builder.Read(_vertices.GetBuffer(), BufferUsage::GRAPHICS);
            builder.Read(_indices.GetBuffer(), BufferUsage::GRAPHICS);
            builder.Read(_textureDatas.GetBuffer(), BufferUsage::GRAPHICS);
            builder.Read(_textureUnits.GetBuffer(), BufferUsage::GRAPHICS);
            builder.Read(_instanceDatas.GetBuffer(), BufferUsage::GRAPHICS);
            builder.Read(_instanceMatrices.GetBuffer(), BufferUsage::GRAPHICS);
            builder.Read(_boneMatrices.GetBuffer(), BufferUsage::GRAPHICS | BufferUsage::COMPUTE);
            builder.Read(_textureTransformMatrices.GetBuffer(), BufferUsage::GRAPHICS | BufferUsage::COMPUTE);
            
            builder.Write(_animatedVertices.GetBuffer(), BufferUsage::GRAPHICS | BufferUsage::COMPUTE);

            GeometryPassSetup(data, builder, &_opaqueCullingResources, frameIndex);
            data.culledDrawCallsBitMaskBuffer = builder.Write(_opaqueCullingResources.GetCulledDrawCallsBitMaskBuffer(frameIndex), BufferUsage::TRANSFER | BufferUsage::GRAPHICS | BufferUsage::COMPUTE);
            data.prevCulledDrawCallsBitMaskBuffer = builder.Write(_opaqueCullingResources.GetCulledDrawCallsBitMaskBuffer(!frameIndex), BufferUsage::TRANSFER | BufferUsage::GRAPHICS | BufferUsage::COMPUTE);

            builder.Read(_opaqueCullingResources.GetDrawCallDatas().GetBuffer(), BufferUsage::GRAPHICS);

            data.globalSet = builder.Use(resources.globalDescriptorSet);
            data.fillSet = builder.Use(_opaqueCullingResources.GetGeometryFillDescriptorSet());

            return true; // Return true from setup to enable this pass, return false to disable it
        },
        [this, &resources, frameIndex, cullingEnabled, numCascades, cvarSystem](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList)
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, ModelGeometry);

            CulledRenderer::GeometryPassParams params;
            params.passName = "Opaque";
            params.graphResources = &graphResources;
            params.commandList = &commandList;
            params.cullingResources = &_opaqueCullingResources;

            params.frameIndex = frameIndex;
            params.rt0 = data.visibilityBuffer;
            for (u32 i = 0; i < numCascades + 1; i++)
            {
                params.depth[i] = data.depth[i];
            }

            params.drawCallsBuffer = data.drawCallsBuffer;
            params.culledDrawCallsBuffer = data.culledDrawCallsBuffer;
            params.culledDrawCallCountBuffer = data.culledDrawCallCountBuffer;
            params.culledDrawCallsBitMaskBuffer = data.culledDrawCallsBitMaskBuffer;
            params.prevCulledDrawCallsBitMaskBuffer = data.prevCulledDrawCallsBitMaskBuffer;

            params.drawCountBuffer = data.drawCountBuffer;
            params.triangleCountBuffer = data.triangleCountBuffer;
            params.drawCountReadBackBuffer = data.drawCountReadBackBuffer;
            params.triangleCountReadBackBuffer = data.triangleCountReadBackBuffer;

            params.globalDescriptorSet = data.globalSet;
            params.fillDescriptorSet = data.fillSet;
            params.drawDescriptorSet = data.drawSet;

            params.drawCallback = [&](const DrawParams& drawParams)
            {
                Draw(resources, frameIndex, graphResources, commandList, drawParams);
            };

            params.numCascades = numCascades;

            params.biasConstantFactor = static_cast<f32>(*cvarSystem->GetFloatCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowDepthBiasConstant"));
            params.biasClamp = static_cast<f32>(*cvarSystem->GetFloatCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowDepthBiasClamp"));
            params.biasSlopeFactor = static_cast<f32>(*cvarSystem->GetFloatCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowDepthBiasSlope"));

            params.enableDrawing = CVAR_ModelDrawGeometry.Get();
            params.cullingEnabled = cullingEnabled;
            params.useInstancedCulling = true;
            params.isIndexed = true;

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

    if (_transparentCullingResources.GetDrawCalls().Count() == 0)
        return;

    u32 numCascades = 0;// *CVarSystem::Get()->GetIntCVar(CVarCategory::Client | CVarCategory::Rendering, "numShadowCascades"_h);

    struct Data
    {
        Renderer::ImageResource depthPyramid;

        Renderer::BufferMutableResource culledInstanceCountsBuffer;
        Renderer::BufferMutableResource culledDrawCallsBuffer;
        Renderer::BufferMutableResource culledDrawCallCountBuffer;
        Renderer::BufferMutableResource drawCountBuffer;
        Renderer::BufferMutableResource triangleCountBuffer;
        Renderer::BufferMutableResource drawCountReadBackBuffer;
        Renderer::BufferMutableResource triangleCountReadBackBuffer;

        Renderer::DescriptorSetResource debugSet;
        Renderer::DescriptorSetResource globalSet;
        Renderer::DescriptorSetResource cullingSet;
    };

    renderGraph->AddPass<Data>("Model (T) Culling",
        [this, &resources, frameIndex](Data& data, Renderer::RenderGraphBuilder& builder) // Setup
        {
            ZoneScoped;
            using BufferUsage = Renderer::BufferPassUsage;

            data.depthPyramid = builder.Read(resources.depthPyramid, Renderer::PipelineType::COMPUTE);

            builder.Read(resources.cameras.GetBuffer(), BufferUsage::COMPUTE);
            builder.Read(_cullingDatas.GetBuffer(), BufferUsage::COMPUTE);
            builder.Read(_instanceMatrices.GetBuffer(), BufferUsage::COMPUTE);

            CullingPassSetup(data, builder, &_transparentCullingResources, frameIndex);
            builder.Read(_transparentCullingResources.GetDrawCallDatas().GetBuffer(), BufferUsage::COMPUTE);

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

            params.culledInstanceCountsBuffer = data.culledInstanceCountsBuffer;
            params.culledDrawCallsBuffer = data.culledDrawCallsBuffer;
            params.culledDrawCallCountBuffer = data.culledDrawCallCountBuffer;

            params.drawCountBuffer = data.drawCountBuffer;
            params.triangleCountBuffer = data.triangleCountBuffer;
            params.drawCountReadBackBuffer = data.drawCountReadBackBuffer;
            params.triangleCountReadBackBuffer = data.triangleCountReadBackBuffer;

            params.debugDescriptorSet = data.debugSet;
            params.globalDescriptorSet = data.globalSet;
            params.cullingDescriptorSet = data.cullingSet;

            params.numCascades = 0;// *CVarSystem::Get()->GetIntCVar(CVarCategory::Client | CVarCategory::Rendering, "numShadowCascades"_h);
            params.occlusionCull = CVAR_ModelOcclusionCullingEnabled.Get();
            params.disableTwoStepCulling = true; // Transparent objects don't write depth, so we don't need to two step cull them

            params.cullingDataIsWorldspace = false;
            params.debugDrawColliders = CVAR_ModelDrawTransparentAABBs.Get();

            params.baseInstanceLookupOffset = offsetof(DrawCallData, baseInstanceLookupOffset);
            params.modelIDOffset = offsetof(DrawCallData, modelID);
            params.drawCallDataSize = sizeof(DrawCallData);

            params.useInstancedCulling = true;

            CullingPass(params);
        });
}

void ModelRenderer::AddTransparencyGeometryPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    ZoneScoped;

    if (!CVAR_ModelRendererEnabled.Get())
        return;

    if (_transparentCullingResources.GetDrawCalls().Count() == 0)
        return;

    const bool cullingEnabled = CVAR_ModelCullingEnabled.Get();

    struct Data
    {
        Renderer::ImageMutableResource transparency;
        Renderer::ImageMutableResource transparencyWeights;
        Renderer::DepthImageMutableResource depth;

        Renderer::BufferMutableResource drawCallsBuffer;
        Renderer::BufferMutableResource culledDrawCallsBuffer;
        Renderer::BufferMutableResource culledDrawCallCountBuffer;

        Renderer::BufferMutableResource drawCountBuffer;
        Renderer::BufferMutableResource triangleCountBuffer;
        Renderer::BufferMutableResource drawCountReadBackBuffer;
        Renderer::BufferMutableResource triangleCountReadBackBuffer;

        Renderer::DescriptorSetResource globalSet;
        Renderer::DescriptorSetResource fillSet;
        Renderer::DescriptorSetResource drawSet;
    };

    renderGraph->AddPass<Data>("Model (T) Geometry",
        [this, &resources, frameIndex](Data& data, Renderer::RenderGraphBuilder& builder)
        {
            using BufferUsage = Renderer::BufferPassUsage;

            data.transparency = builder.Write(resources.transparency, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);
            data.transparencyWeights = builder.Write(resources.transparencyWeights, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);
            data.depth = builder.Write(resources.depth, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);

            builder.Read(resources.cameras.GetBuffer(), BufferUsage::GRAPHICS);
            builder.Read(_vertices.GetBuffer(), BufferUsage::GRAPHICS);
            builder.Read(_indices.GetBuffer(), BufferUsage::GRAPHICS);
            builder.Read(_textureDatas.GetBuffer(), BufferUsage::GRAPHICS);
            builder.Read(_textureUnits.GetBuffer(), BufferUsage::GRAPHICS);
            builder.Read(_instanceDatas.GetBuffer(), BufferUsage::GRAPHICS);
            builder.Read(_instanceMatrices.GetBuffer(), BufferUsage::GRAPHICS);
            builder.Read(_boneMatrices.GetBuffer(), BufferUsage::GRAPHICS | BufferUsage::COMPUTE);
            builder.Read(_textureTransformMatrices.GetBuffer(), BufferUsage::GRAPHICS | BufferUsage::COMPUTE);

            builder.Write(_animatedVertices.GetBuffer(), BufferUsage::GRAPHICS | BufferUsage::COMPUTE);

            GeometryPassSetup(data, builder, &_transparentCullingResources, frameIndex);
            builder.Read(_transparentCullingResources.GetDrawCallDatas().GetBuffer(), BufferUsage::GRAPHICS);

            data.globalSet = builder.Use(resources.globalDescriptorSet);
            data.fillSet = builder.Use(_transparentCullingResources.GetGeometryFillDescriptorSet());

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
            params.depth[0] = data.depth;

            params.drawCallsBuffer = data.drawCallsBuffer;
            params.culledDrawCallsBuffer = data.culledDrawCallsBuffer;
            params.culledDrawCallCountBuffer = data.culledDrawCallCountBuffer;

            params.drawCountBuffer = data.drawCountBuffer;
            params.triangleCountBuffer = data.triangleCountBuffer;
            params.drawCountReadBackBuffer = data.drawCountReadBackBuffer;
            params.triangleCountReadBackBuffer = data.triangleCountReadBackBuffer;

            params.globalDescriptorSet = data.globalSet;
            params.fillDescriptorSet = data.fillSet;
            params.drawDescriptorSet = data.drawSet;

            params.drawCallback = [&](const DrawParams& drawParams)
            {
                DrawTransparent(resources, frameIndex, graphResources, commandList, drawParams);
            };

            params.numCascades = 0;

            params.enableDrawing = CVAR_ModelDrawGeometry.Get();
            params.cullingEnabled = cullingEnabled;
            params.useInstancedCulling = true;
            params.isIndexed = true;

            GeometryPass(params);
        });
}

void ModelRenderer::AddSkyboxPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    ZoneScoped;

    if (!CVAR_ModelRendererEnabled.Get())
        return;

    if (_opaqueSkyboxCullingResources.GetDrawCalls().Count() > 0)
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
                builder.Read(_textureDatas.GetBuffer(), BufferUsage::GRAPHICS);
                builder.Read(_textureUnits.GetBuffer(), BufferUsage::GRAPHICS);
                builder.Read(_instanceDatas.GetBuffer(), BufferUsage::GRAPHICS);
                builder.Read(_instanceMatrices.GetBuffer(), BufferUsage::GRAPHICS);
                builder.Read(_boneMatrices.GetBuffer(), BufferUsage::GRAPHICS);
                builder.Read(_textureTransformMatrices.GetBuffer(), BufferUsage::GRAPHICS | BufferUsage::COMPUTE);
                builder.Read(_opaqueSkyboxCullingResources.GetDrawCallDatas().GetBuffer(), BufferUsage::GRAPHICS);

                builder.Write(_animatedVertices.GetBuffer(), BufferUsage::GRAPHICS);

                data.drawCallsBuffer = builder.Write(_opaqueSkyboxCullingResources.GetDrawCalls().GetBuffer(), BufferUsage::GRAPHICS);
                data.culledDrawCallsBuffer = builder.Write(_opaqueSkyboxCullingResources.GetCulledDrawsBuffer(), BufferUsage::GRAPHICS);
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
                params.depth[0] = data.depth;

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
                params.numCascades = 0;// *CVarSystem::Get()->GetIntCVar(CVarCategory::Client | CVarCategory::Rendering, "numShadowCascades"_h);

                GeometryPass(params);
            });
    }

    if (_transparentSkyboxCullingResources.GetDrawCalls().Count() > 0)
    {
        struct Data
        {
            Renderer::ImageMutableResource transparency;
            Renderer::ImageMutableResource transparencyWeights;
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

                data.transparency = builder.Write(resources.transparency, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);
                data.transparencyWeights = builder.Write(resources.transparencyWeights, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);
                data.depth = builder.Write(resources.skyboxDepth, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);

                builder.Read(resources.cameras.GetBuffer(), BufferUsage::GRAPHICS);
                builder.Read(_vertices.GetBuffer(), BufferUsage::GRAPHICS);
                builder.Read(_indices.GetBuffer(), BufferUsage::GRAPHICS);
                builder.Read(_textureUnits.GetBuffer(), BufferUsage::GRAPHICS);
                builder.Read(_instanceDatas.GetBuffer(), BufferUsage::GRAPHICS);
                builder.Read(_instanceMatrices.GetBuffer(), BufferUsage::GRAPHICS);
                builder.Read(_boneMatrices.GetBuffer(), BufferUsage::GRAPHICS);
                builder.Read(_textureTransformMatrices.GetBuffer(), BufferUsage::GRAPHICS | BufferUsage::COMPUTE);
                builder.Read(_transparentSkyboxCullingResources.GetDrawCallDatas().GetBuffer(), BufferUsage::GRAPHICS);

                builder.Write(_animatedVertices.GetBuffer(), BufferUsage::GRAPHICS);

                data.drawCallsBuffer = builder.Write(_transparentSkyboxCullingResources.GetDrawCalls().GetBuffer(), BufferUsage::GRAPHICS);
                data.culledDrawCallsBuffer = builder.Write(_transparentSkyboxCullingResources.GetCulledDrawsBuffer(), BufferUsage::GRAPHICS);
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
                params.rt0 = data.transparency;
                params.rt1 = data.transparencyWeights;
                params.depth[0] = data.depth;

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
                params.numCascades = 0;// *CVarSystem::Get()->GetIntCVar(CVarCategory::Client | CVarCategory::Rendering, "numShadowCascades"_h);

                GeometryPass(params);
            });
    }
}

void ModelRenderer::RegisterMaterialPassBufferUsage(Renderer::RenderGraphBuilder& builder)
{
    using BufferUsage = Renderer::BufferPassUsage;

    builder.Read(_opaqueCullingResources.GetDrawCalls().GetBuffer(), BufferUsage::COMPUTE);
    builder.Read(_opaqueCullingResources.GetDrawCallDatas().GetBuffer(), BufferUsage::COMPUTE);
    builder.Read(_opaqueCullingResources.GetInstanceRefs().GetBuffer(), BufferUsage::COMPUTE);
    builder.Read(_vertices.GetBuffer(), BufferUsage::COMPUTE);
    builder.Read(_indices.GetBuffer(), BufferUsage::COMPUTE);
    builder.Read(_textureDatas.GetBuffer(), BufferUsage::COMPUTE);
    builder.Read(_textureUnits.GetBuffer(), BufferUsage::COMPUTE);
    builder.Read(_instanceDatas.GetBuffer(), BufferUsage::COMPUTE);
    builder.Read(_instanceMatrices.GetBuffer(), BufferUsage::COMPUTE);
    builder.Read(_boneMatrices.GetBuffer(), BufferUsage::COMPUTE);
    builder.Read(_textureTransformMatrices.GetBuffer(), BufferUsage::COMPUTE);
    builder.Write(_animatedVertices.GetBuffer(), BufferUsage::COMPUTE);
}

void ModelRenderer::Reserve(const ReserveInfo& reserveInfo)
{
    _instanceManifests.reserve(_instanceManifests.size() + reserveInfo.numInstances);
    _instanceDatas.Reserve(reserveInfo.numInstances);
    _instanceMatrices.Reserve(reserveInfo.numInstances);

    _cullingDatas.Reserve(reserveInfo.numModels);
    _modelIDToNumInstances.reserve(_modelIDToNumInstances.size() + reserveInfo.numModels);
    _modelManifests.reserve(_modelManifests.size() + reserveInfo.numModels);

    _vertices.Reserve(reserveInfo.numVertices);
    _indices.Reserve(reserveInfo.numIndices);

    _textureDatas.Reserve(reserveInfo.numTextureUnits); // This might not be accurate
    _textureUnits.Reserve(reserveInfo.numTextureUnits);

    _boneMatrices.Reserve(reserveInfo.numBones);
        
    _textureTransformMatrices.Reserve(reserveInfo.numTextureTransforms);

    _modelDecorationSets.reserve(_modelDecorationSets.size() + reserveInfo.numDecorationSets);
    _modelDecorations.reserve(_modelDecorations.size() + reserveInfo.numDecorations);

    _opaqueCullingResources.Reserve(reserveInfo.numOpaqueDrawcalls);
    _transparentCullingResources.Reserve(reserveInfo.numTransparentDrawcalls);

    _opaqueSkyboxCullingResources.Reserve(reserveInfo.numOpaqueDrawcalls);
    _transparentSkyboxCullingResources.Reserve(reserveInfo.numTransparentDrawcalls);
}

u32 ModelRenderer::LoadModel(const std::string& name, Model::ComplexModel& model)
{
    ZoneScopedN("ModelRenderer::LoadModel");

    EnttRegistries* registries = ServiceLocator::GetEnttRegistries();

    entt::registry* registry = registries->gameRegistry;

    entt::registry::context& ctx = registry->ctx();
    auto& textureSingleton = ctx.get<ECS::Singletons::TextureSingleton>();

    ModelOffsets modelOffsets;
    AllocateModel(model, modelOffsets);

    TextureUnitOffsets textureUnitsOffsets;
    AllocateTextureUnits(model, textureUnitsOffsets);

    // Add ModelManifest
    ModelManifest& modelManifest = _modelManifests[modelOffsets.modelIndex];
    modelManifest.debugName = name;

    // Add CullingData
    {
        ZoneScopedN("Add Culling Data");

        Model::ComplexModel::CullingData& cullingData = _cullingDatas[modelOffsets.modelIndex];
        cullingData = model.cullingData;
    }

    // Add vertices
    {
        ZoneScopedN("Add Vertex Data");

        modelManifest.numVertices = model.modelHeader.numVertices;
        modelManifest.vertexOffset = modelOffsets.verticesStartIndex;

        if (modelManifest.numVertices)
        {
            u32 numModelVertices = static_cast<u32>(model.vertices.size());
            assert(modelManifest.numVertices == numModelVertices);

            void* dst = &_vertices[modelManifest.vertexOffset];
            void* src = model.vertices.data();
            size_t size = sizeof(Model::ComplexModel::Vertex) * numModelVertices;

            if (modelManifest.vertexOffset + numModelVertices > _vertices.Count())
            {
                NC_LOG_CRITICAL("ModelRenderer : Tried to memcpy vertices outside array");
            }

            memcpy(dst, src, size);
        }
    }

    // Add indices
    {
        ZoneScopedN("Add Index Data");

        modelManifest.numIndices = model.modelHeader.numIndices;
        modelManifest.indexOffset = modelOffsets.indicesStartIndex;

        if (modelManifest.numIndices)
        {
            void* dst = &_indices[modelManifest.indexOffset];
            void* src = model.modelData.indices.data();
            size_t size = sizeof(u16) * model.modelData.indices.size();

            if (modelManifest.indexOffset + model.modelData.indices.size() > _indices.Count())
            {
                NC_LOG_CRITICAL("ModelRenderer : Tried to memcpy vertices outside array");
            }

            memcpy(dst, src, size);
        }
    }

    // Add TextureUnits and DrawCalls
    {
        ZoneScopedN("Add TextureUnits and DrawCalls");

        modelManifest.numOpaqueDrawCalls = model.modelHeader.numOpaqueRenderBatches;
        modelManifest.numTransparentDrawCalls = model.modelHeader.numTransparentRenderBatches;

        bool isSkybox = false; // TODO: In the flow, not dealing with this yet

        DrawCallOffsets drawCallOffsets;
        AllocateDrawCalls(modelOffsets.modelIndex, drawCallOffsets, isSkybox);

        modelManifest.opaqueDrawCallOffset = drawCallOffsets.opaqueDrawCallStartIndex;
        modelManifest.transparentDrawCallOffset = drawCallOffsets.transparentDrawCallStartIndex;

        CullingResourcesIndexed<DrawCallData>& opaqueCullingResources = (isSkybox) ? _opaqueSkyboxCullingResources : _opaqueCullingResources;
        CullingResourcesIndexed<DrawCallData>& transparentCullingResources = (isSkybox) ? _transparentSkyboxCullingResources : _transparentCullingResources;

        const Renderer::GPUVector<Renderer::IndexedIndirectDraw>& opaqueDrawCalls = opaqueCullingResources.GetDrawCalls();
        const Renderer::GPUVector<DrawCallData>& opaqueDrawCallDatas = opaqueCullingResources.GetDrawCallDatas();

        const Renderer::GPUVector<Renderer::IndexedIndirectDraw>& transparentDrawCalls = transparentCullingResources.GetDrawCalls();
        const Renderer::GPUVector<DrawCallData>& transparentDrawCallDatas = transparentCullingResources.GetDrawCallDatas();

        u32 numAddedIndices = 0;

        u32 numAddedOpaqueDrawCalls = 0;
        u32 numAddedTransparentDrawCalls = 0;

        u32 textureTransformLookupTableSize = static_cast<u32>(model.textureTransformLookupTable.size());

        u32 numRenderBatches = static_cast<u32>(model.modelData.renderBatches.size());

        modelManifest.opaqueDrawIDToTextureDataID.reserve(numRenderBatches);
        modelManifest.transparentDrawIDToTextureDataID.reserve(numRenderBatches);
        modelManifest.opaqueDrawIDToGroupID.reserve(numRenderBatches);
        modelManifest.transparentDrawIDToGroupID.reserve(numRenderBatches);

        u32 numTexturesInModel = static_cast<u32>(model.textures.size());

        u32 textureUnitIndex = 0;
        for (u32 renderBatchIndex = 0; renderBatchIndex < numRenderBatches; renderBatchIndex++)
        {
            auto& renderBatch = model.modelData.renderBatches[renderBatchIndex];

            u32 textureUnitStartIndex = textureUnitsOffsets.textureUnitsStartIndex + textureUnitIndex;
            u16 numUnlitTextureUnits = 0;

            for (u32 i = 0; i < renderBatch.textureUnits.size(); i++)
            {
                // Texture Unit
                u32 textureUnitOffset = textureUnitsOffsets.textureUnitsStartIndex + (textureUnitIndex++);
                TextureUnit& textureUnit = _textureUnits[textureUnitOffset];

                Model::ComplexModel::TextureUnit& cTextureUnit = renderBatch.textureUnits[i];
                Model::ComplexModel::Material& cMaterial = model.materials[cTextureUnit.materialIndex];

                u16 materialFlag = *reinterpret_cast<u16*>(&cMaterial.flags) << 5;
                u16 blendingMode = static_cast<u16>(cMaterial.blendingMode) << 11;

                textureUnit.data = static_cast<u16>(cTextureUnit.flags.IsProjectedTexture) | materialFlag | blendingMode;
                textureUnit.materialType = cTextureUnit.shaderID;

                u16 textureTransformID1 = MODEL_INVALID_TEXTURE_TRANSFORM_ID;
                if (cTextureUnit.textureTransformIndexStart < textureTransformLookupTableSize)
                    textureTransformID1 = model.textureTransformLookupTable[cTextureUnit.textureTransformIndexStart];

                u16 textureTransformID2 = MODEL_INVALID_TEXTURE_TRANSFORM_ID;
                if (cTextureUnit.textureCount > 1)
                    if (cTextureUnit.textureTransformIndexStart + 1u < textureTransformLookupTableSize)
                        textureTransformID2 = model.textureTransformLookupTable[cTextureUnit.textureTransformIndexStart + 1];

                textureUnit.textureTransformIds[0] = textureTransformID1;
                textureUnit.textureTransformIds[1] = textureTransformID2;

                numUnlitTextureUnits += (materialFlag & 0x2) > 0;

                // Textures
                for (u32 j = 0; j < cTextureUnit.textureCount && j < 2; j++)
                {
                    u16 textureIndex = model.textureIndexLookupTable[cTextureUnit.textureIndexStart + j];
                    if (textureIndex == 65535)
                        continue;

                    const Model::ComplexModel::Texture& cTexture = model.textures[textureIndex];
                    if (cTexture.type == Model::ComplexModel::Texture::Type::None && cTexture.textureHash != std::numeric_limits<u32>().max())
                    {
                        TextureLoadRequest textureLoadRequest =
                        {
                            .textureUnitOffset = textureUnitOffset,
                            .textureIndex = j,
                            .textureHash = cTexture.textureHash,
                        };

                        _textureLoadRequests.enqueue(textureLoadRequest);
                    }
                    else
                    {
                        continue;
                    }

                    u8 textureSamplerIndex = 0;

                    if (cTexture.flags.wrapX)
                        textureSamplerIndex |= 0x1;

                    if (cTexture.flags.wrapY)
                        textureSamplerIndex |= 0x2;

                    textureUnit.data |= textureSamplerIndex << (1 + (j * 2));
                }
            }

            // Draw Calls
            u32& numAddedDrawCalls = (renderBatch.isTransparent) ? numAddedTransparentDrawCalls : numAddedOpaqueDrawCalls;
            u32& drawCallOffset = (renderBatch.isTransparent) ? modelManifest.transparentDrawCallOffset : modelManifest.opaqueDrawCallOffset;

            u32 curDrawCallOffset = drawCallOffset + numAddedDrawCalls;

            Renderer::IndexedIndirectDraw& drawCall = (renderBatch.isTransparent) ? transparentDrawCalls[curDrawCallOffset] : opaqueDrawCalls[curDrawCallOffset];
            drawCall.indexCount = renderBatch.indexCount;
            drawCall.firstIndex = modelManifest.indexOffset + renderBatch.indexStart;
            drawCall.vertexOffset = modelManifest.vertexOffset + renderBatch.vertexStart;
            drawCall.firstInstance = 0; // Is set during Compact
            drawCall.instanceCount = 0; // Is set during Compact

            DrawCallData& drawCallData = (renderBatch.isTransparent) ? transparentDrawCallDatas[curDrawCallOffset] : opaqueDrawCallDatas[curDrawCallOffset];
            //drawCallData.baseInstanceLookupOffset = 0; // Is set during Compact
            drawCallData.modelID = modelOffsets.modelIndex;

            TextureDataOffsets textureDataOffsets;
            AllocateTextureData(1, textureDataOffsets);

            TextureData& textureData = _textureDatas[textureDataOffsets.textureDatasStartIndex];
            textureData.textureUnitOffset = textureUnitStartIndex;
            textureData.numTextureUnits = static_cast<u16>(renderBatch.textureUnits.size());
            textureData.numUnlitTextureUnits = numUnlitTextureUnits;

            // Add to map to go from drawID to textureDataID
            robin_hood::unordered_map<u32, u32>& drawIDToTextureDataID = (renderBatch.isTransparent) ? modelManifest.transparentDrawIDToTextureDataID : modelManifest.opaqueDrawIDToTextureDataID;
            drawIDToTextureDataID[curDrawCallOffset] = textureDataOffsets.textureDatasStartIndex;

            // Add to map to go from drawID to groupID
            robin_hood::unordered_map<u32, u32>& drawIDToGroupID = (renderBatch.isTransparent) ? modelManifest.transparentDrawIDToGroupID : modelManifest.opaqueDrawIDToGroupID;
            drawIDToGroupID[curDrawCallOffset] = renderBatch.groupID;

            numAddedDrawCalls++;
        }
    }

    // Set Animated Data
    {
        ZoneScopedN("Set Animation Data");

        modelManifest.numBones = static_cast<u32>(model.bones.size());
        modelManifest.numTextureTransforms = static_cast<u32>(model.textureTransforms.size());

        modelManifest.isAnimated = model.sequences.size() > 0 && modelManifest.numBones > 0;
    }

    // Add Decoration Data
    {
        ZoneScopedN("Add Decoration Data");

        modelManifest.numDecorationSets = model.modelHeader.numDecorationSets;
        modelManifest.decorationSetOffset = modelOffsets.decorationSetStartIndex;

        if (modelManifest.numDecorationSets)
        {
            std::vector<Model::ComplexModel::DecorationSet>& decorationSets = _modelDecorationSets;

            void* dst = &decorationSets[modelManifest.decorationSetOffset];
            void* src = model.decorationSets.data();
            size_t size = sizeof(Model::ComplexModel::DecorationSet) * model.decorationSets.size();

            if (modelManifest.decorationSetOffset + model.decorationSets.size() > decorationSets.size())
            {
                NC_LOG_CRITICAL("ModelRenderer : Tried to memcpy decorationSets outside array");
            }

            memcpy(dst, src, size);
        }

        modelManifest.numDecorations = model.modelHeader.numDecorations;
        modelManifest.decorationOffset = modelOffsets.decorationStartIndex;

        if (modelManifest.numDecorations)
        {
            std::vector<Model::ComplexModel::Decoration>& decorations = _modelDecorations;

            void* dst = &decorations[modelManifest.decorationOffset];
            void* src = model.decorations.data();
            size_t size = sizeof(Model::ComplexModel::Decoration) * model.decorations.size();

            if (modelManifest.decorationOffset + model.decorations.size() > decorations.size())
            {
                NC_LOG_CRITICAL("ModelRenderer : Tried to memcpy decorations outside array");
            }

            memcpy(dst, src, size);
        }
    }

    return modelOffsets.modelIndex;
}

u32 ModelRenderer::AddPlacementInstance(entt::entity entityID, u32 modelID, Model::ComplexModel* model, const vec3& position, const quat& rotation, f32 scale, u32 doodadSet)
{
    // Add Instance matrix
    mat4x4 rotationMatrix = glm::toMat4(rotation);
    mat4x4 scaleMatrix = glm::scale(mat4x4(1.0f), vec3(scale));
    mat4x4 instanceMatrix = glm::translate(mat4x4(1.0f), position) * rotationMatrix * scaleMatrix;

    u32 instanceIndex = AddInstance(entityID, modelID, model, instanceMatrix);
    ModelManifest& manifest = _modelManifests[modelID];

    // Add Decorations
    if (manifest.numDecorationSets && manifest.numDecorations)
    {
        if (doodadSet == std::numeric_limits<u32>().max())
        {
            ModelLoader* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();

            // Load 0th doodadSet if it exists
            const Model::ComplexModel::DecorationSet& manifestDecorationSet = _modelDecorationSets[manifest.decorationSetOffset];

            for (u32 i = 0; i < manifestDecorationSet.count; i++)
            {
                const Model::ComplexModel::Decoration& manifestDecoration = _modelDecorations[manifest.decorationOffset + (manifestDecorationSet.index + i)];
                modelLoader->LoadDecoration(instanceIndex, manifestDecoration);
            }
        }
        else
        {
            if (doodadSet < manifest.numDecorationSets)
            {
                ModelLoader* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();

                const Model::ComplexModel::DecorationSet& manifestDecorationSet = _modelDecorationSets[manifest.decorationSetOffset + doodadSet];

                for (u32 i = 0; i < manifestDecorationSet.count; i++)
                {
                    const Model::ComplexModel::Decoration& manifestDecoration = _modelDecorations[manifest.decorationOffset + (manifestDecorationSet.index + i)];
                    modelLoader->LoadDecoration(instanceIndex, manifestDecoration);
                }
            }
        }
    }

    return instanceIndex;
}

u32 ModelRenderer::AddInstance(entt::entity entityID, u32 modelID, Model::ComplexModel* model, const mat4x4& transformMatrix, u32 displayInfoPacked)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    bool isSkybox = false;

    if (registry->valid(entityID))
    {
        isSkybox = registry->all_of<ECS::Components::SkyboxModelTag>(entityID); // TODO: We want to get rid of this
    }

    InstanceOffsets instanceOffsets;
    AllocateInstance(modelID, instanceOffsets);

    ModelManifest& manifest = _modelManifests[modelID];

    // Add InstanceData
    {
        InstanceData& instanceData = _instanceDatas[instanceOffsets.instanceIndex];
        instanceData.modelID = modelID;
        instanceData.modelVertexOffset = manifest.vertexOffset;

        if (manifest.isAnimated)
        {
            u32 animatedVertexOffset = _animatedVerticesIndex.fetch_add(manifest.numVertices);
            instanceData.animatedVertexOffset = animatedVertexOffset;
        }
    }

    // Add Instance matrix
    {
        mat4x4& instanceMatrix = _instanceMatrices[instanceOffsets.instanceIndex];
        instanceMatrix = transformMatrix;
    }

    // Add Instance to ModelManifest
    {
        std::scoped_lock lock(*_modelManifestsInstancesMutexes[modelID]);
        manifest.instances.push_back(instanceOffsets.instanceIndex);
    }

    if (model && displayInfoPacked != std::numeric_limits<u32>().max())
    {
        ReplaceTextureUnits(modelID, model, instanceOffsets.instanceIndex, displayInfoPacked);
    }

    _instancesDirty = true;

    return instanceOffsets.instanceIndex;
}

void ModelRenderer::RemoveInstance(u32 instanceID)
{
    InstanceData& instanceData = _instanceDatas[instanceID];
    ModelManifest& manifest = _modelManifests[instanceData.modelID];

    // TODO: We need to change _animatedVerticesIndex so we can free up between instanceData.animatedVertexOffset + manifest.numVertices

    // Remove Instance from ModelManifest
    {
        std::scoped_lock lock(*_modelManifestsInstancesMutexes[instanceData.modelID]);
        std::erase_if(manifest.instances, [instanceID](u32 i) { return i == instanceID; });
    }

    std::scoped_lock lock(_instanceOffsetsMutex);

    _instanceDatas.Remove(instanceID);
    _instanceMatrices.Remove(instanceID);

    _instanceManifests[instanceID].displayInfoPacked = 0;
    _instancesDirty = true;
}

void ModelRenderer::ModifyInstance(entt::entity entityID, u32 instanceID, u32 modelID, Model::ComplexModel* model, const mat4x4& transformMatrix, u32 displayInfoPacked)
{
    InstanceData& instanceData = _instanceDatas[instanceID];

    u32 oldModelID = instanceData.modelID;
    ModelManifest& oldManifest = _modelManifests[oldModelID];

    // Remove Instance from ModelManifest
    {
        std::scoped_lock lock(*_modelManifestsInstancesMutexes[oldModelID]);
        std::erase_if(oldManifest.instances, [instanceID](u32 i) { return i == instanceID; });
    }

    // Deallocate old animation data
    if (oldManifest.isAnimated)
    {
        DeallocateAnimation(instanceData.boneMatrixOffset, oldManifest.numBones, instanceData.textureTransformMatrixOffset, oldManifest.numTextureTransforms);
    }

    ModelManifest& newManifest = _modelManifests[modelID];

    // Add Instance to ModelManifest
    {
        std::scoped_lock lock(*_modelManifestsInstancesMutexes[modelID]);
        newManifest.instances.push_back(instanceID);
    }

    // Modify InstanceData
    {
        instanceData.modelID = modelID;
        instanceData.modelVertexOffset = newManifest.vertexOffset;

        if (newManifest.isAnimated)
        {
            if (!oldManifest.isAnimated || oldManifest.numVertices < newManifest.numVertices)
            {
                u32 animatedVertexOffset = _animatedVerticesIndex.fetch_add(newManifest.numVertices);
                instanceData.animatedVertexOffset = animatedVertexOffset;
            }

            // Allocate new animation data
            AddAnimationInstance(instanceID);
        }
    }

    // Modify Instance matrix
    {
        mat4x4& instanceMatrix = _instanceMatrices[instanceID];
        instanceMatrix = transformMatrix;
    }

    // Replace texture units
    if (model && displayInfoPacked != std::numeric_limits<u32>().max())
    {
        ReplaceTextureUnits(modelID, model, instanceID, displayInfoPacked);
    }

    _instancesDirty = true;
}

void ModelRenderer::ReplaceTextureUnits(u32 modelID, Model::ComplexModel* model, u32 instanceID, u32 displayInfoPacked)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    auto& clientDBCollection = registry->ctx().get<ECS::Singletons::ClientDBCollection>();
    auto& textureSingleton = registry->ctx().get<ECS::Singletons::TextureSingleton>();

    auto* textureFileDataStorage = clientDBCollection.Get(ECS::Singletons::ClientDBHash::TextureFileData);
    auto* creatureDisplayInfoStorage = clientDBCollection.Get(ECS::Singletons::ClientDBHash::CreatureDisplayInfo);
    auto* creatureDisplayInfoExtraStorage = clientDBCollection.Get(ECS::Singletons::ClientDBHash::CreatureDisplayInfoExtra);
    auto* itemDisplayInfoStorage = clientDBCollection.Get(ECS::Singletons::ClientDBHash::ItemDisplayInfo);
    auto* itemDisplayModelMaterialStorage = clientDBCollection.Get(ECS::Singletons::ClientDBHash::ItemDisplayModelMaterialResources);

    const ClientDB::Definitions::CreatureDisplayInfo* creatureDisplayInfo = nullptr;
    const ClientDB::Definitions::CreatureDisplayInfoExtra* creatureDisplayInfoExtra = nullptr;
    const ClientDB::Definitions::ItemDisplayInfo* itemDisplayInfo = nullptr;

    ModelManifest& manifest = _modelManifests[modelID];

    if (!_displayInfoManifests.contains(displayInfoPacked))
    {
        auto displayInfoType = static_cast<ClientDB::Definitions::DisplayInfoType>(displayInfoPacked >> 24);
        u32 displayID = displayInfoPacked & 0xFFFFFF;

        switch (displayInfoType)
        {
        case ClientDB::Definitions::DisplayInfoType::Creature:
        {
            if (creatureDisplayInfoStorage)
            {
                creatureDisplayInfo = &creatureDisplayInfoStorage->Get<ClientDB::Definitions::CreatureDisplayInfo>(displayID);

                if (creatureDisplayInfoExtraStorage && creatureDisplayInfo->extendedDisplayInfoID != 0)
                {
                    creatureDisplayInfoExtra = &creatureDisplayInfoExtraStorage->Get<ClientDB::Definitions::CreatureDisplayInfoExtra>(creatureDisplayInfo->extendedDisplayInfoID);
                }
            }

            break;
        }

        case ClientDB::Definitions::DisplayInfoType::Item:
        {
            if (itemDisplayInfoStorage)
            {
                itemDisplayInfo = &itemDisplayInfoStorage->Get<ClientDB::Definitions::ItemDisplayInfo>(displayID);
            }

            break;
        }

        default: return;
        }

        bool hasDynamicTextureUnits = false;

        u32 numTextureUnits = 0;
        for (auto& renderBatch : model->modelData.renderBatches)
        {
            numTextureUnits += static_cast<u32>(renderBatch.textureUnits.size());

            if (hasDynamicTextureUnits)
                continue;

            for (u32 i = 0; i < renderBatch.textureUnits.size(); i++)
            {
                Model::ComplexModel::TextureUnit& cTextureUnit = renderBatch.textureUnits[i];

                for (u32 j = 0; j < cTextureUnit.textureCount && j < 2; j++)
                {
                    u16 textureIndex = model->textureIndexLookupTable[cTextureUnit.textureIndexStart + j];
                    if (textureIndex == 65535)
                        continue;

                    Model::ComplexModel::Texture& cTexture = model->textures[textureIndex];

                    switch (cTexture.type)
                    {
                    case Model::ComplexModel::Texture::Type::Skin:
                    case Model::ComplexModel::Texture::Type::ObjectSkin:
                    case Model::ComplexModel::Texture::Type::WeaponBlade:
                    case Model::ComplexModel::Texture::Type::CharacterHair:
                    case Model::ComplexModel::Texture::Type::CharacterFacialHair:
                    case Model::ComplexModel::Texture::Type::SkinExtra:
                    case Model::ComplexModel::Texture::Type::MonsterSkin1:
                    case Model::ComplexModel::Texture::Type::MonsterSkin2:
                    case Model::ComplexModel::Texture::Type::MonsterSkin3:
                    {
                        hasDynamicTextureUnits = true;
                        break;
                    }

                    default: break;
                    }

                    if (hasDynamicTextureUnits)
                        break;
                }

                if (hasDynamicTextureUnits)
                    break;
            }
        }

        if (!hasDynamicTextureUnits)
            return;

        u32 numRenderBatches = static_cast<u32>(model->modelData.renderBatches.size());
        u32 textureTransformLookupTableSize = static_cast<u32>(model->textureTransformLookupTable.size());

        // Allocate new texture units
        TextureUnitOffsets textureUnitOffsets;
        AllocateTextureUnits(*model, textureUnitOffsets);

        DisplayInfoManifest displayInfoManifest;
        displayInfoManifest.overrideTextureDatas = true;

        u32 numTextureUnitsAdded = 0;
        u32 numOpaqueDrawCallsHandled = 0;
        u32 numTransparentDrawCallsHandled = 0;

        for (u32 renderBatchIndex = 0; renderBatchIndex < numRenderBatches; renderBatchIndex++)
        {
            u32 renderBatchTextureUnitStartIndex = textureUnitOffsets.textureUnitsStartIndex + numTextureUnitsAdded;
            Model::ComplexModel::RenderBatch& renderBatch = model->modelData.renderBatches[renderBatchIndex];

            u16 numUnlitTextureUnits = 0;

            for (u32 i = 0; i < renderBatch.textureUnits.size(); i++)
            {
                // Texture Unit
                u32 textureUnitOffset = textureUnitOffsets.textureUnitsStartIndex + numTextureUnitsAdded;
                TextureUnit& textureUnit = _textureUnits[textureUnitOffset];
                numTextureUnitsAdded++;

                Model::ComplexModel::TextureUnit& cTextureUnit = renderBatch.textureUnits[i];
                Model::ComplexModel::Material& cMaterial = model->materials[cTextureUnit.materialIndex];

                u16 materialFlag = *reinterpret_cast<u16*>(&cMaterial.flags) << 5;
                u16 blendingMode = static_cast<u16>(cMaterial.blendingMode) << 11;

                textureUnit.data = static_cast<u16>(cTextureUnit.flags.IsProjectedTexture) | materialFlag | blendingMode;
                textureUnit.materialType = cTextureUnit.shaderID;

                u16 textureTransformID1 = MODEL_INVALID_TEXTURE_TRANSFORM_ID;
                if (cTextureUnit.textureTransformIndexStart < textureTransformLookupTableSize)
                    textureTransformID1 = model->textureTransformLookupTable[cTextureUnit.textureTransformIndexStart];

                u16 textureTransformID2 = MODEL_INVALID_TEXTURE_TRANSFORM_ID;
                if (cTextureUnit.textureCount > 1)
                    if (cTextureUnit.textureTransformIndexStart + 1u < textureTransformLookupTableSize)
                        textureTransformID2 = model->textureTransformLookupTable[cTextureUnit.textureTransformIndexStart + 1];

                textureUnit.textureTransformIds[0] = textureTransformID1;
                textureUnit.textureTransformIds[1] = textureTransformID2;

                numUnlitTextureUnits += (materialFlag & 0x2) > 0;

                // Textures
                for (u32 j = 0; j < cTextureUnit.textureCount && j < 2; j++)
                {
                    u16 textureIndex = model->textureIndexLookupTable[cTextureUnit.textureIndexStart + j];
                    if (textureIndex == 65535)
                        continue;

                    Model::ComplexModel::Texture& cTexture = model->textures[textureIndex];
                    u32 textureHash = cTexture.textureHash;

                    static auto GetRaceSkinTextureForDisplayID = [](u32 displayID) -> u32
                    {
                        switch (displayID)
                        {
                        case 49: // Human Male
                        {
                            return "textures/bakednpctextures/creaturedisplayextra-00030.dds"_h;
                        }
                        case 50: // Human Female
                        {
                            return "textures/bakednpctextures/creaturedisplayextra-00941.dds"_h;
                        }
                        case 51: // Orc Male
                        {
                            return "textures/bakednpctextures/creaturedisplayextra-00111.dds"_h;
                        }
                        case 52: // Orc Female
                        {
                            return "textures/bakednpctextures/creaturedisplayextra-00102.dds"_h;
                        }
                        case 53: // Dwarf Male
                        {
                            return "textures/bakednpctextures/creaturedisplayextra-156637.dds"_h;
                        }
                        case 54: // Dwarf Female
                        {
                            return "textures/bakednpctextures/creaturedisplayextra-156636.dds"_h;
                        }
                        case 55: // Night Elf Male
                        {
                            return "textures/bakednpctextures/creaturedisplayextra-00405.dds"_h;
                        }
                        case 56: // Night Elf Female
                        {
                            return "textures/bakednpctextures/creaturedisplayextra-00404.dds"_h;
                        }
                        case 57: // Undead Male
                        {
                            return "textures/bakednpctextures/creaturedisplayextra-00046.dds"_h;
                        }
                        case 58: // Undead Female
                        {
                            return "textures/bakednpctextures/creaturedisplayextra-00262.dds"_h;
                        }
                        case 59: // Tauren Male
                        {
                            return "textures/bakednpctextures/creaturedisplayextra-00382.dds"_h;
                        }
                        case 60: // Tauren Female
                        {
                            return "textures/bakednpctextures/creaturedisplayextra-02034.dds"_h;
                        }
                        case 1563: // Gnome Male
                        {
                            return "textures/bakednpctextures/creaturedisplayextra-01202.dds"_h;
                        }
                        case 1564: // Gnome Female
                        {
                            return "textures/bakednpctextures/creaturedisplayextra-01822.dds"_h;
                        }
                        case 1478: // Troll Male
                        {
                            return "textures/bakednpctextures/creaturedisplayextra-02639.dds"_h;
                        }
                        case 1479: // Troll Female
                        {
                            return "textures/bakednpctextures/creaturedisplayextra-01118.dds"_h;
                        }

                        default: return "textures/bakednpctextures/creaturedisplayextra-00030.dds"_h;
                        }
                    };
                    static auto GetRaceHairTextureForDisplayID = [](u32 displayID) -> u32
                    {
                        switch (displayID)
                        {
                        case 49: // Human Male
                        {
                            return "character/human/hair00_01.dds"_h;
                        }
                        case 50: // Human Female
                        {
                            return "character/human/hair00_01.dds"_h;
                        }
                        case 51: // Orc Male
                        {
                            return "character/orc/hair00_00.dds"_h;
                        }
                        case 52: // Orc Female
                        {
                            return "character/orc/hair00_00.dds"_h;
                        }
                        case 53: // Dwarf Male
                        {
                            return "character/dwarf/hair00_05.dds"_h;
                        }
                        case 54: // Dwarf Female
                        {
                            return "character/dwarf/hair00_05.dds"_h;
                        }
                        case 55: // Night Elf Male
                        {
                            return "character/nightelf/hair00_06.dds"_h;
                        }
                        case 56: // Night Elf Female
                        {
                            return "character/nightelf/hair00_06.dds"_h;
                        }
                        case 57: // Undead Male
                        {
                            return "character/scourge/hair00_05.dds"_h;
                        }
                        case 58: // Undead Female
                        {
                            return "character/scourge/hair00_05.dds"_h;
                        }
                        case 59: // Tauren Male
                        {
                            return "character/tauren/scalplowerhair00_00.dds"_h;
                        }
                        case 60: // Tauren Female
                        {
                            return "character/tauren/scalplowerhair00_00.dds"_h;
                        }
                        case 1563: // Gnome Male
                        {
                            return "character/gnome/hair00_00.dds"_h;
                        }
                        case 1564: // Gnome Female
                        {
                            return "character/gnome/hair00_00.dds"_h;
                        }
                        case 1478: // Troll Male
                        {
                            return "character/troll/hair00_07.dds"_h;
                        }
                        case 1479: // Troll Female
                        {
                            return "character/troll/hair00_07.dds"_h;
                        }

                        default: return "character/human/hair00_01.dds"_h;
                        }
                    };

                    if (cTexture.type == Model::ComplexModel::Texture::Type::None)
                    {
                        textureHash = cTexture.textureHash;
                    }
                    else if (cTexture.type == Model::ComplexModel::Texture::Type::Skin)
                    {
                        u32 skinHash = cTexture.textureHash;
                        if (creatureDisplayInfoExtra)
                        {
                            skinHash = creatureDisplayInfoExtra->bakedTextureHash;
                        }
                        else
                        {
                            skinHash = GetRaceSkinTextureForDisplayID(displayID);
                        }

                        textureHash = skinHash;
                    }
                    else if (cTexture.type == Model::ComplexModel::Texture::Type::ObjectSkin)
                    {
                        if (itemDisplayInfo && itemDisplayModelMaterialStorage && textureFileDataStorage)
                        {
                            u32 materialResourcesID = itemDisplayInfo->materialResourcesID[0];

                            itemDisplayModelMaterialStorage->Each([&textureHash, materialResourcesID, displayID](const u32 id, const ClientDB::Definitions::ItemDisplayModelMaterialResources& row) -> bool
                                {
                                    if (row.materialResourcesID != materialResourcesID || row.displayID != displayID || row.modelIndex != 0 || row.textureType != 2)
                                        return true;

                                    textureHash = row.textureHash[0];
                                    return false;
                                });
                        }
                    }
                    else if (cTexture.type == Model::ComplexModel::Texture::Type::WeaponBlade)
                    {
                        if (itemDisplayInfo && itemDisplayModelMaterialStorage && textureFileDataStorage)
                        {
                            u32 materialResourcesID = itemDisplayInfo->materialResourcesID[0];

                            itemDisplayModelMaterialStorage->Each([&textureHash, materialResourcesID, displayID](u32 id, const ClientDB::Definitions::ItemDisplayModelMaterialResources& row) -> bool
                                {
                                    if (row.materialResourcesID != materialResourcesID || row.displayID != displayID || row.modelIndex != 0 || row.textureType != 3)
                                        return true;

                                    textureHash = row.textureHash[0];
                                    return false;
                                });
                        }
                    }
                    else if (cTexture.type == Model::ComplexModel::Texture::Type::CharacterHair)
                    {
                        textureHash = GetRaceHairTextureForDisplayID(displayID);
                    }
                    else if (cTexture.type == Model::ComplexModel::Texture::Type::MonsterSkin1)
                    {
                        if (creatureDisplayInfo)
                        {
                            textureHash = creatureDisplayInfo->textureVariations[0];
                        }
                    }
                    else if (cTexture.type == Model::ComplexModel::Texture::Type::MonsterSkin2)
                    {
                        if (creatureDisplayInfo)
                        {
                            textureHash = creatureDisplayInfo->textureVariations[1];
                        }
                    }
                    else if (cTexture.type == Model::ComplexModel::Texture::Type::MonsterSkin3)
                    {
                        if (creatureDisplayInfo)
                        {
                            textureHash = creatureDisplayInfo->textureVariations[2];
                        }
                    }

                    if (textureHash != std::numeric_limits<u32>().max())
                    {
                        TextureLoadRequest textureLoadRequest =
                        {
                            .textureUnitOffset = textureUnitOffset,
                            .textureIndex = j,
                            .textureHash = textureHash,
                        };

                        _textureLoadRequests.enqueue(textureLoadRequest);
                    }

                    u8 textureSamplerIndex = 0;

                    if (cTexture.flags.wrapX)
                        textureSamplerIndex |= 0x1;

                    if (cTexture.flags.wrapY)
                        textureSamplerIndex |= 0x2;

                    textureUnit.data |= textureSamplerIndex << (1 + (j * 2));
                }
            }

            robin_hood::unordered_map<u32, u32>& drawIDToTextureDataID = (renderBatch.isTransparent) ? displayInfoManifest.transparentDrawIDToTextureDataID : displayInfoManifest.opaqueDrawIDToTextureDataID;

            u32 drawCallOffset = (renderBatch.isTransparent) ? manifest.transparentDrawCallOffset : manifest.opaqueDrawCallOffset;
            u32& numDrawCallsHandled = (renderBatch.isTransparent) ? numTransparentDrawCallsHandled : numOpaqueDrawCallsHandled;

            u32 drawCallIndex = drawCallOffset + numDrawCallsHandled++;

            TextureDataOffsets textureDataOffsets;
            AllocateTextureData(1, textureDataOffsets);

            TextureData& textureData = _textureDatas[textureDataOffsets.textureDatasStartIndex];
            textureData.textureUnitOffset = renderBatchTextureUnitStartIndex;
            textureData.numTextureUnits = static_cast<u16>(renderBatch.textureUnits.size());
            textureData.numUnlitTextureUnits = numUnlitTextureUnits;

            drawIDToTextureDataID[drawCallIndex] = textureDataOffsets.textureDatasStartIndex;
        }

        std::scoped_lock lock(_displayInfoManifestsMutex);
        _displayInfoManifests[displayInfoPacked] = std::move(displayInfoManifest);
    }

    InstanceManifest& instanceManifest = _instanceManifests[instanceID];
    instanceManifest.displayInfoPacked = displayInfoPacked;
}

void ModelRenderer::RequestChangeGroup(u32 instanceID, u32 groupIDStart, u32 groupIDEnd, bool enable)
{
    ChangeGroupRequest changeGroupRequest =
    {
        .instanceID = instanceID,
        .groupIDStart = groupIDStart,
        .groupIDEnd = groupIDEnd,
        .enable = enable,
    };

    _changeGroupRequests.enqueue(changeGroupRequest);
}

bool ModelRenderer::AddUninstancedAnimationData(u32 modelID, u32& boneMatrixOffset, u32& textureTransformMatrixOffset)
{
    if (_modelManifests.size() <= modelID)
        return false;

    AnimationOffsets animationOffsets;
    AllocateAnimation(modelID, animationOffsets);

    const ModelManifest& modelManifest = _modelManifests[modelID];

    if (modelManifest.numBones > 0)
    {
        boneMatrixOffset = animationOffsets.boneStartIndex;

        for (u32 i = 0; i < modelManifest.numBones; i++)
        {
            _boneMatrices[boneMatrixOffset + i] = mat4x4(1.0f);
        }

        _boneMatrices.SetDirtyElements(boneMatrixOffset, modelManifest.numBones);
    }
    else
    {
        boneMatrixOffset = std::numeric_limits<u32>().max();
    }

    if (modelManifest.numTextureTransforms > 0)
    {
        textureTransformMatrixOffset = animationOffsets.textureTransformStartIndex;
        
        for (u32 i = 0; i < modelManifest.numTextureTransforms; i++)
        {
            _textureTransformMatrices[textureTransformMatrixOffset + i] = mat4x4(1.0f);
        }

        _textureTransformMatrices.SetDirtyElements(textureTransformMatrixOffset, modelManifest.numTextureTransforms);
    }
    else
    {
        textureTransformMatrixOffset = std::numeric_limits<u32>().max();
    }

    return true;
}

bool ModelRenderer::SetInstanceAnimationData(u32 instanceID, u32 boneMatrixOffset, u32 textureTransformMatrixOffset)
{
    if (instanceID >= _instanceDatas.Count())
    {
        return false;
    }

    InstanceData& instanceData = _instanceDatas[instanceID];
    const ModelManifest& modelManifest = _modelManifests[instanceData.modelID];

    if (modelManifest.numBones > 0)
    {
        instanceData.boneMatrixOffset = boneMatrixOffset;
    }

    if (modelManifest.numTextureTransforms > 0)
    {
        instanceData.textureTransformMatrixOffset = textureTransformMatrixOffset;
    }

    if (modelManifest.numBones > 0 || modelManifest.numTextureTransforms > 0)
    {
        _instanceDatas.SetDirtyElement(instanceID);
    }

    return true;
}

bool ModelRenderer::SetUninstancedBoneMatricesAsDirty(u32 modelID, u32 boneMatrixOffset, u32 localBoneIndex, u32 count, const mat4x4* boneMatrixArray)
{
    if (_modelManifests.size() <= modelID)
        return false;

    const ModelManifest& modelManifest = _modelManifests[modelID];
    if (boneMatrixOffset == InstanceData::InvalidID)
        return false;

    u32 globalBoneIndex = boneMatrixOffset + localBoneIndex;
    u32 endGlobalBoneIndex = globalBoneIndex + (count - 1);

    // Check if the bone range is valid
    if (endGlobalBoneIndex > boneMatrixOffset + modelManifest.numBones)
        return false;

    if (count == 1)
    {
        _boneMatrices[globalBoneIndex] = *boneMatrixArray;
        _boneMatrices.SetDirtyElement(globalBoneIndex);
    }
    else
    {
        memcpy(&_boneMatrices[globalBoneIndex], boneMatrixArray, count * sizeof(mat4x4));
        _boneMatrices.SetDirtyElements(globalBoneIndex, count);
    }

    return true;
}

bool ModelRenderer::SetUninstancedTextureTransformMatricesAsDirty(u32 modelID, u32 textureTransformMatrixOffset, u32 localTextureTransformIndex, u32 count, const mat4x4* textureTransformMatrixArray)
{
    if (_modelManifests.size() <= modelID)
        return false;

    const ModelManifest& modelManifest = _modelManifests[modelID];
    if (textureTransformMatrixOffset == InstanceData::InvalidID)
        return false;

    u32 globalTextureTransformIndex = textureTransformMatrixOffset + localTextureTransformIndex;
    u32 endGlobalTextureTransformIndex = globalTextureTransformIndex + (count - 1);

    // Check if the bone range is valid
    if (endGlobalTextureTransformIndex > textureTransformMatrixOffset + modelManifest.numTextureTransforms)
        return false;

    if (count == 1)
    {
        _textureTransformMatrices[globalTextureTransformIndex] = *textureTransformMatrixArray;
        _textureTransformMatrices.SetDirtyElement(globalTextureTransformIndex);
    }
    else
    {
        memcpy(&_textureTransformMatrices[globalTextureTransformIndex], textureTransformMatrixArray, count * sizeof(mat4x4));
        _textureTransformMatrices.SetDirtyElements(globalTextureTransformIndex, count);
    }

    return true;
}

bool ModelRenderer::AddAnimationInstance(u32 instanceID)
{
    if (instanceID >= _instanceDatas.Count())
    {
        return false;
    }

    InstanceData& instanceData = _instanceDatas[instanceID];

    AnimationOffsets animationOffsets;
    AllocateAnimation(instanceData.modelID, animationOffsets);

    const ModelManifest& modelManifest = _modelManifests[instanceData.modelID];

    if (modelManifest.numBones > 0)
    {
        instanceData.boneMatrixOffset = animationOffsets.boneStartIndex;

        // Default initialize the bone and texture transform matrices
        for (u32 i = 0; i < modelManifest.numBones; ++i)
        {
            _boneMatrices[animationOffsets.boneStartIndex + i] = glm::mat4(1.0f);
        }
    
        _boneMatrices.SetDirtyElements(animationOffsets.boneStartIndex, modelManifest.numBones);
    }

    if (modelManifest.numTextureTransforms > 0)
    {
        instanceData.textureTransformMatrixOffset = animationOffsets.textureTransformStartIndex;

        for (u32 i = 0; i < modelManifest.numTextureTransforms; ++i)
        {
            _textureTransformMatrices[animationOffsets.textureTransformStartIndex + i] = glm::mat4(1.0f);
        }

        _textureTransformMatrices.SetDirtyElements(animationOffsets.textureTransformStartIndex, modelManifest.numTextureTransforms);
    }

    if (modelManifest.numBones > 0 || modelManifest.numTextureTransforms > 0)
    {
        _instanceDatas.SetDirtyElement(instanceID);
    }

    return true;
}

bool ModelRenderer::SetBoneMatricesAsDirty(u32 instanceID, u32 localBoneIndex, u32 count, const mat4x4* boneMatrixArray)
{
    if (instanceID >= _instanceDatas.Count())
    {
        return false;
    }

    InstanceData& instanceData = _instanceDatas[instanceID];
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
        _boneMatrices[globalBoneIndex] = *boneMatrixArray;
        _boneMatrices.SetDirtyElement(globalBoneIndex);
    }
    else
    {
        memcpy(&_boneMatrices[globalBoneIndex], boneMatrixArray, count * sizeof(mat4x4));
        _boneMatrices.SetDirtyElements(globalBoneIndex, count);
    }

    return true;
}

bool ModelRenderer::SetTextureTransformMatricesAsDirty(u32 instanceID, u32 localTextureTransformIndex, u32 count, const mat4x4* boneMatrixArray)
{
    if (instanceID >= _instanceDatas.Count())
    {
        return false;
    }

    InstanceData& instanceData = _instanceDatas[instanceID];
    if (instanceData.textureTransformMatrixOffset == InstanceData::InvalidID)
    {
        return false;
    }

    const ModelManifest& modelManifest = _modelManifests[instanceData.modelID];

    u32 globalTextureTransformMatrixIndex = instanceData.textureTransformMatrixOffset + localTextureTransformIndex;
    u32 endGlobalTextureTransformMatrixIndex = globalTextureTransformMatrixIndex + (count - 1);

    // Check if the bone range is valid
    if (endGlobalTextureTransformMatrixIndex > instanceData.textureTransformMatrixOffset + modelManifest.numTextureTransforms)
    {
        return false;
    }

    if (count == 1)
    {
        _textureTransformMatrices[globalTextureTransformMatrixIndex] = *boneMatrixArray;
        _textureTransformMatrices.SetDirtyElement(globalTextureTransformMatrixIndex);
    }
    else
    {
        memcpy(&_textureTransformMatrices[globalTextureTransformMatrixIndex], boneMatrixArray, count * sizeof(mat4x4));
        _textureTransformMatrices.SetDirtyElements(globalTextureTransformMatrixIndex, count);
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

    _textureLoadWork.resize(256);
    _dirtyTextureUnitOffsets.reserve(256);

    _changeGroupWork.resize(256);

    static constexpr u32 NumSamplers = 4;
    _samplers.reserve(NumSamplers);

    // Sampler Clamp, Clamp
    {
        Renderer::SamplerDesc samplerDesc;
        samplerDesc.enabled = true;
        samplerDesc.filter = Renderer::SamplerFilter::MIN_MAG_MIP_LINEAR;
        samplerDesc.addressU = Renderer::TextureAddressMode::CLAMP;
        samplerDesc.addressV = Renderer::TextureAddressMode::CLAMP;
        samplerDesc.addressW = Renderer::TextureAddressMode::CLAMP;
        samplerDesc.shaderVisibility = Renderer::ShaderVisibility::PIXEL;

        Renderer::SamplerID samplerID = _renderer->CreateSampler(samplerDesc);
        _samplers.push_back(samplerID);
    }

    // Sampler Wrap, Clamp
    {
        Renderer::SamplerDesc samplerDesc;
        samplerDesc.enabled = true;
        samplerDesc.filter = Renderer::SamplerFilter::MIN_MAG_MIP_LINEAR;
        samplerDesc.addressU = Renderer::TextureAddressMode::WRAP;
        samplerDesc.addressV = Renderer::TextureAddressMode::CLAMP;
        samplerDesc.addressW = Renderer::TextureAddressMode::CLAMP;
        samplerDesc.shaderVisibility = Renderer::ShaderVisibility::PIXEL;

        Renderer::SamplerID samplerID = _renderer->CreateSampler(samplerDesc);
        _samplers.push_back(samplerID);
    }

    // Sampler Clamp, Wrap
    {
        Renderer::SamplerDesc samplerDesc;
        samplerDesc.enabled = true;
        samplerDesc.filter = Renderer::SamplerFilter::MIN_MAG_MIP_LINEAR;
        samplerDesc.addressU = Renderer::TextureAddressMode::CLAMP;
        samplerDesc.addressV = Renderer::TextureAddressMode::WRAP;
        samplerDesc.addressW = Renderer::TextureAddressMode::CLAMP;
        samplerDesc.shaderVisibility = Renderer::ShaderVisibility::PIXEL;

        Renderer::SamplerID samplerID = _renderer->CreateSampler(samplerDesc);
        _samplers.push_back(samplerID);
    }

    // Sampler Wrap, Wrap
    {
        Renderer::SamplerDesc samplerDesc;
        samplerDesc.enabled = true;
        samplerDesc.filter = Renderer::SamplerFilter::MIN_MAG_MIP_LINEAR;
        samplerDesc.addressU = Renderer::TextureAddressMode::WRAP;
        samplerDesc.addressV = Renderer::TextureAddressMode::WRAP;
        samplerDesc.addressW = Renderer::TextureAddressMode::CLAMP;
        samplerDesc.shaderVisibility = Renderer::ShaderVisibility::PIXEL;

        Renderer::SamplerID samplerID = _renderer->CreateSampler(samplerDesc);
        _samplers.push_back(samplerID);
    }

    for (u32 i = 0; i < NumSamplers; i++)
    {
        _opaqueCullingResources.GetGeometryPassDescriptorSet().BindArray("_samplers"_h, _samplers[i], i);
        _transparentCullingResources.GetGeometryPassDescriptorSet().BindArray("_samplers"_h, _samplers[i], i);
        _opaqueSkyboxCullingResources.GetGeometryPassDescriptorSet().BindArray("_samplers"_h, _samplers[i], i);
        _transparentSkyboxCullingResources.GetGeometryPassDescriptorSet().BindArray("_samplers"_h, _samplers[i], i);
        _materialPassDescriptorSet.BindArray("_samplers"_h, _samplers[i], i);
    }

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

    // Set GPU Buffers name and usage
    {
        _vertices.SetDebugName("ModelVertexBuffer");
        _vertices.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

        _animatedVertices.SetDebugName("ModelAnimatedVertexBuffer");
        _animatedVertices.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

        _indices.SetDebugName("ModelIndexBuffer");
        _indices.SetUsage(Renderer::BufferUsage::INDEX_BUFFER | Renderer::BufferUsage::STORAGE_BUFFER);

        _textureDatas.SetDebugName("ModelTextureDataBuffer");
        _textureDatas.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

        _textureUnits.SetDebugName("ModelTextureUnitBuffer");
        _textureUnits.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

        _instanceDatas.SetDebugName("ModelInstanceDatas");
        _instanceDatas.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

        _instanceMatrices.SetDebugName("ModelInstanceMatrices");
        _instanceMatrices.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

        _boneMatrices.SetDebugName("ModelInstanceBoneMatrices");
        _boneMatrices.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

        _textureTransformMatrices.SetDebugName("ModelInstanceTextureTransformMatrices");
        _textureTransformMatrices.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
    }
}


void ModelRenderer::AllocateModel(const Model::ComplexModel& model, ModelOffsets& offsets)
{
    std::scoped_lock lock(_modelOffsetsMutex);

    offsets.modelIndex = _cullingDatas.Add();

    _modelIDToNumInstances.resize(_modelIDToNumInstances.size() + 1);
    _modelManifests.resize(_modelManifests.size() + 1);
    _modelManifestsInstancesMutexes.push_back(std::make_unique<std::mutex>());

    offsets.verticesStartIndex = _vertices.AddCount(model.modelHeader.numVertices);
    offsets.indicesStartIndex = _indices.AddCount(model.modelHeader.numIndices);

    offsets.decorationSetStartIndex = static_cast<u32>(_modelDecorationSets.size());
    _modelDecorationSets.resize(offsets.decorationSetStartIndex + model.modelHeader.numDecorationSets);

    offsets.decorationStartIndex = static_cast<u32>(_modelDecorations.size());
    _modelDecorations.resize(offsets.decorationStartIndex + model.modelHeader.numDecorations);
}

void ModelRenderer::AllocateTextureData(u32 numTextureDatas, TextureDataOffsets& offsets)
{
    std::scoped_lock lock(_textureDataOffsetsMutex);

    offsets.textureDatasStartIndex = _textureDatas.AddCount(numTextureDatas);
}

void ModelRenderer::AllocateTextureUnits(const Model::ComplexModel& model, TextureUnitOffsets& offsets)
{
    std::scoped_lock lock(_textureOffsetsMutex);

    offsets.textureUnitsStartIndex = _textureUnits.AddCount(model.modelHeader.numTextureUnits);
}

void ModelRenderer::AllocateAnimation(u32 modelID, AnimationOffsets& offsets)
{
    std::scoped_lock lock(_animationOffsetsMutex);

    ModelManifest& manifest = _modelManifests[modelID];

    offsets.boneStartIndex = _boneMatrices.AddCount(manifest.numBones);
    offsets.textureTransformStartIndex = _textureTransformMatrices.AddCount(manifest.numTextureTransforms);
}

void ModelRenderer::AllocateInstance(u32 modelID, InstanceOffsets& offsets)
{
    std::scoped_lock lock(_instanceOffsetsMutex);

    ModelManifest& manifest = _modelManifests[modelID];

    offsets.instanceIndex = _instanceDatas.Add();

    if (offsets.instanceIndex >= _instanceManifests.size())
    {
        _instanceManifests.resize(_instanceManifests.size() + 1);
    }

    u32 instanceMatrixIndex = _instanceMatrices.Add();
    assert(offsets.instanceIndex == instanceMatrixIndex);
}

void ModelRenderer::AllocateDrawCalls(u32 modelID, DrawCallOffsets& offsets, bool isSkybox)
{
    std::scoped_lock lock(_drawCallOffsetsMutex);

    ModelManifest& manifest = _modelManifests[modelID];

    if (isSkybox)
    {
        offsets.opaqueDrawCallStartIndex = _opaqueSkyboxCullingResources.AddCount(manifest.numOpaqueDrawCalls);
        offsets.transparentDrawCallStartIndex = _transparentSkyboxCullingResources.AddCount(manifest.numTransparentDrawCalls);
    }
    else
    {
        const auto& opaqueDrawCalls = _opaqueCullingResources.GetDrawCalls();
        const auto& transparentDrawCalls = _opaqueCullingResources.GetDrawCalls();

        offsets.opaqueDrawCallStartIndex = _opaqueCullingResources.AddCount(manifest.numOpaqueDrawCalls);
        offsets.transparentDrawCallStartIndex = _transparentCullingResources.AddCount(manifest.numTransparentDrawCalls);
    }
}

void ModelRenderer::DeallocateAnimation(u32 boneStartIndex, u32 numBones, u32 textureTransformStartIndex, u32 numTextureTransforms)
{
    std::scoped_lock lock(_animationOffsetsMutex);

    _boneMatrices.Remove(boneStartIndex, numBones);
    _textureTransformMatrices.Remove(textureTransformStartIndex, numTextureTransforms);
}

void ModelRenderer::CompactInstanceRefs()
{
    if (!_instancesDirty)
        return;

    // Create array of culling resources that we can loop over
    CullingResourcesIndexed<DrawCallData>* cullingResources[] = {
        &_opaqueCullingResources,
        &_transparentCullingResources,
        &_opaqueSkyboxCullingResources,
        &_transparentSkyboxCullingResources
    };

    // Loop over all the culling resources
    bool isTransparent = false;
    for (auto& cullingResource : cullingResources)
    {
        u32 numTotalInstances = 0;

        auto& instanceRefs = cullingResource->GetInstanceRefs();
        instanceRefs.Clear();

        const auto& draws = cullingResource->GetDrawCalls();
        const auto& drawDatas = cullingResource->GetDrawCallDatas();

        u32 numDraws = draws.Count();
        for (u32 drawID = 0; drawID < numDraws; drawID++)
        {
            Renderer::IndexedIndirectDraw& draw = draws[drawID];
            DrawCallData& drawData = drawDatas[drawID];
            const ModelManifest& manifest = _modelManifests[drawData.modelID];

            const robin_hood::unordered_map<u32, u32>& drawIDToTextureDataID = (isTransparent) ? manifest.transparentDrawIDToTextureDataID : manifest.opaqueDrawIDToTextureDataID;

            u32 defaultTextureDataID = drawIDToTextureDataID.at(drawID);

            u32 numTotalInstancesInModel = static_cast<u32>(manifest.instances.size());
            instanceRefs.AddCount(numTotalInstancesInModel);
            u32 numEnabledInstancesInDraw = 0;

            for (u32 i = 0; i < numTotalInstancesInModel; i++)
            {
                u32 instanceID = manifest.instances[i];

                u32 textureDataID = defaultTextureDataID;

                InstanceManifest& instanceManifest = _instanceManifests[instanceID];

                // Check if this instance has this draw enabled
                {
                    const robin_hood::unordered_map<u32, u32>& drawIDToGroupID = (isTransparent) ? manifest.transparentDrawIDToGroupID : manifest.opaqueDrawIDToGroupID;
                    u32 groupID = drawIDToGroupID.at(drawID);

                    if (groupID != 0)
                    {
                        if (!instanceManifest.enabledGroupIDs.contains(groupID))
                            continue;
                    }
                }

                if (instanceManifest.displayInfoPacked != 0)
                {
                    const DisplayInfoManifest& displayInfoManifest = _displayInfoManifests[instanceManifest.displayInfoPacked];
                    const robin_hood::unordered_map<u32, u32>& instanceDrawIDToTextureDataID = (isTransparent) ? displayInfoManifest.transparentDrawIDToTextureDataID : displayInfoManifest.opaqueDrawIDToTextureDataID;
                    textureDataID = instanceDrawIDToTextureDataID.at(drawID);
                }

                InstanceRef& instanceRef = instanceRefs[numTotalInstances + numEnabledInstancesInDraw];
                instanceRef.drawID = drawID;
                instanceRef.instanceID = instanceID;
                instanceRef.extraID = textureDataID;
                numEnabledInstancesInDraw++;
            }

            draw.firstInstance = numTotalInstances;
            draw.instanceCount = numEnabledInstancesInDraw;

            drawData.baseInstanceLookupOffset = numTotalInstances;
            numTotalInstances += numEnabledInstancesInDraw;
        }

        u32 numInstanceRefs = instanceRefs.Count();
        if (numInstanceRefs > numTotalInstances)
        {
            instanceRefs.Remove(numTotalInstances, numInstanceRefs - numTotalInstances);
        }

        cullingResource->SetDirty();
        isTransparent = !isTransparent;
    }
}

void ModelRenderer::SyncToGPU()
{
    ZoneScopedN("ModelRenderer::SyncToGPU");

    CulledRenderer::SyncToGPU();

    // Sync Vertex buffer to GPU
    {
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
        if (_indices.SyncToGPU(_renderer))
        {
            _opaqueCullingResources.GetGeometryPassDescriptorSet().Bind("_modelIndices"_h, _indices.GetBuffer());
            _transparentCullingResources.GetGeometryPassDescriptorSet().Bind("_modelIndices"_h, _indices.GetBuffer());
            _opaqueSkyboxCullingResources.GetGeometryPassDescriptorSet().Bind("_modelIndices"_h, _indices.GetBuffer());
            _transparentSkyboxCullingResources.GetGeometryPassDescriptorSet().Bind("_modelIndices"_h, _indices.GetBuffer());
            _materialPassDescriptorSet.Bind("_modelIndices"_h, _indices.GetBuffer());
        }
    }

    // Sync TextureDatas buffer to GPU
    {
        if (_textureDatas.SyncToGPU(_renderer))
        {
            _opaqueCullingResources.GetGeometryPassDescriptorSet().Bind("_packedModelTextureDatas"_h, _textureDatas.GetBuffer());
            _transparentCullingResources.GetGeometryPassDescriptorSet().Bind("_packedModelTextureDatas"_h, _textureDatas.GetBuffer());
            _opaqueSkyboxCullingResources.GetGeometryPassDescriptorSet().Bind("_packedModelTextureDatas"_h, _textureDatas.GetBuffer());
            _transparentSkyboxCullingResources.GetGeometryPassDescriptorSet().Bind("_packedModelTextureDatas"_h, _textureDatas.GetBuffer());
            _materialPassDescriptorSet.Bind("_packedModelTextureDatas"_h, _textureDatas.GetBuffer());
        }
    }

    // Sync TextureUnit buffer to GPU
    {
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

    // Sync TextureTransformMatrices buffer to GPU
    {
        if (_textureTransformMatrices.SyncToGPU(_renderer))
        {
            //_animationPrepassDescriptorSet.Bind("_instanceTextureTransformMatrices"_h, _textureTransformMatrices.GetBuffer());

            _opaqueCullingResources.GetGeometryPassDescriptorSet().Bind("_instanceTextureTransformMatrices"_h, _textureTransformMatrices.GetBuffer());
            _transparentCullingResources.GetGeometryPassDescriptorSet().Bind("_instanceTextureTransformMatrices"_h, _textureTransformMatrices.GetBuffer());
            _opaqueSkyboxCullingResources.GetGeometryPassDescriptorSet().Bind("_instanceTextureTransformMatrices"_h, _textureTransformMatrices.GetBuffer());
            _transparentSkyboxCullingResources.GetGeometryPassDescriptorSet().Bind("_instanceTextureTransformMatrices"_h, _textureTransformMatrices.GetBuffer());
            _materialPassDescriptorSet.Bind("_instanceTextureTransformMatrices"_h, _textureTransformMatrices.GetBuffer());
        }
    }

    bool forceRecount = _instancesDirty;
    _opaqueCullingResources.SyncToGPU(forceRecount);
    _transparentCullingResources.SyncToGPU(forceRecount);
    _opaqueSkyboxCullingResources.SyncToGPU(forceRecount);
    _transparentSkyboxCullingResources.SyncToGPU(forceRecount);

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
    pipelineDesc.states.rasterizerState.cullMode = (params.shadowPass) ? Renderer::CullMode::NONE : Renderer::CullMode::BACK;
    pipelineDesc.states.rasterizerState.frontFaceMode = Renderer::Settings::FRONT_FACE_STATE;
    pipelineDesc.states.rasterizerState.depthBiasEnabled = params.shadowPass;
    pipelineDesc.states.rasterizerState.depthClampEnabled = params.shadowPass;

    // Render targets
    if (!params.shadowPass)
    {
        pipelineDesc.renderTargets[0] = params.rt0;
        if (params.rt1 != Renderer::ImageMutableResource::Invalid())
        {
            pipelineDesc.renderTargets[1] = params.rt1;
        }
    }
    pipelineDesc.depthStencil = params.depth;

    // Draw
    Renderer::GraphicsPipelineID pipeline = _renderer->CreatePipeline(pipelineDesc); // This will compile the pipeline and return the ID, or just return ID of cached pipeline
    commandList.BeginPipeline(pipeline);

    struct PushConstants
    {
        u32 viewIndex;
    };

    PushConstants* constants = graphResources.FrameNew<PushConstants>();

    constants->viewIndex = params.viewIndex;
    commandList.PushConstant(constants, 0, sizeof(PushConstants));

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
    pixelShaderDesc.AddPermutationField("TRANSPARENCY", isTransparent ? "1" : "0");

    pipelineDesc.states.pixelShader = _renderer->LoadShader(pixelShaderDesc);

    // Depth state
    pipelineDesc.states.depthStencilState.depthEnable = !isTransparent;
    pipelineDesc.states.depthStencilState.depthFunc = Renderer::ComparisonFunc::GREATER;
    pipelineDesc.states.depthStencilState.depthWriteEnable = !isTransparent;

    // Blend state
    if (isTransparent)
    {
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
    }

    // Rasterizer state
    pipelineDesc.states.rasterizerState.cullMode = Renderer::CullMode::BACK;
    pipelineDesc.states.rasterizerState.frontFaceMode = Renderer::Settings::FRONT_FACE_STATE;

    // Render targets
    pipelineDesc.renderTargets[0] = params.rt0;
    if (isTransparent)
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