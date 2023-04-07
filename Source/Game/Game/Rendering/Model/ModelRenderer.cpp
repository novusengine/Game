#include "ModelRenderer.h"
#include "Game/Rendering/RenderUtils.h"
#include "Game/Rendering/GameRenderer.h"
#include "Game/Rendering/RenderResources.h"
#include "Game/Rendering/Debug/DebugRenderer.h"
#include "Game/Util/ServiceLocator.h"
#include "Game/Application/EnttRegistries.h"
#include "Game/ECS/Singletons/TextureSingleton.h"

#include <Base/Util/Timer.h>
#include <Base/CVarSystem/CVarSystem.h>

#include <FileFormat/Novus/Map/MapChunk.h>

#include <Input/InputManager.h>

#include <Renderer/Renderer.h>
#include <Renderer/RenderGraph.h>
#include <Renderer/Descriptors/ImageDesc.h>

#include <imgui/imgui.h>
#include <entt/entt.hpp>
#include <glm/gtx/euler_angles.hpp>

AutoCVar_Int CVAR_ModelRendererEnabled("modelRenderer.enabled", "enable modelrendering", 0, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_ModelCullingEnabled("modelRenderer.culling", "enable model culling", 1, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_ModelOcclusionCullingEnabled("modelRenderer.occlusionculling", "enable model occlusion culling", 1, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_ModelForceDisableOccluders("modelRenderer.forcedisableoccluders", "force disable occluders", 0, CVarFlags::EditCheckbox);

AutoCVar_Int CVAR_ModelOccludersEnabled("modelRenderer.draw.occluders", "should draw occluders", 1, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_ModelGeometryEnabled("modelRenderer.draw.geometry", "should draw geometry", 1, CVarFlags::EditCheckbox);

ModelRenderer::ModelRenderer(Renderer::Renderer* renderer, DebugRenderer* debugRenderer)
    : _renderer(renderer)
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

    //if (!CVAR_TerrainRendererEnabled.Get())
    //    return;

    /*const bool cullingEnabled = CVAR_ModelCullingEnabled.Get();

    // Read back from culling counters
    u32 numDrawCalls = 0;// Terrain::CHUNK_NUM_CELLS* _numChunksLoaded;

    for (u32 i = 0; i < Renderer::Settings::MAX_VIEWS; i++)
    {
        _numSurvivingDrawCalls[i] = numDrawCalls;
    }

    if (cullingEnabled)
    {
        u32* count = static_cast<u32*>(_renderer->MapBuffer(_occluderDrawCountReadBackBuffer));
        if (count != nullptr)
        {
            _numOccluderDrawCalls = *count;
        }
        _renderer->UnmapBuffer(_occluderDrawCountReadBackBuffer);
    }

    {
        u32* count = static_cast<u32*>(_renderer->MapBuffer(_drawCountReadBackBuffer));
        if (count != nullptr)
        {
            for (u32 i = 0; i < Renderer::Settings::MAX_VIEWS; i++)
            {
                _numSurvivingDrawCalls[i] = count[i];
            }
        }
        _renderer->UnmapBuffer(_drawCountReadBackBuffer);
    }*/

    SyncToGPU();
}

void ModelRenderer::Clear()
{
    ZoneScoped;

    _modelManifests.clear();
    _modelManifestsIndex.store(0);

    _modelIDToNumInstances.clear();

    _vertices.Clear();
    _verticesIndex.store(0);

    _indices.Clear();
    _indicesIndex.store(0);

    _instanceDatas.Clear();
    _instanceMatrices.Clear();
    _instanceIndex.store(0);

    _textureUnits.Clear();
    _textureUnitIndex.store(0);

    _opaqueDrawCalls.Clear();
    _opaqueDrawCallDatas.Clear();
    _opaqueDrawCallsIndex.store(0);

    _transparentDrawCalls.Clear();
    _transparentDrawCallDatas.Clear();
    _transparentDrawCallsIndex.store(0);

    _renderer->UnloadTexturesInArray(_textures, 1);

    SyncToGPU();
}

void ModelRenderer::AddOccluderPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    if (!CVAR_ModelRendererEnabled.Get())
        return;

    const bool cullingEnabled = CVAR_ModelCullingEnabled.Get();
    if (!cullingEnabled)
        return;

    const bool forceDisableOccluders = CVAR_ModelForceDisableOccluders.Get();

    struct Data
    {
        Renderer::RenderPassMutableResource visibilityBuffer;
        Renderer::RenderPassMutableResource depth;
    };

    renderGraph->AddPass<Data>("Model Occluders",
        [=, &resources](Data& data, Renderer::RenderGraphBuilder& builder) // Setup
        {
            data.visibilityBuffer = builder.Write(resources.visibilityBuffer, Renderer::RenderGraphBuilder::WriteMode::RENDERTARGET, Renderer::RenderGraphBuilder::LoadMode::LOAD);
    data.depth = builder.Write(resources.depth, Renderer::RenderGraphBuilder::WriteMode::RENDERTARGET, Renderer::RenderGraphBuilder::LoadMode::LOAD);

    return true; // Return true from setup to enable this pass, return false to disable it
        },
        [=, &resources](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, ModelOccluders);

        });
}

void ModelRenderer::AddCullingPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    return;
    ZoneScoped;

    if (!CVAR_ModelRendererEnabled.Get())
        return;

    if (!CVAR_ModelCullingEnabled.Get())
        return;

    u32 numCascades = 0;// *CVarSystem::Get()->GetIntCVar("shadows.cascade.num");

    struct Data
    {
    };

    renderGraph->AddPass<Data>("Model Culling",
        [=, &resources](Data& data, Renderer::RenderGraphBuilder& builder) // Setup
        {
            return true; // Return true from setup to enable this pass, return false to disable it
        },
        [=, &resources](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, ModelCulling);

        });
}

