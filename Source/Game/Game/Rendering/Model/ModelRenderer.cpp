#include "ModelRenderer.h"
#include "ModelRenderer.h"
#include "Game/Rendering/CullUtils.h"
#include "Game/Rendering/RenderUtils.h"
#include "Game/Rendering/GameRenderer.h"
#include "Game/Rendering/RenderResources.h"
#include "Game/Rendering/Debug/DebugRenderer.h"
#include "Game/Util/ServiceLocator.h"
#include "Game/Application/EnttRegistries.h"
#include "Game/ECS/Singletons/TextureSingleton.h"

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

ModelRenderer::ModelRenderer(Renderer::Renderer* renderer, DebugRenderer* debugRenderer)
    : CulledRenderer(renderer, debugRenderer)
    , _renderer(renderer)
    , _debugRenderer(debugRenderer)
{
    CreatePermanentResources();
}

ModelRenderer::~ModelRenderer()
{

}

void ModelRenderer::Update(f32 deltaTime)
{
    ZoneScoped;

    if (!CVAR_ModelRendererEnabled.Get())
        return;

    const bool cullingEnabled = CVAR_ModelCullingEnabled.Get();
    _opaqueCullingResources.Update(deltaTime, cullingEnabled);
    _transparentCullingResources.Update(deltaTime, cullingEnabled);
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

    _opaqueCullingResources.Clear();
    _transparentCullingResources.Clear();

    _renderer->UnloadTexturesInArray(_textures, 1);

    SyncToGPU();
}

