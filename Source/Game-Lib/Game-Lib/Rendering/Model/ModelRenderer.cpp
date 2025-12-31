#include "ModelRenderer.h"

#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Components/DisplayInfo.h"
#include "Game-Lib/ECS/Components/Model.h"
#include "Game-Lib/ECS/Components/Tags.h"
#include "Game-Lib/ECS/Components/UnitCustomization.h"
#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/ECS/Singletons/Database/ItemSingleton.h"
#include "Game-Lib/ECS/Singletons/Database/TextureSingleton.h"
#include "Game-Lib/ECS/Singletons/Database/UnitCustomizationSingleton.h"
#include "Game-Lib/ECS/Util/Transforms.h"
#include "Game-Lib/ECS/Util/Database/ItemUtil.h"
#include "Game-Lib/ECS/Util/Database/UnitCustomizationUtil.h"
#include "Game-Lib/Rendering/CullUtils.h"
#include "Game-Lib/Rendering/RenderUtils.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/RenderResources.h"
#include "Game-Lib/Rendering/Debug/DebugRenderer.h"
#include "Game-Lib/Rendering/Model/ModelLoader.h"
#include "Game-Lib/Rendering/Texture/TextureRenderer.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/CVarSystem/CVarSystem.h>

#include <FileFormat/Novus/Map/MapChunk.h>

#include <Input/InputManager.h>

#include <MetaGen/Shared/ClientDB/ClientDB.h>

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