void ModelRenderer::AddGeometryPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    ZoneScoped;

    if (!CVAR_ModelRendererEnabled.Get())
        return;

    const bool cullingEnabled = CVAR_ModelCullingEnabled.Get();

    struct Data
    {
        Renderer::RenderPassMutableResource visibilityBuffer;
        Renderer::RenderPassMutableResource depth;
    };

    renderGraph->AddPass<Data>("ModelGeometry",
        [=, &resources](Data& data, Renderer::RenderGraphBuilder& builder)
        {
            data.visibilityBuffer = builder.Write(resources.visibilityBuffer, Renderer::RenderGraphBuilder::WriteMode::RENDERTARGET, Renderer::RenderGraphBuilder::LoadMode::LOAD);
    data.depth = builder.Write(resources.depth, Renderer::RenderGraphBuilder::WriteMode::RENDERTARGET, Renderer::RenderGraphBuilder::LoadMode::LOAD);

    return true; // Return true from setup to enable this pass, return false to disable it
        },
        [=, &resources](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList)
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, TerrainGeometryPass);

        const u32 numOpaqueDrawCalls = static_cast<u32>(_opaqueDrawCalls.Size());
        /*if (cullingEnabled)
        {
            for (u32 i = 0; i < Renderer::Settings::MAX_VIEWS; i++)
            {
                commandList.PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToIndirectArguments, _opaqueCulledDrawCallBuffer[i]);
            }
            commandList.PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToIndirectArguments, _opaqueDrawCountBuffer);
        }
        else
        {
            // Reset the counters
            commandList.FillBuffer(_opaqueDrawCountBuffer, 0, sizeof(u32) * Renderer::Settings::MAX_VIEWS, numOpaqueDrawCalls);
            commandList.PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToIndirectArguments, _opaqueDrawCountBuffer);
            commandList.PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToTransferDest, _opaqueDrawCountBuffer);
        }*/

        commandList.PushMarker("Opaque " + std::to_string(numOpaqueDrawCalls), Color::White);

        //u32 debugDrawCallBufferIndex = CVAR_ComplexModelDebugShadowDraws.Get();

        DrawParams drawParams;
        drawParams.shadowPass = false;
        drawParams.visibilityBuffer = data.visibilityBuffer;
        drawParams.depth = data.depth;
        drawParams.argumentBuffer = /*(cullingEnabled) ? _opaqueCulledDrawCallBuffer[debugDrawCallBufferIndex] :*/ _opaqueDrawCalls.GetBuffer();
        //drawParams.drawCountBuffer = _opaqueDrawCountBuffer;
        //drawParams.drawCountIndex = debugDrawCallBufferIndex;
        drawParams.numMaxDrawCalls = numOpaqueDrawCalls;

        Draw(resources, frameIndex, graphResources, commandList, drawParams);

        commandList.PopMarker();
        });
}