void ModelRenderer::AddOccluderPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    ZoneScoped;

    SyncToGPU();

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

        Renderer::DescriptorSetResource globalSet;
        Renderer::DescriptorSetResource occluderFillSet;
        Renderer::DescriptorSetResource drawSet;
    };

    renderGraph->AddPass<Data>("Model (O) Occluders",
        [=, &resources](Data& data, Renderer::RenderGraphBuilder& builder)
        {
            data.visibilityBuffer = builder.Write(resources.visibilityBuffer, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);
            data.depth = builder.Write(resources.depth, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);

            data.globalSet = builder.Use(resources.globalDescriptorSet);
            data.occluderFillSet = builder.Use(_opaqueCullingResources.GetOccluderFillDescriptorSet());
            data.drawSet = builder.Use(_opaqueCullingResources.GetGeometryPassDescriptorSet());

            return true; // Return true from setup to enable this pass, return false to disable it
        },
        [=](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
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

        Renderer::DescriptorSetResource debugSet;
        Renderer::DescriptorSetResource globalSet;
        Renderer::DescriptorSetResource cullingSet;
    };

    renderGraph->AddPass<Data>("Model (O) Culling",
        [=, &resources](Data& data, Renderer::RenderGraphBuilder& builder) // Setup
        {
            data.depthPyramid = builder.Read(resources.depthPyramid, Renderer::PipelineType::COMPUTE);

            data.debugSet = builder.Use(_debugRenderer->GetDebugDescriptorSet());
            data.globalSet = builder.Use(resources.globalDescriptorSet);
            data.cullingSet = builder.Use(_opaqueCullingResources.GetCullingDescriptorSet());

            return true; // Return true from setup to enable this pass, return false to disable it
        },
        [=](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, ModelCulling);

            CulledRenderer::CullingPassParams params;
            params.passName = "Opaque";
            params.graphResources = &graphResources;
            params.commandList = &commandList;
            params.cullingResources = &_opaqueCullingResources;
            params.frameIndex = frameIndex;

            params.depthPyramid = data.depthPyramid;

            params.debugDescriptorSet = data.debugSet;
            params.globalDescriptorSet = data.globalSet;
            params.cullingDescriptorSet = data.cullingSet;

            params.numCascades = 0;// *CVarSystem::Get()->GetIntCVar("shadows.cascade.num");
            params.occlusionCull = true;
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

        Renderer::DescriptorSetResource globalSet;
        Renderer::DescriptorSetResource drawSet;
    };

    renderGraph->AddPass<Data>("Model (O) Geometry",
        [=, &resources](Data& data, Renderer::RenderGraphBuilder& builder)
        {
            data.visibilityBuffer = builder.Write(resources.visibilityBuffer, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);
            data.depth = builder.Write(resources.depth, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);

            data.globalSet = builder.Use(resources.globalDescriptorSet);
            data.drawSet = builder.Use(_opaqueCullingResources.GetGeometryPassDescriptorSet());

            return true; // Return true from setup to enable this pass, return false to disable it
        },
        [=](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList)
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

        Renderer::DescriptorSetResource debugSet;
        Renderer::DescriptorSetResource globalSet;
        Renderer::DescriptorSetResource cullingSet;
    };

    renderGraph->AddPass<Data>("Model (T) Culling",
        [=, &resources](Data& data, Renderer::RenderGraphBuilder& builder) // Setup
        {
            data.depthPyramid = builder.Read(resources.depthPyramid, Renderer::PipelineType::COMPUTE);

            data.debugSet = builder.Use(_debugRenderer->GetDebugDescriptorSet());
            data.globalSet = builder.Use(resources.globalDescriptorSet);
            data.cullingSet = builder.Use(_transparentCullingResources.GetCullingDescriptorSet());

            return true; // Return true from setup to enable this pass, return false to disable it
        },
        [=](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, ModelCulling);

            CulledRenderer::CullingPassParams params;
            params.passName = "Transparent";
            params.graphResources = &graphResources;
            params.commandList = &commandList;
            params.cullingResources = &_transparentCullingResources;
            params.frameIndex = frameIndex;

            params.depthPyramid = data.depthPyramid;

            params.debugDescriptorSet = data.debugSet;
            params.globalDescriptorSet = data.globalSet;
            params.cullingDescriptorSet = data.cullingSet;

            params.numCascades = 0;// *CVarSystem::Get()->GetIntCVar("shadows.cascade.num");
            params.occlusionCull = false;
            params.disableTwoStepCulling = true; // Transparent objects don't write depth, so we don't need to two step cull them
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

        Renderer::DescriptorSetResource globalSet;
        Renderer::DescriptorSetResource drawSet;
    };

    renderGraph->AddPass<Data>("Model (T) Geometry",
        [=, &resources](Data& data, Renderer::RenderGraphBuilder& builder)
        {
            data.transparency = builder.Write(resources.transparency, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::CLEAR);
            data.transparencyWeights = builder.Write(resources.transparencyWeights, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::CLEAR);
            data.depth = builder.Write(resources.depth, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);

            data.globalSet = builder.Use(resources.globalDescriptorSet);
            data.drawSet = builder.Use(_transparentCullingResources.GetGeometryPassDescriptorSet());

            return true; // Return true from setup to enable this pass, return false to disable it
        },
        [=](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList)
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

    _textureUnits.Grow(reserveInfo.numTextureUnits);

    _opaqueCullingResources.Grow(reserveInfo.numOpaqueDrawcalls);
    _transparentCullingResources.Grow(reserveInfo.numTransparentDrawcalls);
}

u32 ModelRenderer::LoadModel(const std::string& name, Model::ComplexModel& model)
{
    EnttRegistries* registries = ServiceLocator::GetEnttRegistries();

    entt::registry* registry = registries->gameRegistry;

    entt::registry::context& ctx = registry->ctx();
    ECS::Singletons::TextureSingleton& textureSingleton = ctx.at<ECS::Singletons::TextureSingleton>();

    // Add ModelManifest
    u32 modelManifestIndex = _modelManifestsIndex.fetch_add(1);
    ModelManifest& modelManifest = _modelManifests[modelManifestIndex];

    modelManifest.debugName = name;

    // Add CullingData
    std::vector<Model::ComplexModel::CullingData>& cullingDatas = _cullingDatas.Get();
    Model::ComplexModel::CullingData& cullingData = cullingDatas[modelManifestIndex];
    cullingData = model.cullingData;

    // Add vertices
    {
        modelManifest.numVertices = model.modelHeader.numVertices;
        modelManifest.vertexOffset = _verticesIndex.fetch_add(modelManifest.numVertices);

        std::vector<Model::ComplexModel::Vertex>& vertices = _vertices.Get();

        void* dst = &vertices[modelManifest.vertexOffset];
        void* src = model.vertices.data();
        size_t size = sizeof(Model::ComplexModel::Vertex) * model.vertices.size();

        if (modelManifest.vertexOffset + model.vertices.size() > vertices.size())
        {
            DebugHandler::PrintFatal("ModelRenderer : Tried to memcpy vertices outside array");
        }

        memcpy(dst, src, size);
    }

    // Add indices
    {
        modelManifest.numIndices = model.modelHeader.numIndices;
        modelManifest.indexOffset = _indicesIndex.fetch_add(modelManifest.numIndices);

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

    // Add TextureUnits and DrawCalls
    {
        modelManifest.numOpaqueDrawCalls = model.modelHeader.numOpaqueRenderBatches;
        modelManifest.opaqueDrawCallOffset = _opaqueCullingResources.GetDrawCallsIndex().fetch_add(modelManifest.numOpaqueDrawCalls);

        modelManifest.numTransparentDrawCalls = model.modelHeader.numTransparentRenderBatches;
        modelManifest.transparentDrawCallOffset = _transparentCullingResources.GetDrawCallsIndex().fetch_add(modelManifest.numTransparentDrawCalls);

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
                    u16 textureIndex = model.textureIndexLookupTable[cTextureUnit.textureIndexStart + j];

                    Model::ComplexModel::Texture& cTexture = model.textures[textureIndex];
                    if (cTexture.type == Model::ComplexModel::Texture::Type::None)
                    {
                        std::scoped_lock lock(_textureLoadMutex);

                        Renderer::TextureDesc textureDesc;
                        textureDesc.path = textureSingleton.textureHashToPath[cTexture.textureHash];

                        if (textureDesc.path.size() > 0)
                        {
                            Renderer::TextureID textureID = _renderer->LoadTextureIntoArray(textureDesc, _textures, textureUnit.textureIds[j]);
                            textureSingleton.textureHashToTextureID[cTexture.textureHash] = static_cast<Renderer::TextureID::type>(textureID);

                            DebugHandler::Assert(textureUnit.textureIds[j] < 4096, "ModelRenderer : LoadModel overflowed the 4096 textures we have support for");
                        }
                    }
                }
            }

            // Draw Calls
            u32& numAddedDrawCalls = (renderBatch.isTransparent) ? numAddedTransparentDrawCalls : numAddedOpaqueDrawCalls;
            u32& drawCallOffset = (renderBatch.isTransparent) ? modelManifest.transparentDrawCallOffset : modelManifest.opaqueDrawCallOffset;

            CullingResources<DrawCallData>& cullingResources = (renderBatch.isTransparent) ? _transparentCullingResources : _opaqueCullingResources;
            std::vector<Renderer::IndexedIndirectDraw>& drawCalls = cullingResources.GetDrawCalls().Get();
            std::vector<DrawCallData>& drawCallDatas = cullingResources.GetDrawCallDatas().Get();

            u32 curDrawCallOffset = drawCallOffset + numAddedDrawCalls;

            Renderer::IndexedIndirectDraw& drawCall = drawCalls[curDrawCallOffset];
            drawCall.indexCount = renderBatch.indexCount;
            drawCall.instanceCount = 1;
            drawCall.firstIndex = modelManifest.indexOffset + renderBatch.indexStart;
            drawCall.vertexOffset = modelManifest.vertexOffset + renderBatch.vertexStart;
            drawCall.firstInstance = 0; // Is set during AddInstance

            DrawCallData& drawCallData = drawCallDatas[curDrawCallOffset];
            drawCallData.instanceID = 0; // Is set during AddInstance
            drawCallData.textureUnitOffset = textureUnitBaseIndex;
            drawCallData.numTextureUnits = static_cast<u16>(renderBatch.textureUnits.size());
            drawCallData.numUnlitTextureUnits = numUnlitTextureUnits;

            numAddedDrawCalls++;
        }
    }

    return modelManifestIndex;
}