ModelRenderer::ModelRenderer(Renderer::Renderer* renderer, GameRenderer* gameRenderer, DebugRenderer* debugRenderer)
    : CulledRenderer(renderer, gameRenderer, debugRenderer)
    , _renderer(renderer)
    , _gameRenderer(gameRenderer)
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

    entt::registry* gameRegistry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    entt::registry* dbRegistry = ServiceLocator::GetEnttRegistries()->dbRegistry;

    {
        ZoneScopedN("Update Transform Matrices");

        gameRegistry->view<ECS::Components::Transform, ECS::Components::Model, ECS::Components::DirtyTransform>().each([&](entt::entity entity, ECS::Components::Transform& transform, ECS::Components::Model& model, ECS::Components::DirtyTransform& dirtyTransform)
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
    
        auto& textureSingleton = dbRegistry->ctx().get<ECS::Singletons::TextureSingleton>();
    
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

    u32 numChangeSkinTextureRequests = static_cast<u32>(_changeSkinTextureRequests.try_dequeue_bulk(_changeSkinTextureWork.begin(), 256));
    if (numChangeSkinTextureRequests > 0)
    {
        ZoneScopedN("Change Skin Texture Requests");

        for (u32 i = 0; i < numChangeSkinTextureRequests; i++)
        {
            ChangeSkinTextureRequest& changeSkinTextureRequest = _changeSkinTextureWork[i];

            InstanceManifest& instanceManifest = _instanceManifests[changeSkinTextureRequest.instanceID];

            robin_hood::unordered_map<u64, DisplayInfoManifest>* displayInfoManifests = instanceManifest.isDynamic ? &_uniqueDisplayInfoManifests : &_displayInfoManifests;
            if (!displayInfoManifests->contains(instanceManifest.displayInfoPacked))
                continue;

            DisplayInfoManifest& displayInfoManifest = displayInfoManifests->at(instanceManifest.displayInfoPacked);
            u32 textureArrayIndex = changeSkinTextureRequest.textureID == Renderer::TextureID::Invalid() ? 0 : _renderer->AddTextureToArray(changeSkinTextureRequest.textureID, _textures);

            for (u64 textureUnitAddress : displayInfoManifest.skinTextureUnits)
            {
                u32 textureUnitOffset = textureUnitAddress & 0xFFFFFFFF;
                u32 textureIndex = (textureUnitAddress >> 32) & 0xFFFFFFFF;

                TextureUnit& textureUnit = _textureUnits[textureUnitOffset];
                textureUnit.textureIds[textureIndex] = textureArrayIndex;
                
                _textureUnits.SetDirtyElement(textureUnitOffset);
            }
        }
    }

    u32 numChangeHairTextureRequests = static_cast<u32>(_changeHairTextureRequests.try_dequeue_bulk(_changeHairTextureWork.begin(), 256));
    if (numChangeHairTextureRequests > 0)
    {
        ZoneScopedN("Change Hair Texture Requests");

        for (u32 i = 0; i < numChangeHairTextureRequests; i++)
        {
            ChangeHairTextureRequest& changeHairTextureRequest = _changeHairTextureWork[i];

            InstanceManifest& instanceManifest = _instanceManifests[changeHairTextureRequest.instanceID];

            robin_hood::unordered_map<u64, DisplayInfoManifest>* displayInfoManifests = instanceManifest.isDynamic ? &_uniqueDisplayInfoManifests : &_displayInfoManifests;
            if (!displayInfoManifests->contains(instanceManifest.displayInfoPacked))
                continue;

            DisplayInfoManifest& displayInfoManifest = displayInfoManifests->at(instanceManifest.displayInfoPacked);
            u32 textureArrayIndex = changeHairTextureRequest.textureID == Renderer::TextureID::Invalid() ? 0 : _renderer->AddTextureToArray(changeHairTextureRequest.textureID, _textures);

            for (u64 textureUnitAddress : displayInfoManifest.hairTextureUnits)
            {
                u32 textureUnitOffset = textureUnitAddress & 0xFFFFFFFF;
                u32 textureIndex = (textureUnitAddress >> 32) & 0xFFFFFFFF;

                TextureUnit& textureUnit = _textureUnits[textureUnitOffset];
                textureUnit.textureIds[textureIndex] = textureArrayIndex;
                
                _textureUnits.SetDirtyElement(textureUnitOffset);
            }
        }
    }

    u32 numChangeVisibilityRequests = static_cast<u32>(_changeVisibilityRequests.try_dequeue_bulk(_changeVisibilityWork.begin(), 256));
    if (numChangeVisibilityRequests > 0)
    {
        ZoneScopedN("Change Visibility Requests");
        for (u32 i = 0; i < numChangeVisibilityRequests; i++)
        {
            ChangeVisibilityRequest& changeVisibilityRequest = _changeVisibilityWork[i];
            InstanceManifest& instanceManifest = _instanceManifests[changeVisibilityRequest.instanceID];
            instanceManifest.visible = changeVisibilityRequest.visible;
        }
        _instancesDirty = true;
    }

    u32 numChangeTransparencyRequests = static_cast<u32>(_changeTransparencyRequests.try_dequeue_bulk(_changeTransparencyWork.begin(), 256));
    if (numChangeTransparencyRequests > 0)
    {
        ZoneScopedN("Change Transparency Requests");
        for (u32 i = 0; i < numChangeTransparencyRequests; i++)
        {
            ChangeTransparencyRequest& changeTransparencyRequest = _changeTransparencyWork[i];
            InstanceManifest& instanceManifest = _instanceManifests[changeTransparencyRequest.instanceID];
            instanceManifest.transparent = changeTransparencyRequest.transparent;

            if (instanceManifest.transparent)
            {
                ModelManifest& modelManifest = _modelManifests[instanceManifest.modelID];
                MakeInstanceTransparent(changeTransparencyRequest.instanceID, instanceManifest, modelManifest);
            }

            InstanceData& instanceData = _instanceDatas[changeTransparencyRequest.instanceID];
            instanceData.opacity = changeTransparencyRequest.opacity;
            _instanceDatas.SetDirtyElement(changeTransparencyRequest.instanceID);
        }
        _instancesDirty = true;
    }

    u32 numChangeHighlightRequests = static_cast<u32>(_changeHighlightRequests.try_dequeue_bulk(_changeHighlightWork.begin(), 256));
    if (numChangeHighlightRequests > 0)
    {
        ZoneScopedN("Change Highlight Requests");
        for (u32 i = 0; i < numChangeHighlightRequests; i++)
        {
            ChangeHighlightRequest& changeHighlightRequest = _changeHighlightWork[i];

            InstanceData& instanceData = _instanceDatas[changeHighlightRequest.instanceID];
            instanceData.highlightIntensity = changeHighlightRequest.highlightIntensity;
            _instanceDatas.SetDirtyElement(changeHighlightRequest.instanceID);
        }
        _instancesDirty = true;
    }

    u32 numChangeSkyboxRequests = static_cast<u32>(_changeSkyboxRequests.try_dequeue_bulk(_changeSkyboxWork.begin(), 256));
    if (numChangeSkyboxRequests > 0)
    {
        ZoneScopedN("Change Skybox Requests");
        for (u32 i = 0; i < numChangeSkyboxRequests; i++)
        {
            ChangeSkyboxRequest& changeSkyboxRequest = _changeSkyboxWork[i];
            InstanceManifest& instanceManifest = _instanceManifests[changeSkyboxRequest.instanceID];
            instanceManifest.skybox = changeSkyboxRequest.skybox;

            MakeInstanceSkybox(changeSkyboxRequest.instanceID, instanceManifest, instanceManifest.skybox);
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
    _modelManifestsInstancesMutexes.clear();
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
    _uniqueDisplayInfoManifests.clear();

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
        Renderer::DescriptorSetResource modelSet;

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
            data.modelSet = builder.Use(resources.modelDescriptorSet);

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

            params.drawCallback = [&](DrawParams& drawParams)
            {
                drawParams.descriptorSets = {
                    &data.globalSet,
                    &data.modelSet
                };
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
        Renderer::DescriptorSetResource createIndirectAfterCullSet;
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
            params.createIndirectAfterCullSet = data.createIndirectAfterCullSet;

            params.numCascades = *CVarSystem::Get()->GetIntCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowCascadeNum"_h);
            params.occlusionCull = CVAR_ModelOcclusionCullingEnabled.Get();
            params.disableTwoStepCulling = CVAR_ModelDisableTwoStepCulling.Get();

            params.cullingDataIsWorldspace = false;
            params.debugDrawColliders = CVAR_ModelDrawOpaqueAABBs.Get();

            params.baseInstanceLookupOffset = offsetof(DrawCallData, baseInstanceLookupOffset);
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
        Renderer::DescriptorSetResource modelSet;
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
            data.modelSet = builder.Use(resources.modelDescriptorSet);
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

            params.drawCallback = [&](DrawParams& drawParams)
            {
                drawParams.descriptorSets = {
                    &data.globalSet,
                    &data.modelSet
                };
                Draw(resources, frameIndex, graphResources, commandList, drawParams);
            };

            params.numCascades = numCascades;

            params.biasConstantFactor = static_cast<f32>(*cvarSystem->GetFloatCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowDepthBiasConstant"));
            params.biasClamp = static_cast<f32>(*cvarSystem->GetFloatCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowDepthBiasClamp"));
            params.biasSlopeFactor = static_cast<f32>(*cvarSystem->GetFloatCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowDepthBiasSlope"));

            params.enableDrawing = CVAR_ModelDrawGeometry.Get();
            params.cullingEnabled = cullingEnabled;

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
        Renderer::DescriptorSetResource createIndirectAfterCullSet;
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
            params.createIndirectAfterCullSet = data.createIndirectAfterCullSet;

            params.numCascades = 0;// *CVarSystem::Get()->GetIntCVar(CVarCategory::Client | CVarCategory::Rendering, "numShadowCascades"_h);
            params.occlusionCull = CVAR_ModelOcclusionCullingEnabled.Get();
            params.disableTwoStepCulling = true; // Transparent objects don't write depth, so we don't need to two step cull them

            params.cullingDataIsWorldspace = false;
            params.debugDrawColliders = CVAR_ModelDrawTransparentAABBs.Get();

            params.baseInstanceLookupOffset = offsetof(DrawCallData, baseInstanceLookupOffset);
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
        Renderer::DescriptorSetResource modelSet;
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
            data.modelSet = builder.Use(resources.modelDescriptorSet);
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

            params.drawCallback = [&](DrawParams& drawParams)
            {
                drawParams.descriptorSets = {
                    &data.globalSet,
                    &data.modelSet,
                    &data.drawSet
                };
                DrawTransparent(resources, frameIndex, graphResources, commandList, drawParams);
            };

            params.numCascades = 0;

            params.enableDrawing = CVAR_ModelDrawGeometry.Get();
            params.cullingEnabled = cullingEnabled;

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
            Renderer::DescriptorSetResource modelSet;
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
                builder.Read(_opaqueSkyboxCullingResources.GetInstanceRefs().GetBuffer(), BufferUsage::GRAPHICS);
                builder.Read(_opaqueSkyboxCullingResources.GetCulledInstanceLookupTableBuffer(), BufferUsage::GRAPHICS);

                builder.Write(_animatedVertices.GetBuffer(), BufferUsage::GRAPHICS);

                data.drawCallsBuffer = builder.Write(_opaqueSkyboxCullingResources.GetDrawCalls().GetBuffer(), BufferUsage::GRAPHICS);
                data.culledDrawCallsBuffer = builder.Write(_opaqueSkyboxCullingResources.GetCulledDrawsBuffer(), BufferUsage::GRAPHICS);
                data.drawCountBuffer = builder.Write(_opaqueSkyboxCullingResources.GetDrawCountBuffer(), BufferUsage::TRANSFER | BufferUsage::GRAPHICS);
                data.triangleCountBuffer = builder.Write(_opaqueSkyboxCullingResources.GetTriangleCountBuffer(), BufferUsage::TRANSFER | BufferUsage::GRAPHICS);
                data.drawCountReadBackBuffer = builder.Write(_opaqueSkyboxCullingResources.GetDrawCountReadBackBuffer(), BufferUsage::TRANSFER);
                data.triangleCountReadBackBuffer = builder.Write(_opaqueSkyboxCullingResources.GetTriangleCountReadBackBuffer(), BufferUsage::TRANSFER);

                data.globalSet = builder.Use(resources.globalDescriptorSet);
                data.modelSet = builder.Use(resources.modelDescriptorSet);
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

                params.drawCallback = [&](DrawParams& drawParams)
                {
                    drawParams.descriptorSets = {
                        &data.globalSet,
                        &data.modelSet,
                        &data.drawSet
                    };
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
            Renderer::DescriptorSetResource modelSet;
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
                builder.Read(_textureDatas.GetBuffer(), BufferUsage::GRAPHICS);
                builder.Read(_textureUnits.GetBuffer(), BufferUsage::GRAPHICS);
                builder.Read(_instanceDatas.GetBuffer(), BufferUsage::GRAPHICS);
                builder.Read(_instanceMatrices.GetBuffer(), BufferUsage::GRAPHICS);
                builder.Read(_boneMatrices.GetBuffer(), BufferUsage::GRAPHICS);
                builder.Read(_textureTransformMatrices.GetBuffer(), BufferUsage::GRAPHICS | BufferUsage::COMPUTE);
                builder.Read(_transparentSkyboxCullingResources.GetDrawCallDatas().GetBuffer(), BufferUsage::GRAPHICS);
                builder.Read(_transparentSkyboxCullingResources.GetInstanceRefs().GetBuffer(), BufferUsage::GRAPHICS);
                builder.Read(_transparentSkyboxCullingResources.GetCulledInstanceLookupTableBuffer(), BufferUsage::GRAPHICS);

                builder.Write(_animatedVertices.GetBuffer(), BufferUsage::GRAPHICS);

                data.drawCallsBuffer = builder.Write(_transparentSkyboxCullingResources.GetDrawCalls().GetBuffer(), BufferUsage::GRAPHICS);
                data.culledDrawCallsBuffer = builder.Write(_transparentSkyboxCullingResources.GetCulledDrawsBuffer(), BufferUsage::GRAPHICS);
                data.drawCountBuffer = builder.Write(_transparentSkyboxCullingResources.GetDrawCountBuffer(), BufferUsage::TRANSFER | BufferUsage::GRAPHICS);
                data.triangleCountBuffer = builder.Write(_transparentSkyboxCullingResources.GetTriangleCountBuffer(), BufferUsage::TRANSFER | BufferUsage::GRAPHICS);
                data.drawCountReadBackBuffer = builder.Write(_transparentSkyboxCullingResources.GetDrawCountReadBackBuffer(), BufferUsage::TRANSFER);
                data.triangleCountReadBackBuffer = builder.Write(_transparentSkyboxCullingResources.GetTriangleCountReadBackBuffer(), BufferUsage::TRANSFER);

                data.globalSet = builder.Use(resources.globalDescriptorSet);
                data.modelSet = builder.Use(resources.modelDescriptorSet);
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

                params.drawCallback = [&](DrawParams& drawParams)
                {
                    drawParams.descriptorSets = {
                        &data.globalSet,
                        &data.modelSet,
                        &data.drawSet
                    };
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

Renderer::TextureID ModelRenderer::LoadTexture(const std::string& path, u32& arrayIndex)
{
    ZoneScopedN("ModelRenderer::LoadTexture");
    
    Renderer::TextureDesc textureDesc = 
    {
        .path = path,
    };

    return _renderer->LoadTextureIntoArray(textureDesc, _textures, arrayIndex);
}

u32 ModelRenderer::LoadModel(const std::string& name, Model::ComplexModel& model)
{
    ZoneScopedN("ModelRenderer::LoadModel");

    EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
    auto& textureSingleton = registries->dbRegistry->ctx().get<ECS::Singletons::TextureSingleton>();

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

        DrawCallOffsets drawCallOffsets;
        AllocateDrawCalls(modelOffsets.modelIndex, drawCallOffsets);

        modelManifest.opaqueDrawCallOffset = drawCallOffsets.opaqueDrawCallStartIndex;
        modelManifest.transparentDrawCallOffset = drawCallOffsets.transparentDrawCallStartIndex;

        const Renderer::GPUVector<Renderer::IndexedIndirectDraw>& opaqueDrawCalls = _opaqueCullingResources.GetDrawCalls();
        const Renderer::GPUVector<DrawCallData>& opaqueDrawCallDatas = _opaqueCullingResources.GetDrawCallDatas();

        const Renderer::GPUVector<Renderer::IndexedIndirectDraw>& transparentDrawCalls = _transparentCullingResources.GetDrawCalls();
        const Renderer::GPUVector<DrawCallData>& transparentDrawCallDatas = _transparentCullingResources.GetDrawCallDatas();

        u32 numAddedIndices = 0;

        u32 numAddedOpaqueDrawCalls = 0;
        u32 numAddedTransparentDrawCalls = 0;

        u32 textureTransformLookupTableSize = static_cast<u32>(model.textureTransformLookupTable.size());

        u32 numRenderBatches = static_cast<u32>(model.modelData.renderBatches.size());

        modelManifest.opaqueDrawIDToTextureDataID.reserve(numRenderBatches);
        modelManifest.transparentDrawIDToTextureDataID.reserve(numRenderBatches);
        modelManifest.opaqueSkyboxDrawIDToTextureDataID.reserve(numRenderBatches);
        modelManifest.transparentSkyboxDrawIDToTextureDataID.reserve(numRenderBatches);

        modelManifest.opaqueDrawIDToGroupID.reserve(numRenderBatches);
        modelManifest.transparentDrawIDToGroupID.reserve(numRenderBatches);
        modelManifest.opaqueSkyboxDrawIDToGroupID.reserve(numRenderBatches);
        modelManifest.transparentSkyboxDrawIDToGroupID.reserve(numRenderBatches);

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

            // If the draw call is transparent, add it to the originallyTransparentDrawIDs
            if (renderBatch.isTransparent)
            {
                modelManifest.originallyTransparentDrawIDs.insert(curDrawCallOffset);
            }

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

u32 ModelRenderer::AddPlacementInstance(entt::entity entityID, u32 modelID, u32 modelHash, Model::ComplexModel* model, const vec3& position, const quat& rotation, f32 scale, u32 doodadSet, bool canUseDoodadSet)
{
    // Add Instance matrix
    mat4x4 rotationMatrix = glm::toMat4(rotation);
    mat4x4 scaleMatrix = glm::scale(mat4x4(1.0f), vec3(scale));
    mat4x4 instanceMatrix = glm::translate(mat4x4(1.0f), position) * rotationMatrix * scaleMatrix;

    u32 instanceIndex = AddInstance(entityID, modelID, model, instanceMatrix);
    ModelManifest& manifest = _modelManifests[modelID];

    // Add Decorations
    if (canUseDoodadSet && manifest.numDecorationSets && manifest.numDecorations)
    {
        if (doodadSet == std::numeric_limits<u32>().max())
        {
            ModelLoader* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();

            // Load 0th doodadSet if it exists
            const Model::ComplexModel::DecorationSet& manifestDecorationSet = _modelDecorationSets[manifest.decorationSetOffset];

            for (u32 i = 0; i < manifestDecorationSet.count; i++)
            {
                const Model::ComplexModel::Decoration& manifestDecoration = _modelDecorations[manifest.decorationOffset + (manifestDecorationSet.index + i)];
                if (manifestDecoration.nameID == modelHash)
                    continue;

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
                    if (manifestDecoration.nameID == modelHash)
                        continue;

                    modelLoader->LoadDecoration(instanceIndex, manifestDecoration);
                }
            }
        }
    }

    return instanceIndex;
}

u32 ModelRenderer::AddInstance(entt::entity entityID, u32 modelID, Model::ComplexModel* model, const mat4x4& transformMatrix, u64 displayInfoPacked)
{
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
        manifest.instances.insert(instanceOffsets.instanceIndex);
    }

    // Set up InstanceManifest
    {
        InstanceManifest& instanceManifest = _instanceManifests[instanceOffsets.instanceIndex];
        instanceManifest.modelID = modelID;
    }

    if (model && displayInfoPacked != std::numeric_limits<u64>().max())
    {
        ReplaceTextureUnits(entityID, modelID, model, instanceOffsets.instanceIndex, displayInfoPacked);
    }

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    bool isSkybox = false;
    if (registry->valid(entityID))
    {
        isSkybox = registry->all_of<ECS::Components::SkyboxModelTag>(entityID);
    }

    if (isSkybox)
    {
        RequestChangeSkybox(instanceOffsets.instanceIndex, true);
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
        manifest.instances.erase(instanceID);
        manifest.skyboxInstances.erase(instanceID);
    }

    std::scoped_lock lock(_instanceOffsetsMutex);

    // Deallocate old animation data
    if (manifest.isAnimated)
    {
        DeallocateAnimation(instanceData.boneMatrixOffset, manifest.numBones, instanceData.textureTransformMatrixOffset, manifest.numTextureTransforms);
    }

    _instanceDatas.Remove(instanceID);
    _instanceMatrices.Remove(instanceID);

    // Reset InstanceManifest
    InstanceManifest& instanceManifest = _instanceManifests[instanceID];
    instanceManifest.modelID = 0;
    instanceManifest.displayInfoPacked = std::numeric_limits<u64>().max();
    instanceManifest.isDynamic = false;
    instanceManifest.visible = true;
    instanceManifest.transparent = false;
    instanceManifest.skybox = false;
    instanceManifest.enabledGroupIDs.clear();
    _instancesDirty = true;
}

void ModelRenderer::ModifyInstance(entt::entity entityID, u32 instanceID, u32 modelID, Model::ComplexModel* model, const mat4x4& transformMatrix, u64 displayInfoPacked)
{
    InstanceData& instanceData = _instanceDatas[instanceID];

    u32 oldModelID = instanceData.modelID;
    ModelManifest& oldManifest = _modelManifests[oldModelID];

    // Remove Instance from ModelManifest
    {
        std::scoped_lock lock(*_modelManifestsInstancesMutexes[oldModelID]);
        oldManifest.instances.erase(instanceID);
        oldManifest.skyboxInstances.erase(instanceID);
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
        newManifest.instances.insert(instanceID);
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

    InstanceManifest& instanceManifest = _instanceManifests[instanceID];
    instanceManifest.modelID = modelID;

    // Replace texture units
    if (model && displayInfoPacked != std::numeric_limits<u64>().max())
    {
        ReplaceTextureUnits(entityID, modelID, model, instanceID, displayInfoPacked);
    }

    if (instanceManifest.transparent)
    {
        MakeInstanceTransparent(instanceID, instanceManifest, newManifest);
    }

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    bool isSkybox = false;
    if (registry->valid(entityID))
    {
        isSkybox = registry->all_of<ECS::Components::SkyboxModelTag>(entityID);
    }
    instanceManifest.skybox = isSkybox;

    if (isSkybox)
    {
        MakeInstanceSkybox(instanceID, instanceManifest, true);
    }

    _instancesDirty = true;
}

void ModelRenderer::ReplaceTextureUnits(entt::entity entityID, u32 modelID, Model::ComplexModel* model, u32 instanceID, u64 displayInfoPacked)
{
    entt::registry* gameRegistry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    entt::registry* dbRegistry = ServiceLocator::GetEnttRegistries()->dbRegistry;
    auto& clientDBSingleton = dbRegistry->ctx().get<ECS::Singletons::ClientDBSingleton>();
    auto& itemSingleton = dbRegistry->ctx().get<ECS::Singletons::ItemSingleton>();
    auto& textureSingleton = dbRegistry->ctx().get<ECS::Singletons::TextureSingleton>();
    auto& unitCustomizationSingleton = dbRegistry->ctx().get<ECS::Singletons::UnitCustomizationSingleton>();
    auto* textureFileDataStorage = clientDBSingleton.Get(ClientDBHash::TextureFileData);
    auto* creatureDisplayInfoStorage = clientDBSingleton.Get(ClientDBHash::CreatureDisplayInfo);
    auto* creatureDisplayInfoExtraStorage = clientDBSingleton.Get(ClientDBHash::CreatureDisplayInfoExtra);
    auto* itemDisplayInfoStorage = clientDBSingleton.Get(ClientDBHash::ItemDisplayInfo);
    auto* itemDisplayModelMaterialStorage = clientDBSingleton.Get(ClientDBHash::ItemDisplayModelMaterialResources);

    const MetaGen::Shared::ClientDB::CreatureDisplayInfoRecord* creatureDisplayInfo = nullptr;
    const MetaGen::Shared::ClientDB::CreatureDisplayInfoExtraRecord* creatureDisplayInfoExtra = nullptr;
    const MetaGen::Shared::ClientDB::ItemDisplayInfoRecord* itemDisplayInfo = nullptr;
    ECS::Components::DisplayInfo* unitDisplayInfo = nullptr;
    ECS::Components::UnitCustomization* unitCustomization = nullptr;

    ModelManifest& manifest = _modelManifests[modelID];

    auto displayInfoType = static_cast<Database::Unit::DisplayInfoType>((displayInfoPacked >> 32) & 0x7);
    u32 displayID = displayInfoPacked & 0xFFFFFFFF;
    u32 extendedDisplayInfoID = (displayInfoPacked >> 35) & 0x1FFFFFF;
    bool isDynamicModel = displayInfoType == Database::Unit::DisplayInfoType::Creature && (displayInfoPacked >> 63) & 0x1;
    u64 displayInfoKey = isDynamicModel ? static_cast<u64>(entityID) : displayInfoPacked;

    robin_hood::unordered_map<u64, DisplayInfoManifest>* displayInfoManifests = isDynamicModel ? &_uniqueDisplayInfoManifests : &_displayInfoManifests;

    if (!displayInfoManifests->contains(displayInfoKey))
    {
        switch (displayInfoType)
        {
            case Database::Unit::DisplayInfoType::Creature:
            {
                if (creatureDisplayInfoStorage)
                {
                    creatureDisplayInfo = &creatureDisplayInfoStorage->Get<MetaGen::Shared::ClientDB::CreatureDisplayInfoRecord>(displayID);

                    if (creatureDisplayInfoExtraStorage && creatureDisplayInfo->extendedDisplayInfoID != 0)
                    {
                        creatureDisplayInfoExtra = &creatureDisplayInfoExtraStorage->Get<MetaGen::Shared::ClientDB::CreatureDisplayInfoExtraRecord>(creatureDisplayInfo->extendedDisplayInfoID);
                    }

                    if (gameRegistry->valid(entityID))
                    {
                        unitDisplayInfo = gameRegistry->try_get<ECS::Components::DisplayInfo>(entityID);
                        unitCustomization = gameRegistry->try_get<ECS::Components::UnitCustomization>(entityID);
                    }
                }

                break;
            }

            case Database::Unit::DisplayInfoType::Item:
            {
                if (itemDisplayInfoStorage)
                {
                    itemDisplayInfo = &itemDisplayInfoStorage->Get<MetaGen::Shared::ClientDB::ItemDisplayInfoRecord>(displayID);
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

        bool isPrebaked = false;
        bool useCustomSkin = false;

        GameDefine::UnitRace unitRace = GameDefine::UnitRace::None;
        GameDefine::UnitGender gender = GameDefine::UnitGender::None;

        if (creatureDisplayInfoExtra)
        {
            u32 bakedTextureHash = creatureDisplayInfoExtraStorage->GetStringHash(creatureDisplayInfoExtra->bakedTexture);
            isPrebaked = textureSingleton.textureHashToPath.contains(bakedTextureHash);
            useCustomSkin = !isPrebaked;

            unitRace = static_cast<GameDefine::UnitRace>(creatureDisplayInfoExtra->raceID);
            gender = static_cast<GameDefine::UnitGender>(creatureDisplayInfoExtra->gender);
        }
        else if (creatureDisplayInfo)
        {
            if (unitCustomizationSingleton.modelIDToUnitModelInfo.contains(creatureDisplayInfo->modelID))
            {
                auto& unitModelInfo = unitCustomizationSingleton.modelIDToUnitModelInfo[creatureDisplayInfo->modelID];
                unitRace = unitModelInfo.race;
                gender = unitModelInfo.gender;

                useCustomSkin = true;
            }
        }

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

                    if (cTexture.type == Model::ComplexModel::Texture::Type::None)
                    {
                        textureHash = cTexture.textureHash;
                    }
                    else if (cTexture.type == Model::ComplexModel::Texture::Type::Skin)
                    {
                        if (isPrebaked)
                        {
                            u32 bakedTextureHash = creatureDisplayInfoExtraStorage->GetStringHash(creatureDisplayInfoExtra->bakedTexture);
                            textureHash = bakedTextureHash;
                        }
                        else if (useCustomSkin)
                        {
                            if (unitDisplayInfo)
                            {
                                u64 textureUnitAddress = textureUnitOffset | (static_cast<u64>(j) << 32);
                                displayInfoManifest.skinTextureUnits.push_back(textureUnitAddress);
                            }

                            _textureUnits[textureUnitOffset].textureIds[j] = MODEL_INVALID_TEXTURE_ID;
                        }
                    }
                    else if (cTexture.type == Model::ComplexModel::Texture::Type::ObjectSkin)
                    {
                        if (itemDisplayInfo && itemDisplayModelMaterialStorage && textureFileDataStorage)
                        {
                            u32 materialResourcesID = itemDisplayInfo->modelMaterialResourcesID[0];
                            u64 itemDisplayModelMaterialKey = ECSUtil::Item::CreateItemDisplayModelMaterialResourcesKey(displayID, 0, (u8)cTexture.type, materialResourcesID);

                            if (itemSingleton.itemDisplayInfoMaterialResourcesKeyToID.contains(itemDisplayModelMaterialKey))
                            {
                                u32 modelMaterialResourcesRowID = itemSingleton.itemDisplayInfoMaterialResourcesKeyToID[itemDisplayModelMaterialKey];

                                if (itemDisplayModelMaterialStorage->Has(modelMaterialResourcesRowID))
                                {
                                    if (textureSingleton.materialResourcesIDToTextureHashes.contains(materialResourcesID))
                                    {
                                        const std::vector<u32>& textureHashes = textureSingleton.materialResourcesIDToTextureHashes[materialResourcesID];
                                        textureHash = textureHashes[0];
                                    }
                                }
                            }
                        }
                    }
                    else if (cTexture.type == Model::ComplexModel::Texture::Type::WeaponBlade)
                    {
                        if (itemDisplayInfo && itemDisplayModelMaterialStorage && textureFileDataStorage)
                        {
                            u32 materialResourcesID = itemDisplayInfo->modelMaterialResourcesID[0];
                            u64 itemDisplayModelMaterialKey = ECSUtil::Item::CreateItemDisplayModelMaterialResourcesKey(displayID, 0, (u8)cTexture.type, materialResourcesID);

                            if (itemSingleton.itemDisplayInfoMaterialResourcesKeyToID.contains(itemDisplayModelMaterialKey))
                            {
                                u32 modelMaterialResourcesRowID = itemSingleton.itemDisplayInfoMaterialResourcesKeyToID[itemDisplayModelMaterialKey];

                                if (itemDisplayModelMaterialStorage->Has(modelMaterialResourcesRowID))
                                {
                                    if (textureSingleton.materialResourcesIDToTextureHashes.contains(materialResourcesID))
                                    {
                                        const std::vector<u32>& textureHashes = textureSingleton.materialResourcesIDToTextureHashes[materialResourcesID];
                                        textureHash = textureHashes[0];
                                    }
                                }
                            }
                        }
                    }
                    else if (cTexture.type == Model::ComplexModel::Texture::Type::CharacterHair)
                    {
                        if (useCustomSkin)
                        {
                            u64 textureUnitAddress = textureUnitOffset | (static_cast<u64>(j) << 32);
                            displayInfoManifest.hairTextureUnits.push_back(textureUnitAddress);

                            _textureUnits[textureUnitOffset].textureIds[j] = MODEL_INVALID_TEXTURE_ID;
                        }
                        else if (creatureDisplayInfoExtra)
                        {
                            if (unitRace != GameDefine::UnitRace::None && gender != GameDefine::UnitGender::None)
                            {
                                Renderer::TextureID hairTextureID;
                                if (ECSUtil::UnitCustomization::GetHairTexture(unitCustomizationSingleton, unitRace, gender, creatureDisplayInfoExtra->hairStyleID, creatureDisplayInfoExtra->hairColorID, hairTextureID))
                                {
                                    _textureUnits[textureUnitOffset].textureIds[j] = _renderer->AddTextureToArray(hairTextureID, _textures);
                                }
                            }
                        }
                    }
                    else if (cTexture.type == Model::ComplexModel::Texture::Type::MonsterSkin1)
                    {
                        if (creatureDisplayInfo)
                        {
                            u32 textureStringIndex = creatureDisplayInfo->textureVariations[0];
                            const std::string& texturePath = creatureDisplayInfoStorage->GetString(textureStringIndex);
                            textureHash = StringUtils::fnv1a_32(texturePath.c_str(), texturePath.length());
                        }
                    }
                    else if (cTexture.type == Model::ComplexModel::Texture::Type::MonsterSkin2)
                    {
                        if (creatureDisplayInfo)
                        {
                            u32 textureStringIndex = creatureDisplayInfo->textureVariations[1];
                            const std::string& texturePath = creatureDisplayInfoStorage->GetString(textureStringIndex);
                            textureHash = StringUtils::fnv1a_32(texturePath.c_str(), texturePath.length());
                        }
                    }
                    else if (cTexture.type == Model::ComplexModel::Texture::Type::MonsterSkin3)
                    {
                        if (creatureDisplayInfo)
                        {
                            u32 textureStringIndex = creatureDisplayInfo->textureVariations[2];
                            const std::string& texturePath = creatureDisplayInfoStorage->GetString(textureStringIndex);
                            textureHash = StringUtils::fnv1a_32(texturePath.c_str(), texturePath.length());
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
        displayInfoManifests->insert({ displayInfoKey, std::move(displayInfoManifest) });
    }

    InstanceManifest& instanceManifest = _instanceManifests[instanceID];
    if (instanceManifest.displayInfoPacked != std::numeric_limits<u64>().max())
    {
        bool wasDynamicModel = (instanceManifest.displayInfoPacked >> 63) & 0x1;

        // Remove old dynamic displayInfoManifest if it exists
        if (wasDynamicModel)
        {
            std::scoped_lock lock(_displayInfoManifestsMutex);
            if (_uniqueDisplayInfoManifests.contains(instanceManifest.displayInfoPacked))
                _uniqueDisplayInfoManifests.erase(instanceManifest.displayInfoPacked);
        }
    }
    instanceManifest.displayInfoPacked = displayInfoKey;
    instanceManifest.isDynamic = isDynamicModel;
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

void ModelRenderer::RequestChangeSkinTexture(u32 instanceID, Renderer::TextureID textureID)
{
    ChangeSkinTextureRequest changeSkinTextureRequest =
    {
        .instanceID = instanceID,
        .textureID = textureID,
    };

    _changeSkinTextureRequests.enqueue(changeSkinTextureRequest);
}

void ModelRenderer::RequestChangeHairTexture(u32 instanceID, Renderer::TextureID textureID)
{
    ChangeHairTextureRequest changeHairTextureRequest =
    {
        .instanceID = instanceID,
        .textureID = textureID,
    };

    _changeHairTextureRequests.enqueue(changeHairTextureRequest);
}

void ModelRenderer::RequestChangeVisibility(u32 instanceID, bool visible)
{
    ChangeVisibilityRequest changeVisibilityRequest =
    {
        .instanceID = instanceID,
        .visible = visible,
    };

    _changeVisibilityRequests.enqueue(changeVisibilityRequest);
}

void ModelRenderer::RequestChangeTransparency(u32 instanceID, bool transparent, f32 opacity)
{
    ChangeTransparencyRequest changeTransparencyRequest =
    {
        .instanceID = instanceID,
        .transparent = transparent,
        .opacity = opacity
    };

    _changeTransparencyRequests.enqueue(changeTransparencyRequest);
}

void ModelRenderer::RequestChangeHighlight(u32 instanceID, f32 highlightIntensity)
{
    ChangeHighlightRequest changeHighlightRequest =
    {
        .instanceID = instanceID,
        .highlightIntensity = highlightIntensity
    };

    _changeHighlightRequests.enqueue(changeHighlightRequest);
}

void ModelRenderer::RequestChangeSkybox(u32 instanceID, bool skybox)
{
    ChangeSkyboxRequest changeSkyboxRequest =
    {
        .instanceID = instanceID,
        .skybox = skybox,
    };
    _changeSkyboxRequests.enqueue(changeSkyboxRequest);
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
    CreateModelPipelines();

    RenderResources& resources = _gameRenderer->GetRenderResources();

    Renderer::TextureArrayDesc textureArrayDesc;
    textureArrayDesc.size = Renderer::Settings::MAX_TEXTURES;

    _textures = _renderer->CreateTextureArray(textureArrayDesc);
    resources.modelDescriptorSet.Bind("_modelTextures"_h, _textures);

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
    _changeSkinTextureWork.resize(256);
    _changeHairTextureWork.resize(256);
    _changeVisibilityWork.resize(256);
    _changeTransparencyWork.resize(256);
    _changeHighlightWork.resize(256);
    _changeSkyboxWork.resize(256);

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
        resources.modelDescriptorSet.BindArray("_samplers"_h, _samplers[i], i);
    }

    CullingResourcesIndexed<DrawCallData>::InitParams initParams;
    initParams.renderer = _renderer;
    initParams.culledRenderer = this;
    initParams.bufferNamePrefix = "OpaqueModels";
    initParams.materialPassDescriptorSet = &resources.modelDescriptorSet;
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

    InitDescriptorSets();

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

void ModelRenderer::CreateModelPipelines()
{
    bool supportsExtendedTextures = _renderer->HasExtendedTextureSupport();

    // Regular Draws
    {
        Renderer::GraphicsPipelineDesc pipelineDesc;
        // Depth state
        pipelineDesc.states.depthStencilState.depthEnable = true;
        pipelineDesc.states.depthStencilState.depthWriteEnable = true;
        pipelineDesc.states.depthStencilState.depthFunc = Renderer::ComparisonFunc::GREATER;

        // Rasterizer state
        pipelineDesc.states.rasterizerState.cullMode = Renderer::CullMode::BACK;
        pipelineDesc.states.rasterizerState.frontFaceMode = Renderer::Settings::FRONT_FACE_STATE;

        pipelineDesc.states.renderTargetFormats[0] = Renderer::ImageFormat::R32G32_UINT; // Visibility buffer format
        pipelineDesc.states.depthStencilFormat = Renderer::DepthImageFormat::D32_FLOAT;

        Renderer::VertexShaderDesc vertexShaderDesc;
        std::vector<Renderer::PermutationField> vertexPermutationFields =
        {
            { "EDITOR_PASS", "0" },
            { "SHADOW_PASS", "0"}
        };
        u32 shaderEntryNameHash = Renderer::GetShaderEntryNameHash("Model/Draw.vs", vertexPermutationFields);
        vertexShaderDesc.shaderEntry = _gameRenderer->GetShaderEntry(shaderEntryNameHash, "Model/Draw.vs");
        pipelineDesc.states.vertexShader = _renderer->LoadShader(vertexShaderDesc);
            
        Renderer::PixelShaderDesc pixelShaderDesc;
        std::vector<Renderer::PermutationField> pixelPermutationFields =
        {
            { "SHADOW_PASS", "0" }
        };
        shaderEntryNameHash = Renderer::GetShaderEntryNameHash("Model/Draw.ps", pixelPermutationFields);
        pixelShaderDesc.shaderEntry = _gameRenderer->GetShaderEntry(shaderEntryNameHash, "Model/Draw.ps");
        pipelineDesc.states.pixelShader = _renderer->LoadShader(pixelShaderDesc);

        _drawPipeline = _renderer->CreatePipeline(pipelineDesc);
    }
    // Shadows
    {
        Renderer::GraphicsPipelineDesc pipelineDesc;
        // Rasterizer state
        pipelineDesc.states.rasterizerState.cullMode = Renderer::CullMode::NONE;
        pipelineDesc.states.rasterizerState.depthBiasEnabled = true;
        pipelineDesc.states.rasterizerState.depthClampEnabled = true;

        pipelineDesc.states.depthStencilFormat = Renderer::DepthImageFormat::D32_FLOAT;

        Renderer::VertexShaderDesc vertexShaderDesc;
        std::vector<Renderer::PermutationField> vertexPermutationFields =
        {
            { "EDITOR_PASS", "0" },
            { "SHADOW_PASS", "1"}
        };
        u32 shaderEntryNameHash = Renderer::GetShaderEntryNameHash("Model/Draw.vs", vertexPermutationFields);
        vertexShaderDesc.shaderEntry = _gameRenderer->GetShaderEntry(shaderEntryNameHash, "Model/Draw.vs");
        pipelineDesc.states.vertexShader = _renderer->LoadShader(vertexShaderDesc);
            
        Renderer::PixelShaderDesc pixelShaderDesc;
        std::vector<Renderer::PermutationField> pixelPermutationFields =
        {
            { "SHADOW_PASS", "1" }
        };
        shaderEntryNameHash = Renderer::GetShaderEntryNameHash("Model/Draw.ps", pixelPermutationFields);
        pixelShaderDesc.shaderEntry = _gameRenderer->GetShaderEntry(shaderEntryNameHash, "Model/Draw.ps");
        pipelineDesc.states.pixelShader = _renderer->LoadShader(pixelShaderDesc);
            
        // Draw
        _drawShadowPipeline = _renderer->CreatePipeline(pipelineDesc);
    }
    // Transparencies
    {
        Renderer::GraphicsPipelineDesc pipelineDesc;
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

        pipelineDesc.states.renderTargetFormats[0] = Renderer::ImageFormat::R16G16B16A16_FLOAT; // Transparency format
        pipelineDesc.states.renderTargetFormats[1] = Renderer::ImageFormat::R16_FLOAT; // Transparency weight format
        pipelineDesc.states.depthStencilFormat = Renderer::DepthImageFormat::D32_FLOAT;

        Renderer::VertexShaderDesc vertexShaderDesc;
        vertexShaderDesc.shaderEntry = _gameRenderer->GetShaderEntry("Model/DrawTransparent.vs"_h, "Model/DrawTransparent.vs");
        pipelineDesc.states.vertexShader = _renderer->LoadShader(vertexShaderDesc);
            

        Renderer::PixelShaderDesc pixelShaderDesc;
        pixelShaderDesc.shaderEntry = _gameRenderer->GetShaderEntry("Model/DrawTransparent.ps"_h, "Model/DrawTransparent.ps");
        pipelineDesc.states.pixelShader = _renderer->LoadShader(pixelShaderDesc);

        // Draw
        _drawTransparentPipeline = _renderer->CreatePipeline(pipelineDesc);
    }
    // Skybox Opaque
    {
        Renderer::GraphicsPipelineDesc pipelineDesc;
        // Depth state
        pipelineDesc.states.depthStencilState.depthEnable = true;
        pipelineDesc.states.depthStencilState.depthWriteEnable = true;
        pipelineDesc.states.depthStencilState.depthFunc = Renderer::ComparisonFunc::GREATER;

        // Rasterizer state
        pipelineDesc.states.rasterizerState.cullMode = Renderer::CullMode::BACK;
        pipelineDesc.states.rasterizerState.frontFaceMode = Renderer::Settings::FRONT_FACE_STATE;

        // Render targets
        pipelineDesc.states.renderTargetFormats[0] = _renderer->GetSwapChainImageFormat();
        pipelineDesc.states.depthStencilFormat = Renderer::DepthImageFormat::D32_FLOAT;

        Renderer::VertexShaderDesc vertexShaderDesc;
        vertexShaderDesc.shaderEntry = _gameRenderer->GetShaderEntry("Model/DrawSkybox.vs"_h, "Model/DrawSkybox.vs");
        pipelineDesc.states.vertexShader = _renderer->LoadShader(vertexShaderDesc);

        Renderer::PixelShaderDesc pixelShaderDesc;
        std::vector<Renderer::PermutationField> pixelPermutationFields =
        {
            { "TRANSPARENCY", "0"  }
        };
        u32 shaderEntryNameHash = Renderer::GetShaderEntryNameHash("Model/DrawSkybox.ps", pixelPermutationFields);
        pixelShaderDesc.shaderEntry = _gameRenderer->GetShaderEntry(shaderEntryNameHash, "Model/DrawSkybox.ps");
        pipelineDesc.states.pixelShader = _renderer->LoadShader(pixelShaderDesc);

        // Draw
        _drawSkyboxOpaquePipeline = _renderer->CreatePipeline(pipelineDesc);
    }
    // Skybox Transparent
    {
        Renderer::GraphicsPipelineDesc pipelineDesc;
        // Depth state
        pipelineDesc.states.depthStencilState.depthEnable = false; // Is this really correct?
        pipelineDesc.states.depthStencilState.depthWriteEnable = false;
        pipelineDesc.states.depthStencilState.depthFunc = Renderer::ComparisonFunc::GREATER;

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

        // Rasterizer state
        pipelineDesc.states.rasterizerState.cullMode = Renderer::CullMode::BACK;
        pipelineDesc.states.rasterizerState.frontFaceMode = Renderer::Settings::FRONT_FACE_STATE;

        // Render targets
        pipelineDesc.states.renderTargetFormats[0] = Renderer::ImageFormat::R16G16B16A16_FLOAT; // Transparency format
        pipelineDesc.states.renderTargetFormats[1] = Renderer::ImageFormat::R16_FLOAT; // Transparency weight format
        pipelineDesc.states.depthStencilFormat = Renderer::DepthImageFormat::D32_FLOAT;

        Renderer::VertexShaderDesc vertexShaderDesc;
        vertexShaderDesc.shaderEntry = _gameRenderer->GetShaderEntry("Model/DrawSkybox.vs"_h, "Model/DrawSkybox.vs");
        pipelineDesc.states.vertexShader = _renderer->LoadShader(vertexShaderDesc);

        Renderer::PixelShaderDesc pixelShaderDesc;
        std::vector<Renderer::PermutationField> pixelPermutationFields =
        {
            { "TRANSPARENCY", "1"  }
        };
        u32 shaderEntryNameHash = Renderer::GetShaderEntryNameHash("Model/DrawSkybox.ps", pixelPermutationFields);
        pixelShaderDesc.shaderEntry = _gameRenderer->GetShaderEntry(shaderEntryNameHash, "Model/DrawSkybox.ps");
        pipelineDesc.states.pixelShader = _renderer->LoadShader(pixelShaderDesc);

        // Draw
        _drawSkyboxTransparentPipeline = _renderer->CreatePipeline(pipelineDesc);
    }
}

void ModelRenderer::InitDescriptorSets()
{
    // Opaque
    {
        Renderer::DescriptorSet& geometryPassDescriptorSet = _opaqueCullingResources.GetGeometryPassDescriptorSet();
        geometryPassDescriptorSet.RegisterPipeline(_renderer, _drawPipeline);
        geometryPassDescriptorSet.Init(_renderer);
    }

    // Transparent
    {
        Renderer::DescriptorSet& geometryPassDescriptorSet = _transparentCullingResources.GetGeometryPassDescriptorSet();
        geometryPassDescriptorSet.RegisterPipeline(_renderer, _drawTransparentPipeline);
        geometryPassDescriptorSet.Init(_renderer);
    }

    // Opaque Skybox
    {
        Renderer::DescriptorSet& geometryPassDescriptorSet = _opaqueSkyboxCullingResources.GetGeometryPassDescriptorSet();
        geometryPassDescriptorSet.RegisterPipeline(_renderer, _drawSkyboxOpaquePipeline);
        geometryPassDescriptorSet.Init(_renderer);
    }

    // Transparent Skybox
    {
        Renderer::DescriptorSet& geometryPassDescriptorSet = _transparentSkyboxCullingResources.GetGeometryPassDescriptorSet();
        geometryPassDescriptorSet.RegisterPipeline(_renderer, _drawSkyboxTransparentPipeline);
        geometryPassDescriptorSet.Init(_renderer);
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

void ModelRenderer::AllocateDrawCalls(u32 modelID, DrawCallOffsets& offsets)
{
    std::scoped_lock lock(_drawCallOffsetsMutex);

    ModelManifest& manifest = _modelManifests[modelID];

    offsets.opaqueDrawCallStartIndex = _opaqueCullingResources.AddCount(manifest.numOpaqueDrawCalls);
    offsets.transparentDrawCallStartIndex = _transparentCullingResources.AddCount(manifest.numTransparentDrawCalls);
}

void ModelRenderer::DeallocateAnimation(u32 boneStartIndex, u32 numBones, u32 textureTransformStartIndex, u32 numTextureTransforms)
{
    std::scoped_lock lock(_animationOffsetsMutex);

    _boneMatrices.Remove(boneStartIndex, numBones);
    _textureTransformMatrices.Remove(textureTransformStartIndex, numTextureTransforms);
}

void ModelRenderer::MakeInstanceTransparent(u32 instanceID, InstanceManifest& instanceManifest, ModelManifest& modelManifest)
{
    if (!modelManifest.hasTemporarilyTransparentDrawCalls)
    {
        // Allocate drawcalls in the transparent culling resources
        modelManifest.temporarilyTransparentDrawCallOffset = _transparentCullingResources.AddCount(modelManifest.numOpaqueDrawCalls);

        // Copy the opaque drawcalls to the transparent culling resources
        auto& opaqueDrawCalls = _opaqueCullingResources.GetDrawCalls();
        auto& transparentDrawCalls = _transparentCullingResources.GetDrawCalls();

        auto& opaqueDrawCallDatas = _opaqueCullingResources.GetDrawCallDatas();
        auto& transparentDrawCallDatas = _transparentCullingResources.GetDrawCallDatas();

        u32 destinationOffset = modelManifest.temporarilyTransparentDrawCallOffset;
        u32 sourceOffset = modelManifest.opaqueDrawCallOffset;

        void* destinationDrawCalls = &transparentDrawCalls[destinationOffset];
        void* sourceDrawCalls = &opaqueDrawCalls[sourceOffset];
        memcpy(destinationDrawCalls, sourceDrawCalls, modelManifest.numOpaqueDrawCalls * sizeof(Renderer::IndexedIndirectDraw));

        void* destinationDrawCallDatas = &transparentDrawCallDatas[destinationOffset];
        void* sourceDrawCallDatas = &opaqueDrawCallDatas[sourceOffset];
        memcpy(destinationDrawCallDatas, sourceDrawCallDatas, modelManifest.numOpaqueDrawCalls * sizeof(DrawCallData));

        // Set up transparentDrawIDToTextureDataID and transparentDrawIDToGroupID
        for (u32 drawID = 0; drawID < modelManifest.numOpaqueDrawCalls; drawID++)
        {
            u32 transparentDrawID = modelManifest.temporarilyTransparentDrawCallOffset + drawID;
            u32 opaqueDrawID = modelManifest.opaqueDrawCallOffset + drawID;

            modelManifest.transparentDrawIDToTextureDataID[transparentDrawID] = modelManifest.opaqueDrawIDToTextureDataID[opaqueDrawID];
            modelManifest.transparentDrawIDToGroupID[transparentDrawID] = modelManifest.opaqueDrawIDToGroupID[opaqueDrawID];
        }

        modelManifest.hasTemporarilyTransparentDrawCalls = true;
    }

    DisplayInfoManifest* displayInfoManifest = instanceManifest.isDynamic ? &_uniqueDisplayInfoManifests[instanceManifest.displayInfoPacked] : &_displayInfoManifests[instanceManifest.displayInfoPacked];

    if (!displayInfoManifest->hasTemporarilyTransparentDrawCalls)
    {
        // Set up transparentDrawIDToTextureDataID
        for (u32 drawID = 0; drawID < modelManifest.numOpaqueDrawCalls; drawID++)
        {
            u32 transparentDrawID = modelManifest.temporarilyTransparentDrawCallOffset + drawID;
            u32 opaqueDrawID = modelManifest.opaqueDrawCallOffset + drawID;

            if (displayInfoManifest->overrideTextureDatas)
            {
                displayInfoManifest->transparentDrawIDToTextureDataID[transparentDrawID] = displayInfoManifest->opaqueDrawIDToTextureDataID[opaqueDrawID];
            }
        }
        displayInfoManifest->hasTemporarilyTransparentDrawCalls = true;
    }
}

void ModelRenderer::MakeInstanceSkybox(u32 instanceID, InstanceManifest& instanceManifest, bool skybox)
{
    ModelManifest& modelManifest = _modelManifests[instanceManifest.modelID];
    if (skybox)
    {
        if (!modelManifest.hasSkyboxDrawCalls)
        {
            // Allocate opaque drawcalls in the skybox culling resources
            modelManifest.opaqueSkyboxDrawCallOffset = _opaqueSkyboxCullingResources.AddCount(modelManifest.numOpaqueDrawCalls);
            
            // Copy the opaque drawcalls to the opaque skybox culling resources
            auto& opaqueDrawCalls = _opaqueCullingResources.GetDrawCalls();
            auto& opaqueSkyboxDrawCalls = _opaqueSkyboxCullingResources.GetDrawCalls();

            auto& opaqueDrawCallDatas = _opaqueCullingResources.GetDrawCallDatas();
            auto& opaqueSkyboxDrawCallDatas = _opaqueSkyboxCullingResources.GetDrawCallDatas();

            u32 destinationOffset = modelManifest.opaqueSkyboxDrawCallOffset;
            u32 sourceOffset = modelManifest.opaqueDrawCallOffset;

            void* destinationDrawCalls = &opaqueSkyboxDrawCalls[destinationOffset];
            void* sourceDrawCalls = &opaqueDrawCalls[sourceOffset];
            memcpy(destinationDrawCalls, sourceDrawCalls, modelManifest.numOpaqueDrawCalls * sizeof(Renderer::IndexedIndirectDraw));

            void* destinationDrawCallDatas = &opaqueSkyboxDrawCallDatas[destinationOffset];
            void* sourceDrawCallDatas = &opaqueDrawCallDatas[sourceOffset];
            memcpy(destinationDrawCallDatas, sourceDrawCallDatas, modelManifest.numOpaqueDrawCalls * sizeof(DrawCallData));

            // Set up opaqueSkyboxDrawIDToTextureDataID and opaqueSkyboxDrawIDToGroupID
            for (u32 drawID = 0; drawID < modelManifest.numOpaqueDrawCalls; drawID++)
            {
                u32 opaqueSkyboxDrawID = modelManifest.opaqueSkyboxDrawCallOffset + drawID;
                u32 opaqueDrawID = modelManifest.opaqueDrawCallOffset + drawID;
                modelManifest.opaqueSkyboxDrawIDToTextureDataID[opaqueSkyboxDrawID] = modelManifest.opaqueDrawIDToTextureDataID[opaqueDrawID];
                modelManifest.opaqueSkyboxDrawIDToGroupID[opaqueSkyboxDrawID] = modelManifest.opaqueDrawIDToGroupID[opaqueDrawID];
            }

            // Allocate transparent drawcalls in the skybox culling resources
            modelManifest.transparentSkyboxDrawCallOffset = _transparentSkyboxCullingResources.AddCount(modelManifest.numTransparentDrawCalls);

            // Copy the transparent drawcalls to the transparent skybox culling resources
            auto& transparentDrawCalls = _transparentCullingResources.GetDrawCalls();
            auto& transparentSkyboxDrawCalls = _transparentSkyboxCullingResources.GetDrawCalls();

            auto& transparentDrawCallDatas = _transparentCullingResources.GetDrawCallDatas();
            auto& transparentSkyboxDrawCallDatas = _transparentSkyboxCullingResources.GetDrawCallDatas();

            destinationOffset = modelManifest.transparentSkyboxDrawCallOffset;
            sourceOffset = modelManifest.transparentDrawCallOffset;

            destinationDrawCalls = &transparentSkyboxDrawCalls[destinationOffset];
            sourceDrawCalls = &transparentDrawCalls[sourceOffset];
            memcpy(destinationDrawCalls, sourceDrawCalls, modelManifest.numTransparentDrawCalls * sizeof(Renderer::IndexedIndirectDraw));

            destinationDrawCallDatas = &transparentSkyboxDrawCallDatas[destinationOffset];
            sourceDrawCallDatas = &transparentDrawCallDatas[sourceOffset];
            memcpy(destinationDrawCallDatas, sourceDrawCallDatas, modelManifest.numTransparentDrawCalls * sizeof(DrawCallData));

            // Set up transparentSkyboxDrawIDToTextureDataID and transparentSkyboxDrawIDToGroupID
            for (u32 drawID = 0; drawID < modelManifest.numTransparentDrawCalls; drawID++)
            {
                u32 transparentSkyboxDrawID = modelManifest.transparentSkyboxDrawCallOffset + drawID;
                u32 transparentDrawID = modelManifest.transparentDrawCallOffset + drawID;
                modelManifest.transparentSkyboxDrawIDToTextureDataID[transparentSkyboxDrawID] = modelManifest.transparentDrawIDToTextureDataID[transparentDrawID];
                modelManifest.transparentSkyboxDrawIDToGroupID[transparentSkyboxDrawID] = modelManifest.transparentDrawIDToGroupID[transparentDrawID];
            }

            modelManifest.hasSkyboxDrawCalls = true;
        }

        DisplayInfoManifest* displayInfoManifest = instanceManifest.isDynamic ? &_uniqueDisplayInfoManifests[instanceManifest.displayInfoPacked] : &_displayInfoManifests[instanceManifest.displayInfoPacked];

        if (!displayInfoManifest->hasSkyboxDrawCalls && displayInfoManifest->overrideTextureDatas)
        {
            // Set up opaqueSkyboxDrawIDToTextureDataID
            for (u32 drawID = 0; drawID < modelManifest.numOpaqueDrawCalls; drawID++)
            {
                u32 opaqueSkyboxDrawID = modelManifest.opaqueSkyboxDrawCallOffset + drawID;
                u32 opaqueDrawID = modelManifest.opaqueDrawCallOffset + drawID;

                displayInfoManifest->opaqueSkyboxDrawIDToTextureDataID[opaqueSkyboxDrawID] = displayInfoManifest->opaqueDrawIDToTextureDataID[opaqueDrawID];
            }

            // Set up transparentSkyboxDrawIDToTextureDataID
            for (u32 drawID = 0; drawID < modelManifest.numTransparentDrawCalls; drawID++)
            {
                u32 transparentSkyboxDrawID = modelManifest.transparentSkyboxDrawCallOffset + drawID;
                u32 transparentDrawID = modelManifest.transparentDrawCallOffset + drawID;

                displayInfoManifest->transparentSkyboxDrawIDToTextureDataID[transparentSkyboxDrawID] = displayInfoManifest->transparentDrawIDToTextureDataID[transparentDrawID];
            }

            displayInfoManifest->hasSkyboxDrawCalls = true;
        }

        // Add the skybox instance
        modelManifest.skyboxInstances.insert(instanceID);

        // Remove the non skybox instance
        modelManifest.instances.erase(instanceID);
    }
    else
    {
        // Remove the skybox instance
        modelManifest.skyboxInstances.erase(instanceID);
        
        // Add the non skybox instance
        modelManifest.instances.insert(instanceID);
    }
}

void ModelRenderer::CompactInstanceRefs()
{
    ZoneScopedN("ModelRenderer::CompactInstanceRefs");

    if (!_instancesDirty)
        return;

    // Create array of culling resources that we can loop over
    CullingResourcesIndexed<DrawCallData>* cullingResources[] = {
        &_opaqueCullingResources,
        &_transparentCullingResources,
        &_opaqueSkyboxCullingResources,
        &_transparentSkyboxCullingResources
    };

    static bool cullingResourcesIsTransparent[] = {
        false,
        true,
        false,
        true
    };

    static bool cullingResourcesIsSkybox[] = {
        false,
        false,
        true,
        true
    };

    static u32 reserveSizes[] = {
        1000000,
        100000,
        1000,
        1000
    };

    // Loop over all the culling resources
    for(u32 i = 0; i < 4; i++)
    {
        ZoneScopedN("CullingResource");

        auto& cullingResource = cullingResources[i];
        bool isTransparent = cullingResourcesIsTransparent[i];
        bool isSkybox = cullingResourcesIsSkybox[i];
        u32 numTotalInstances = 0;

        auto& instanceRefs = cullingResource->GetInstanceRefs();
        {
            ZoneScopedN("Clear InstanceRefs");
            instanceRefs.Clear();
            instanceRefs.Reserve(reserveSizes[i]);
        }

        const auto& draws = cullingResource->GetDrawCalls();
        const auto& drawDatas = cullingResource->GetDrawCallDatas();

        u32 numDraws = draws.Count();
        for (u32 drawID = 0; drawID < numDraws; drawID++)
        {
            ZoneScopedN("Draw");

            Renderer::IndexedIndirectDraw& draw = draws[drawID];
            DrawCallData& drawData = drawDatas[drawID];
            const ModelManifest& manifest = _modelManifests[drawData.modelID];

            // Select the correct drawIDToTextureDataID and drawIDToGroupID based on isSkybox and isTransparent
            const robin_hood::unordered_map<u32, u32>& opaqueDrawIDToTextureDataID = (isSkybox) ? manifest.opaqueSkyboxDrawIDToTextureDataID : manifest.opaqueDrawIDToTextureDataID;
            const robin_hood::unordered_map<u32, u32>& transparentDrawIDToTextureDataID = (isSkybox) ? manifest.transparentSkyboxDrawIDToTextureDataID : manifest.transparentDrawIDToTextureDataID;
            const robin_hood::unordered_map<u32, u32>& drawIDToTextureDataID = (isTransparent) ? transparentDrawIDToTextureDataID : opaqueDrawIDToTextureDataID;

            const robin_hood::unordered_map<u32, u32>& opaqueDrawIDToGroupID = (isSkybox) ? manifest.opaqueSkyboxDrawIDToGroupID : manifest.opaqueDrawIDToGroupID;
            const robin_hood::unordered_map<u32, u32>& transparentDrawIDToGroupID = (isSkybox) ? manifest.transparentSkyboxDrawIDToGroupID : manifest.transparentDrawIDToGroupID;
            const robin_hood::unordered_map<u32, u32>& drawIDToGroupID = (isTransparent) ? transparentDrawIDToGroupID : opaqueDrawIDToGroupID;

            u32 defaultTextureDataID = drawIDToTextureDataID.at(drawID);

            auto& instances = (isSkybox) ? manifest.skyboxInstances : manifest.instances;

            u32 numTotalInstancesInModel = static_cast<u32>(instances.size());
            {
                ZoneScopedN("AddCount");
                instanceRefs.AddCount(numTotalInstancesInModel);
            }

            u32 numEnabledInstancesInDraw = 0;

            // Models with default transparency
            for(auto& instanceID : instances)
            {
                ZoneScopedN("Instance");
                u32 textureDataID = defaultTextureDataID;

                InstanceManifest& instanceManifest = _instanceManifests[instanceID];

                // Check if this instance is visible
                if (!instanceManifest.visible)
                    continue;

                // Don't draw transparent instances in opaque draw calls
                if (instanceManifest.transparent && !isTransparent)
                    continue;

                // Don't draw opaque instances in temporarily transparent draw calls
                if (manifest.hasTemporarilyTransparentDrawCalls)
                {
                    bool originallyTransparent = manifest.originallyTransparentDrawIDs.contains(drawID);
                    if (!originallyTransparent)
                    {
                        if (!instanceManifest.transparent && isTransparent && !originallyTransparent && drawID >= manifest.transparentDrawCallOffset)
                            continue;
                    }
                }

                // Check if this instance has this draw enabled
                {
                    u32 groupID = drawIDToGroupID.at(drawID);

                    if (groupID != 0)
                    {
                        if (!instanceManifest.enabledGroupIDs.contains(groupID))
                            continue;
                    }
                }

                {
                    ZoneScopedN("Setup InstanceRef");

                    if (instanceManifest.displayInfoPacked != std::numeric_limits<u64>().max())
                    {
                        DisplayInfoManifest* displayInfoManifest = instanceManifest.isDynamic ? &_uniqueDisplayInfoManifests[instanceManifest.displayInfoPacked] : &_displayInfoManifests[instanceManifest.displayInfoPacked];

                        // Select the correct instanceDrawIDToTextureDataID based on isSkybox and isTransparent
                        const robin_hood::unordered_map<u32, u32>& displayInfoManifestOpaqueDrawIDToTextureDataID = (isSkybox) ? displayInfoManifest->opaqueSkyboxDrawIDToTextureDataID : displayInfoManifest->opaqueDrawIDToTextureDataID;
                        const robin_hood::unordered_map<u32, u32>& displayInfoManifestTransparentDrawIDToTextureDataID = (isSkybox) ? displayInfoManifest->transparentSkyboxDrawIDToTextureDataID : displayInfoManifest->transparentDrawIDToTextureDataID;

                        const robin_hood::unordered_map<u32, u32>& instanceDrawIDToTextureDataID = (isTransparent || instanceManifest.transparent) ? displayInfoManifestTransparentDrawIDToTextureDataID : displayInfoManifestOpaqueDrawIDToTextureDataID;
                        textureDataID = instanceDrawIDToTextureDataID.at(drawID);
                    }

                    InstanceRef& instanceRef = instanceRefs[numTotalInstances + numEnabledInstancesInDraw];
                    instanceRef.drawID = drawID;
                    instanceRef.instanceID = instanceID;
                    instanceRef.extraID = textureDataID;
                    numEnabledInstancesInDraw++;
                }
            }

            {
                ZoneScopedN("Setup Draw");
                draw.firstInstance = numTotalInstances;
                draw.instanceCount = numEnabledInstancesInDraw;

                drawData.baseInstanceLookupOffset = numTotalInstances;
                numTotalInstances += numEnabledInstancesInDraw;
            }
        }

        u32 numInstanceRefs = instanceRefs.Count();
        if (numInstanceRefs > numTotalInstances)
        {
            ZoneScopedN("Remove InstanceRefs");
            instanceRefs.Remove(numTotalInstances, numInstanceRefs - numTotalInstances);
        }

        cullingResource->SetDirty();
    }
}

void ModelRenderer::SyncToGPU()
{
    ZoneScopedN("ModelRenderer::SyncToGPU");
    RenderResources& resources = _gameRenderer->GetRenderResources();

    CulledRenderer::SyncToGPU();

    // Sync Vertex buffer to GPU
    {
        if (_vertices.SyncToGPU(_renderer))
        {
            resources.modelDescriptorSet.Bind("_packedModelVertices"_h, _vertices.GetBuffer());
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
            resources.modelDescriptorSet.Bind("_animatedModelVertexPositions"_h, _animatedVertices.GetBuffer());
        }
    }

    // Sync Index buffer to GPU
    {
        if (_indices.SyncToGPU(_renderer))
        {
            resources.modelDescriptorSet.Bind("_modelIndices"_h, _indices.GetBuffer());
        }
    }

    // Sync TextureDatas buffer to GPU
    {
        if (_textureDatas.SyncToGPU(_renderer))
        {
            resources.modelDescriptorSet.Bind("_packedModelTextureDatas"_h, _textureDatas.GetBuffer());
        }
    }

    // Sync TextureUnit buffer to GPU
    {
        if (_textureUnits.SyncToGPU(_renderer))
        {
            resources.modelDescriptorSet.Bind("_modelTextureUnits"_h, _textureUnits.GetBuffer());
        }
    }

    // Sync InstanceDatas buffer to GPU
    {
        if (_instanceDatas.SyncToGPU(_renderer))
        {
            resources.modelDescriptorSet.Bind("_modelInstanceDatas"_h, _instanceDatas.GetBuffer());
        }
    }

    // Sync InstanceMatrices buffer to GPU
    {
        if (_instanceMatrices.SyncToGPU(_renderer))
        {
            _opaqueCullingResources.GetCullingDescriptorSet().Bind("_instanceMatrices"_h, _instanceMatrices.GetBuffer());
            _transparentCullingResources.GetCullingDescriptorSet().Bind("_instanceMatrices"_h, _instanceMatrices.GetBuffer());

            resources.modelDescriptorSet.Bind("_modelInstanceMatrices"_h, _instanceMatrices.GetBuffer());
        }
    }

    // Sync BoneMatrices buffer to GPU
    {
        if (_boneMatrices.SyncToGPU(_renderer))
        {
            resources.modelDescriptorSet.Bind("_instanceBoneMatrices"_h, _boneMatrices.GetBuffer());
        }
    }

    // Sync TextureTransformMatrices buffer to GPU
    {
        if (_textureTransformMatrices.SyncToGPU(_renderer))
        {
            resources.modelDescriptorSet.Bind("_instanceTextureTransformMatrices"_h, _textureTransformMatrices.GetBuffer());
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
    Renderer::RenderPassDesc renderPassDesc;
    graphResources.InitializeRenderPassDesc(renderPassDesc);

    // Render targets
    if (!params.shadowPass)
    {
        renderPassDesc.renderTargets[0] = params.rt0;
        if (params.rt1 != Renderer::ImageMutableResource::Invalid())
        {
            renderPassDesc.renderTargets[1] = params.rt1;
        }
    }
    renderPassDesc.depthStencil = params.depth;
    commandList.BeginRenderPass(renderPassDesc);

    Renderer::GraphicsPipelineID pipeline = params.shadowPass ? _drawShadowPipeline : _drawPipeline;
    commandList.BeginPipeline(pipeline);

    struct PushConstants
    {
        u32 viewIndex;
    };

    PushConstants* constants = graphResources.FrameNew<PushConstants>();

    constants->viewIndex = params.viewIndex;
    commandList.PushConstant(constants, 0, sizeof(PushConstants));

    for (auto& descriptorSet : params.descriptorSets)
    {
        commandList.BindDescriptorSet(*descriptorSet, frameIndex);
    }

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
    commandList.EndRenderPass(renderPassDesc);
}

void ModelRenderer::DrawTransparent(const RenderResources& resources, u8 frameIndex, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, const DrawParams& params)
{
    Renderer::RenderPassDesc renderPassDesc;
    graphResources.InitializeRenderPassDesc(renderPassDesc);

    // Render targets
    renderPassDesc.renderTargets[0] = params.rt0;
    renderPassDesc.renderTargets[1] = params.rt1;

    renderPassDesc.depthStencil = params.depth;
    commandList.BeginRenderPass(renderPassDesc);

    Renderer::GraphicsPipelineID pipeline = _drawTransparentPipeline;
    commandList.BeginPipeline(pipeline);

    for (auto& descriptorSet : params.descriptorSets)
    {
        commandList.BindDescriptorSet(*descriptorSet, frameIndex);
    }

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
    commandList.EndRenderPass(renderPassDesc);
}

void ModelRenderer::DrawSkybox(const RenderResources& resources, u8 frameIndex, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, const DrawParams& params, bool isTransparent)
{
    Renderer::RenderPassDesc renderPassDesc;
    graphResources.InitializeRenderPassDesc(renderPassDesc);

    // Render targets
    renderPassDesc.renderTargets[0] = params.rt0;
    if (isTransparent)
    {
        renderPassDesc.renderTargets[1] = params.rt1;
    }
    renderPassDesc.depthStencil = params.depth;
    commandList.BeginRenderPass(renderPassDesc);

    Renderer::GraphicsPipelineID pipeline = isTransparent ? _drawSkyboxTransparentPipeline : _drawSkyboxOpaquePipeline;
    commandList.BeginPipeline(pipeline);

    for (auto& descriptorSet : params.descriptorSets)
    {
        commandList.BindDescriptorSet(*descriptorSet, frameIndex);
    }

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
    commandList.EndRenderPass(renderPassDesc);
}