u32 ModelRenderer::GetInstanceIDFromDrawCallID(u32 drawCallID, bool isOpaque)
{
    Renderer::GPUVector<DrawCallData>& drawCallDatas = (isOpaque) ? _opaqueDrawCallDatas : _opaqueDrawCallDatas; // TODO: Transparency

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

    _vertices.Grow(reserveInfo.numVertices);
    _indices.Grow(reserveInfo.numIndices);

    _instanceDatas.Grow(reserveInfo.numInstances);
    _instanceMatrices.Grow(reserveInfo.numInstances);

    _textureUnits.Grow(reserveInfo.numTextureUnits);

    _opaqueDrawCalls.Grow(reserveInfo.numOpaqueDrawcalls);
    _opaqueDrawCallDatas.Grow(reserveInfo.numOpaqueDrawcalls);

    _transparentDrawCalls.Grow(reserveInfo.numTransparentDrawcalls);
    _transparentDrawCallDatas.Grow(reserveInfo.numTransparentDrawcalls);
}

u32 ModelRenderer::LoadModel(const std::string& name, Model::ComplexModel& model)
{
    if (!CVAR_ModelRendererEnabled.Get())
        return 0;


    EnttRegistries* registries = ServiceLocator::GetEnttRegistries();

    entt::registry* registry = registries->gameRegistry;

    entt::registry::context& ctx = registry->ctx();
    ECS::Singletons::TextureSingleton& textureSingleton = ctx.at<ECS::Singletons::TextureSingleton>();

    // Add ModelManifest
    u32 modelManifestIndex = _modelManifestsIndex.fetch_add(1);
    ModelManifest& modelManifest = _modelManifests[modelManifestIndex];

    // Add vertices
    {
        modelManifest.numVertices = model.modelHeader.numVertices;
        modelManifest.vertexOffset = _verticesIndex.fetch_add(modelManifest.numVertices);

        std::vector<Model::ComplexModel::Vertex>& vertices = _vertices.Get();
        vertices.insert(vertices.begin() + modelManifest.vertexOffset, model.vertices.begin(), model.vertices.end());
    }

    // Add indices
    {
        modelManifest.numIndices = model.modelHeader.numIndices;
        modelManifest.indexOffset = _indicesIndex.fetch_add(modelManifest.numIndices);

        std::vector<u16>& indices = _indices.Get();
        indices.insert(indices.begin() + modelManifest.indexOffset, model.modelData.indices.begin(), model.modelData.indices.end());
    }

    // Add TextureUnits and DrawCalls
    {
        modelManifest.numOpaqueDrawCalls = model.modelHeader.numOpaqueRenderBatches;
        modelManifest.opaqueDrawCallOffset = _opaqueDrawCallsIndex.fetch_add(modelManifest.numOpaqueDrawCalls);

        modelManifest.numTransparentDrawCalls = model.modelHeader.numTransparentRenderBatches;
        modelManifest.transparentDrawCallOffset = _transparentDrawCallsIndex.fetch_add(modelManifest.numTransparentDrawCalls);

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
                        Renderer::TextureDesc textureDesc;
                        textureDesc.path = textureSingleton.textureHashToPath[cTexture.textureHash];

                        if (textureDesc.path.size() > 0)
                        {
                            Renderer::TextureID textureID = _renderer->LoadTextureIntoArray(textureDesc, _textures, textureUnit.textureIds[j]);
                            textureSingleton.textureHashToTextureID[cTexture.textureHash] = static_cast<Renderer::TextureID::type>(textureID);
                        }
                    }
                }
            }

            // Draw Calls
            // TODO: Clarifying comment, split into a branch
            u32& numAddedDrawCalls = (renderBatch.isTransparent) ? numAddedTransparentDrawCalls : numAddedOpaqueDrawCalls;
            u32& drawCallOffset = (renderBatch.isTransparent) ? modelManifest.transparentDrawCallOffset : modelManifest.opaqueDrawCallOffset;

            std::vector<Renderer::IndexedIndirectDraw>& drawCalls = (renderBatch.isTransparent) ? _transparentDrawCalls.Get() : _opaqueDrawCalls.Get();
            std::vector<DrawCallData>& drawCallDatas = (renderBatch.isTransparent) ? _transparentDrawCallDatas.Get() : _opaqueDrawCallDatas.Get();

            u32 curDrawCallOffset = drawCallOffset + numAddedDrawCalls;

            Renderer::IndexedIndirectDraw& drawCall = drawCalls[curDrawCallOffset];
            drawCall.indexCount = renderBatch.indexCount;
            drawCall.instanceCount = 1;
            drawCall.firstIndex = modelManifest.indexOffset + renderBatch.indexStart;
            drawCall.vertexOffset = modelManifest.vertexOffset;
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
    if (!CVAR_ModelRendererEnabled.Get())
        return 0;

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

        std::vector<Renderer::IndexedIndirectDraw>& opaqueDrawCalls = _opaqueDrawCalls.Get();
        std::vector<DrawCallData>& opaqueDrawCallDatas = _opaqueDrawCallDatas.Get();

        // The first instance doesn't need to copy the drawcalls
        if (modelInstanceIndex != 0)
        {
            opaqueBaseIndex = _opaqueDrawCallsIndex.fetch_add(manifest.numOpaqueDrawCalls);

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
        }
    }

    // Set up Transparent DrawCalls and DrawCallDatas
    if (manifest.numTransparentDrawCalls > 0)
    {
        u32 transparentBaseIndex = manifest.transparentDrawCallOffset;

        std::vector<Renderer::IndexedIndirectDraw>& transparentDrawCalls = _transparentDrawCalls.Get();
        std::vector<DrawCallData>& transparentDrawCallDatas = _transparentDrawCallDatas.Get();

        // The first instance doesn't need to copy the drawcalls
        if (modelInstanceIndex != 0)
        {
            transparentBaseIndex = _transparentDrawCallsIndex.fetch_add(manifest.numTransparentDrawCalls);

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
    _geometryPassDescriptorSet.Bind("_modelTextures"_h, _textures);
    _materialPassDescriptorSet.Bind("_modelTextures"_h, _textures);
    //_transparencyPassDescriptorSet.Bind("_modelTextures"_h, _textures);

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
    _geometryPassDescriptorSet.Bind("_sampler"_h, _sampler);
    //_transparencyPassDescriptorSet.Bind("_sampler"_h, _sampler);

    Renderer::SamplerDesc occlusionSamplerDesc;
    occlusionSamplerDesc.filter = Renderer::SamplerFilter::MINIMUM_MIN_MAG_MIP_LINEAR;

    occlusionSamplerDesc.addressU = Renderer::TextureAddressMode::CLAMP;
    occlusionSamplerDesc.addressV = Renderer::TextureAddressMode::CLAMP;
    occlusionSamplerDesc.addressW = Renderer::TextureAddressMode::CLAMP;
    occlusionSamplerDesc.minLOD = 0.f;
    occlusionSamplerDesc.maxLOD = 16.f;
    occlusionSamplerDesc.mode = Renderer::SamplerReductionMode::MIN;

    _occlusionSampler = _renderer->CreateSampler(occlusionSamplerDesc);
    _opaqueCullingDescriptorSet.Bind("_depthSampler"_h, _occlusionSampler);
    //_transparentCullingDescriptorSet.Bind("_depthSampler"_h, _occlusionSampler);
}

void ModelRenderer::SyncToGPU()
{
    // Sync Vertex buffer to GPU
    {
        _vertices.SetDebugName("ModelVertexBuffer");
        _vertices.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
        _vertices.SyncToGPU(_renderer);

        _geometryPassDescriptorSet.Bind("_packedModelVertices"_h, _vertices.GetBuffer());
        _materialPassDescriptorSet.Bind("_packedModelVertices"_h, _vertices.GetBuffer());
        //_transparencyPassDescriptorSet.Bind("_packedModelVertices"_h, _vertices.GetBuffer());
    }

    // Sync Index buffer to GPU
    {
        _indices.SetDebugName("ModelIndexBuffer");
        _indices.SetUsage(Renderer::BufferUsage::INDEX_BUFFER | Renderer::BufferUsage::STORAGE_BUFFER);
        _indices.SyncToGPU(_renderer);

        _geometryPassDescriptorSet.Bind("_modelIndices"_h, _indices.GetBuffer());
        _materialPassDescriptorSet.Bind("_modelIndices"_h, _indices.GetBuffer());
        //_transparencyPassDescriptorSet.Bind("_modelIndices"_h, _indices.GetBuffer());
    }

    // Sync TextureUnit buffer to GPU
    {
        _textureUnits.SetDebugName("ModelTextureUnitBuffer");
        _textureUnits.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
        _textureUnits.SyncToGPU(_renderer);

        _geometryPassDescriptorSet.Bind("_modelTextureUnits"_h, _textureUnits.GetBuffer());
        _materialPassDescriptorSet.Bind("_modelTextureUnits"_h, _textureUnits.GetBuffer());
        //_transparencyPassDescriptorSet.Bind("_cModelTextureUnits"_h, _textureUnits.GetBuffer());
    }

    // Sync InstanceDatas buffer to GPU
    {
        _instanceDatas.SetDebugName("ModelInstanceDatas");
        _instanceDatas.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
        _instanceDatas.SyncToGPU(_renderer);

        _opaqueCullingDescriptorSet.Bind("_modelInstanceDatas"_h, _instanceDatas.GetBuffer());
        //_transparentCullingDescriptorSet.Bind("_modelInstanceDatas"_h, _instanceDatas.GetBuffer());
        //_animationPrepassDescriptorSet.Bind("_modelInstanceDatas"_h, _instanceDatas.GetBuffer());
        _geometryPassDescriptorSet.Bind("_modelInstanceDatas"_h, _instanceDatas.GetBuffer());
        _materialPassDescriptorSet.Bind("_modelInstanceDatas"_h, _instanceDatas.GetBuffer());
        //_transparencyPassDescriptorSet.Bind("_modelInstanceDatas"_h, _instanceDatas.GetBuffer());
    }

    // Sync InstanceMatrices buffer to GPU
    {
        _instanceMatrices.SetDebugName("ModelInstanceMatrices");
        _instanceMatrices.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
        _instanceMatrices.SyncToGPU(_renderer);

        _opaqueCullingDescriptorSet.Bind("_modelInstanceMatrices"_h, _instanceMatrices.GetBuffer());
        //_transparentCullingDescriptorSet.Bind("_modelInstanceMatrices"_h, _instanceMatrices.GetBuffer());
        //_animationPrepassDescriptorSet.Bind("_modelInstanceMatrices"_h, _instanceMatrices.GetBuffer());
        _geometryPassDescriptorSet.Bind("_modelInstanceMatrices"_h, _instanceMatrices.GetBuffer());
        _materialPassDescriptorSet.Bind("_modelInstanceMatrices"_h, _instanceMatrices.GetBuffer());
        //_transparencyPassDescriptorSet.Bind("_modelInstanceMatrices"_h, _instanceMatrices.GetBuffer());
    }

    // Sync Opaque DrawCalls to GPU
    {
        _opaqueDrawCalls.SetDebugName("ModelOpaqueDrawCallBuffer");
        _opaqueDrawCalls.SetUsage(Renderer::BufferUsage::INDIRECT_ARGUMENT_BUFFER | Renderer::BufferUsage::STORAGE_BUFFER);
        _opaqueDrawCalls.SyncToGPU(_renderer);

        //_occluderFillDescriptorSet.Bind("_drawCalls"_h, _opaqueDrawCalls.GetBuffer());
        _opaqueCullingDescriptorSet.Bind("_drawCalls"_h, _opaqueDrawCalls.GetBuffer());
        _geometryPassDescriptorSet.Bind("_modelDraws"_h, _opaqueDrawCalls.GetBuffer());
        _materialPassDescriptorSet.Bind("_modelDraws"_h, _opaqueDrawCalls.GetBuffer());

        // TODO: Culling
    }

    // Sync Opaque DrawCallDatas to GPU
    {
        _opaqueDrawCallDatas.SetDebugName("ModelOpaqueDrawCallDataBuffer");
        _opaqueDrawCallDatas.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
        _opaqueDrawCallDatas.SyncToGPU(_renderer);

        _opaqueCullingDescriptorSet.Bind("_packedModelDrawCallDatas"_h, _opaqueDrawCallDatas.GetBuffer());
        _geometryPassDescriptorSet.Bind("_packedModelDrawCallDatas"_h, _opaqueDrawCallDatas.GetBuffer());
        _materialPassDescriptorSet.Bind("_packedModelDrawCallDatas"_h, _opaqueDrawCallDatas.GetBuffer());
    }
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
        pipelineDesc.renderTargets[0] = params.visibilityBuffer;
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

    commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, &resources.globalDescriptorSet, frameIndex);
    //commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::SHADOWS, &resources.shadowDescriptorSet, frameIndex);
    commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::MODEL, &_geometryPassDescriptorSet, frameIndex);

    commandList.SetIndexBuffer(_indices.GetBuffer(), Renderer::IndexFormat::UInt16);

    // TODO: Culling
    //u32 drawCountBufferOffset = params.drawCountIndex * sizeof(u32);
    //commandList.DrawIndexedIndirectCount(params.argumentBuffer, 0, params.drawCountBuffer, drawCountBufferOffset, params.numMaxDrawCalls);
    commandList.DrawIndexedIndirect(params.argumentBuffer, 0, params.numMaxDrawCalls);

    commandList.EndPipeline(pipeline);
}