u32 ModelRenderer::AddInstance(u32 modelID, const Terrain::Placement& placement)
{
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
    }

    // Add Instance matrix
    {
        mat4x4& instanceMatrix = _instanceMatrices.Get()[instanceID];

        vec3 pos = vec3(placement.position.x, placement.position.y, placement.position.z);

        vec3 scale = vec3(placement.scale) / 1024.0f;

        mat4x4 rotationMatrix = glm::toMat4(placement.rotation);
        mat4x4 scaleMatrix = glm::scale(mat4x4(1.0f), scale);
        instanceMatrix = glm::translate(mat4x4(1.0f), pos) * rotationMatrix * scaleMatrix;
    }

    // Set up Opaque DrawCalls and DrawCallDatas
    if (manifest.numOpaqueDrawCalls > 0)
    {
        u32 opaqueBaseIndex = manifest.opaqueDrawCallOffset;

        std::vector<Renderer::IndexedIndirectDraw>& opaqueDrawCalls = _opaqueCullingResources.GetDrawCalls().Get();
        std::vector<DrawCallData>& opaqueDrawCallDatas = _opaqueCullingResources.GetDrawCallDatas().Get();

        // The first instance doesn't need to copy the drawcalls
        if (modelInstanceIndex != 0)
        {
            opaqueBaseIndex = _opaqueCullingResources.GetDrawCallsIndex().fetch_add(manifest.numOpaqueDrawCalls);

            // Copy DrawCalls
            {
                Renderer::IndexedIndirectDraw* dst = &opaqueDrawCalls[opaqueBaseIndex];
                Renderer::IndexedIndirectDraw* src = &opaqueDrawCalls[manifest.opaqueDrawCallOffset];
                size_t size = manifest.numOpaqueDrawCalls * sizeof(Renderer::IndexedIndirectDraw);
                memcpy(dst, src, size);
            }

            // Copy DrawCallDatas
            {
                DrawCallData* dst = &opaqueDrawCallDatas[opaqueBaseIndex];
                DrawCallData* src = &opaqueDrawCallDatas[manifest.opaqueDrawCallOffset];
                size_t size = manifest.numOpaqueDrawCalls * sizeof(DrawCallData);
                memcpy(dst, src, size);
            }
        }

        // Modify the per-instance data
        for (u32 i = 0; i < manifest.numOpaqueDrawCalls; i++)
        {
            u32 opaqueIndex = opaqueBaseIndex + i;

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
        u32 transparentBaseIndex = manifest.transparentDrawCallOffset;

        std::vector<Renderer::IndexedIndirectDraw>& transparentDrawCalls = _transparentCullingResources.GetDrawCalls().Get();
        std::vector<DrawCallData>& transparentDrawCallDatas = _transparentCullingResources.GetDrawCallDatas().Get();

        // The first instance doesn't need to copy the drawcalls
        if (modelInstanceIndex != 0)
        {
            transparentBaseIndex = _transparentCullingResources.GetDrawCallsIndex().fetch_add(manifest.numTransparentDrawCalls);

            // Copy DrawCalls
            {
                Renderer::IndexedIndirectDraw* dst = &transparentDrawCalls[transparentBaseIndex];
                Renderer::IndexedIndirectDraw* src = &transparentDrawCalls[manifest.transparentDrawCallOffset];
                size_t size = manifest.numTransparentDrawCalls * sizeof(Renderer::IndexedIndirectDraw);
                memcpy(dst, src, size);
            }

            // Copy DrawCallDatas
            {
                DrawCallData* dst = &transparentDrawCallDatas[transparentBaseIndex];
                DrawCallData* src = &transparentDrawCallDatas[manifest.transparentDrawCallOffset];
                size_t size = manifest.numTransparentDrawCalls * sizeof(DrawCallData);
                memcpy(dst, src, size);
            }
        }

        // Modify the per-instance data
        for (u32 i = 0; i < manifest.numTransparentDrawCalls; i++)
        {
            u32 transparentIndex = transparentBaseIndex + i;

            Renderer::IndexedIndirectDraw& drawCall = transparentDrawCalls[transparentIndex];
            drawCall.firstInstance = transparentIndex;

            DrawCallData& drawCallData = transparentDrawCallDatas[transparentIndex];
            drawCallData.instanceID = instanceID;
            drawCallData.modelID = modelID;
        }
    }

    return instanceID;
}

void ModelRenderer::CreatePermanentResources()
{
    ZoneScoped;
    Renderer::TextureArrayDesc textureArrayDesc;
    textureArrayDesc.size = 4096;

    _textures = _renderer->CreateTextureArray(textureArrayDesc);
    _opaqueCullingResources.GetGeometryPassDescriptorSet().Bind("_modelTextures"_h, _textures);
    _transparentCullingResources.GetGeometryPassDescriptorSet().Bind("_modelTextures"_h, _textures);
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

    CullingResources<DrawCallData>::InitParams initParams;
    initParams.renderer = _renderer;
    initParams.bufferNamePrefix = "OpaqueModels";
    initParams.materialPassDescriptorSet = &_materialPassDescriptorSet;
    initParams.enableTwoStepCulling = true;
    _opaqueCullingResources.Init(initParams);

    initParams.bufferNamePrefix = "TransparentModels";
    initParams.materialPassDescriptorSet = nullptr;
    initParams.enableTwoStepCulling = false;
    _transparentCullingResources.Init(initParams);
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
            _materialPassDescriptorSet.Bind("_packedModelVertices"_h, _vertices.GetBuffer());
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
            _materialPassDescriptorSet.Bind("_modelInstanceMatrices"_h, _instanceMatrices.GetBuffer());
        }
    }

    _opaqueCullingResources.SyncToGPU();
    _transparentCullingResources.SyncToGPU();

    SetupCullingResource(_opaqueCullingResources);
    SetupCullingResource(_transparentCullingResources);
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

    pipelineDesc.states.vertexShader = _renderer->LoadShader(vertexShaderDesc);

    Renderer::PixelShaderDesc pixelShaderDesc;
    pixelShaderDesc.path = "Model/Draw.ps.hlsl";
    pixelShaderDesc.AddPermutationField("SHADOW_PASS", params.shadowPass ? "1" : "0");
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

    pipelineDesc.states.vertexShader = _renderer->LoadShader(vertexShaderDesc);

    Renderer::PixelShaderDesc pixelShaderDesc;
    pixelShaderDesc.path = "Model/DrawTransparent.ps.hlsl";
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
