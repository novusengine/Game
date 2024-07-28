#include "MaterialRenderer.h"

#include "Game/Application/EnttRegistries.h"
#include "Game/Editor/EditorHandler.h"
#include "Game/Editor/TerrainTools.h"
#include "Game/Rendering/Debug/DebugRenderer.h"
#include "Game/Rendering/Model/ModelRenderer.h"
#include "Game/Rendering/RenderResources.h"
#include "Game/Rendering/Terrain/TerrainRenderer.h"
#include "Game/Util/PhysicsUtil.h"
#include "Game/Util/ServiceLocator.h"

#include <Base/CVarSystem/CVarSystem.h>

#include <Renderer/Renderer.h>
#include <Renderer/RenderGraph.h>

#include <entt/entt.hpp>

AutoCVar_Int CVAR_VisibilityBufferDebugID(CVarCategory::Client | CVarCategory::Rendering, "visibilityBufferDebugID", "Debug visualizers: 0 - Off, 1 - TypeID, 2 - ObjectID, 3 - TriangleID, 4 - ShadowCascade", 0);
AutoCVar_ShowFlag CVAR_DrawTerrainWireframe(CVarCategory::Client | CVarCategory::Rendering, "drawTerrainWireframe", "Draw terrain wireframe", ShowFlag::DISABLED);
AutoCVar_ShowFlag CVAR_EnableFog(CVarCategory::Client | CVarCategory::Rendering, "enableFog", "Toggle fog", ShowFlag::DISABLED);
AutoCVar_VecFloat CVAR_FogColor(CVarCategory::Client | CVarCategory::Rendering, "fogColor", "Change fog color", vec4(0.33f, 0.2f, 0.38f, 1.0f), CVarFlags::None);
AutoCVar_Float CVAR_FogBeginDist(CVarCategory::Client | CVarCategory::Rendering, "fogBlendBegin", "Fog blending start distance", 200.0f, CVarFlags::EditFloatDrag);
AutoCVar_Float CVAR_FogEndDist(CVarCategory::Client | CVarCategory::Rendering, "fogBlendEnd", "Fog blending end distance", 600.0f, CVarFlags::EditFloatDrag);

MaterialRenderer::MaterialRenderer(Renderer::Renderer* renderer, TerrainRenderer* terrainRenderer, ModelRenderer* modelRenderer)
    : _renderer(renderer)
    , _terrainRenderer(terrainRenderer)
    , _modelRenderer(modelRenderer)
{
    CreatePermanentResources();
}

MaterialRenderer::~MaterialRenderer()
{

}

void MaterialRenderer::Update(f32 deltaTime)
{
    SyncToGPU();
}

void MaterialRenderer::AddPreEffectsPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    struct PreEffectsPassData
    {
        Renderer::ImageResource visibilityBuffer;
        Renderer::ImageMutableResource packedNormals;

        Renderer::DescriptorSetResource globalSet;
        Renderer::DescriptorSetResource preEffectsSet;
        Renderer::DescriptorSetResource terrainSet;
        Renderer::DescriptorSetResource modelSet;
    };

    renderGraph->AddPass<PreEffectsPassData>("Pre Effects",
        [this, &resources](PreEffectsPassData& data, Renderer::RenderGraphBuilder& builder) // Setup
        {
            data.visibilityBuffer = builder.Read(resources.visibilityBuffer, Renderer::PipelineType::COMPUTE);
            data.packedNormals = builder.Write(resources.packedNormals, Renderer::PipelineType::COMPUTE, Renderer::LoadMode::LOAD);

            builder.Read(resources.cameras.GetBuffer(), Renderer::BufferPassUsage::COMPUTE);

            Renderer::DescriptorSet& terrainDescriptorSet = _terrainRenderer->GetMaterialPassDescriptorSet();
            Renderer::DescriptorSet& modelDescriptorSet = _modelRenderer->GetMaterialPassDescriptorSet();

            data.globalSet = builder.Use(resources.globalDescriptorSet);
            data.preEffectsSet = builder.Use(_preEffectsPassDescriptorSet);
            data.terrainSet = builder.Use(terrainDescriptorSet);
            data.modelSet = builder.Use(modelDescriptorSet);

            _terrainRenderer->RegisterMaterialPassBufferUsage(builder);
            _modelRenderer->RegisterMaterialPassBufferUsage(builder);

            return true; // Return true from setup to enable this pass, return false to disable it
        },
        [this, &resources, frameIndex](PreEffectsPassData& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, PreEffectsPass);

            Renderer::ComputePipelineDesc pipelineDesc;
            graphResources.InitializePipelineDesc(pipelineDesc);

            Renderer::ComputeShaderDesc shaderDesc;
            shaderDesc.path = "PreEffectsPass.cs.hlsl";
            pipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);

            Renderer::ComputePipelineID pipeline = _renderer->CreatePipeline(pipelineDesc);
            commandList.BeginPipeline(pipeline);

            data.preEffectsSet.Bind("_visibilityBuffer", data.visibilityBuffer);
            data.preEffectsSet.BindStorage("_packedNormals", data.packedNormals, 0);

            // Bind descriptorset
            commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, data.globalSet, frameIndex);
            commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::TERRAIN, data.terrainSet, frameIndex);
            commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::MODEL, data.modelSet, frameIndex);
            commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS, data.preEffectsSet, frameIndex);

            uvec2 outputSize = _renderer->GetImageDimensions(resources.packedNormals, 0);

            uvec2 dispatchSize = uvec2((outputSize.x + 7) / 8, (outputSize.y + 7) / 8);
            commandList.Dispatch(dispatchSize.x, dispatchSize.y, 1);

            commandList.EndPipeline(pipeline);
        });
}

void MaterialRenderer::AddMaterialPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    struct MaterialPassData
    {
        Renderer::ImageResource visibilityBuffer;
        Renderer::ImageResource skyboxColor;
        Renderer::ImageResource transparency;
        Renderer::ImageResource transparencyWeights;
        Renderer::DepthImageResource depth;
        Renderer::ImageMutableResource resolvedColor;

        Renderer::ImageResource ambientOcclusion;

        Renderer::DescriptorSetResource globalSet;
        Renderer::DescriptorSetResource shadowSet;
        Renderer::DescriptorSetResource materialSet;
        Renderer::DescriptorSetResource terrainSet;
        Renderer::DescriptorSetResource modelSet;
    };

    const i32 visibilityBufferDebugID = Math::Clamp(CVAR_VisibilityBufferDebugID.Get(), 0, 4);

    renderGraph->AddPass<MaterialPassData>("Material Pass",
        [this, &resources](MaterialPassData& data, Renderer::RenderGraphBuilder& builder) // Setup
        {
            data.visibilityBuffer = builder.Read(resources.visibilityBuffer, Renderer::PipelineType::COMPUTE);
            data.skyboxColor = builder.Read(resources.skyboxColor, Renderer::PipelineType::COMPUTE);
            data.transparency = builder.Read(resources.transparency, Renderer::PipelineType::COMPUTE);
            data.transparencyWeights = builder.Read(resources.transparencyWeights, Renderer::PipelineType::COMPUTE);
            data.depth = builder.Read(resources.depth, Renderer::PipelineType::COMPUTE);
            data.resolvedColor = builder.Write(resources.sceneColor, Renderer::PipelineType::COMPUTE, Renderer::LoadMode::LOAD);

            data.ambientOcclusion = builder.Read(resources.ssaoTarget, Renderer::PipelineType::COMPUTE);

            builder.Read(resources.cameras.GetBuffer(), Renderer::BufferPassUsage::COMPUTE);
            builder.Read(_directionalLights.GetBuffer(), Renderer::BufferPassUsage::COMPUTE);

            Renderer::DescriptorSet& terrainDescriptorSet = _terrainRenderer->GetMaterialPassDescriptorSet();
            Renderer::DescriptorSet& modelDescriptorSet = _modelRenderer->GetMaterialPassDescriptorSet();

            data.globalSet = builder.Use(resources.globalDescriptorSet);
            data.shadowSet = builder.Use(resources.shadowDescriptorSet);
            data.materialSet = builder.Use(_materialPassDescriptorSet);
            data.terrainSet = builder.Use(terrainDescriptorSet);
            data.modelSet = builder.Use(modelDescriptorSet);

            _terrainRenderer->RegisterMaterialPassBufferUsage(builder);
            _modelRenderer->RegisterMaterialPassBufferUsage(builder);

            return true; // Return true from setup to enable this pass, return false to disable it
        },
        [this, &resources, frameIndex, visibilityBufferDebugID](MaterialPassData& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, MaterialPass);

            Renderer::ComputePipelineDesc pipelineDesc;
            graphResources.InitializePipelineDesc(pipelineDesc);

            const i32 shadowFilterMode = *CVarSystem::Get()->GetIntCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowFilterMode"_h);

            Renderer::ComputeShaderDesc shaderDesc;
            shaderDesc.path = "MaterialPass.cs.hlsl";
            shaderDesc.AddPermutationField("DEBUG_ID", std::to_string(visibilityBufferDebugID));
            shaderDesc.AddPermutationField("SHADOW_FILTER_MODE", std::to_string(shadowFilterMode));
            shaderDesc.AddPermutationField("SUPPORTS_EXTENDED_TEXTURES", _renderer->HasExtendedTextureSupport() ? "1" : "0");
            shaderDesc.AddPermutationField("EDITOR_MODE", CVAR_DrawTerrainWireframe.Get() == ShowFlag::ENABLED ? "1" : "0");
            pipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);

            Renderer::ComputePipelineID pipeline = _renderer->CreatePipeline(pipelineDesc);
            commandList.BeginPipeline(pipeline);

            // For some reason, this works when we're binding to modelSet despite the GPU side being bound as PER_PASS???
            data.materialSet.Bind("_visibilityBuffer", data.visibilityBuffer);
            data.materialSet.Bind("_skyboxColor", data.skyboxColor);
            data.materialSet.Bind("_transparency", data.transparency);
            data.materialSet.Bind("_transparencyWeights", data.transparencyWeights);
            data.materialSet.Bind("_depth"_h, data.depth);
            data.materialSet.BindStorage("_resolvedColor", data.resolvedColor, 0);

            data.materialSet.Bind("_ambientOcclusion", data.ambientOcclusion);

            // Bind descriptorset
            commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, data.globalSet, frameIndex);
            commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::SHADOWS, data.shadowSet, frameIndex);
            commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::TERRAIN, data.terrainSet, frameIndex);
            commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::MODEL, data.modelSet, frameIndex);
            commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS, data.materialSet, frameIndex);

            //if (CVAR_DrawTerrainWireframe.Get() == ShowFlag::ENABLED)
            {
                Editor::Viewport* viewport = ServiceLocator::GetEditorHandler()->GetViewport();

                vec3 mouseWorldPosition = vec3(0, 0, 0);
                Util::Physics::GetMouseWorldPosition(viewport, mouseWorldPosition);

                struct Constants
                {
                    uvec4 lightInfo; // x = Directional Light Count, Y = Point Light Count, Z = Cascade Count, W = Shadows Enabled
                    vec4 fogColor;
                    vec4 fogSettings; // x = Enabled, y = Begin Fog Blend Dist, z = End Fog Blend Dist, w = UNUSED
                    vec4 mouseWorldPos;
                    vec4 brushSettings; // x = hardness, y = radius, z = pressure, w = falloff
                    Color chunkEdgeColor;
                    Color cellEdgeColor;
                    Color patchEdgeColor;
                    Color vertexColor;
                    Color brushColor;
                };

                Constants* constants = graphResources.FrameNew<Constants>();

                CVarSystem* cvarSystem = CVarSystem::Get();
                const u32 numCascades = static_cast<u32>(*cvarSystem->GetIntCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowCascadeNum"));
                const u32 shadowEnabled = static_cast<u32>(*cvarSystem->GetIntCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowEnabled"));
                constants->lightInfo = uvec4(static_cast<u32>(_directionalLights.Size()), 0, numCascades, shadowEnabled);

                constants->fogColor = CVAR_FogColor.Get();
                constants->fogSettings.x = CVAR_EnableFog.Get() == ShowFlag::ENABLED;
                constants->fogSettings.y = CVAR_FogBeginDist.GetFloat();
                constants->fogSettings.z = CVAR_FogEndDist.GetFloat();

                constants->mouseWorldPos = vec4(mouseWorldPosition, 1.0f);
                Editor::TerrainTools* terrainTools = ServiceLocator::GetEditorHandler()->GetTerrainTools();
                constants->brushSettings.x = terrainTools->GetHardness();
                constants->brushSettings.y = terrainTools->GetRadius();
                constants->brushSettings.z = terrainTools->GetPressure();
                constants->brushSettings.w = 0.25f; // Falloff, hardcoded for now

                constants->chunkEdgeColor = terrainTools->GetChunkEdgeColor();
                constants->cellEdgeColor = terrainTools->GetCellEdgeColor();
                constants->patchEdgeColor = terrainTools->GetPatchEdgeColor();
                constants->vertexColor = terrainTools->GetVertexColor();
                constants->brushColor = terrainTools->GetBrushColor();

                commandList.PushConstant(constants, 0, sizeof(Constants));
            }

            uvec2 outputSize = _renderer->GetImageDimensions(resources.sceneColor, 0);

            uvec2 dispatchSize = uvec2((outputSize.x + 7) / 8, (outputSize.y + 7) / 8);
            commandList.Dispatch(dispatchSize.x, dispatchSize.y, 1);

            commandList.EndPipeline(pipeline);
        });
}

void MaterialRenderer::AddDirectionalLight(const vec3& direction, const vec3& color, f32 intensity, const vec3& groundAmbientColor, f32 groundAmbientIntensity, const vec3& skyAmbientColor, f32 skyAmbientIntensity)
{
    DirectionalLight light
    {
        .direction = vec4(direction, 0.0f),
        .color = vec4(color, intensity),
        .groundAmbientColor = vec4(groundAmbientColor, groundAmbientIntensity),
        .skyAmbientColor = vec4(skyAmbientColor, skyAmbientIntensity)
    };

    std::vector<DirectionalLight>& directionalLights = _directionalLights.Get();
    directionalLights.push_back(light);
}

void MaterialRenderer::CreatePermanentResources()
{
    Renderer::SamplerDesc samplerDesc;
    samplerDesc.enabled = true;
    samplerDesc.filter = Renderer::SamplerFilter::MIN_MAG_MIP_LINEAR;
    samplerDesc.addressU = Renderer::TextureAddressMode::WRAP;
    samplerDesc.addressV = Renderer::TextureAddressMode::WRAP;
    samplerDesc.addressW = Renderer::TextureAddressMode::CLAMP;
    samplerDesc.shaderVisibility = Renderer::ShaderVisibility::ALL;

    _sampler = _renderer->CreateSampler(samplerDesc);
    _materialPassDescriptorSet.Bind("_sampler"_h, _sampler);

    _directionalLights.SetDebugName("Directional Lights");
    _directionalLights.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

    // Debug directional light
    vec3 direction = glm::normalize(vec3(*CVarSystem::Get()->GetVecFloatCVar(CVarCategory::Client | CVarCategory::Rendering, "directionalLightDirection"_h)));
    vec3 color = vec3(66.f/255.f, 101.f/255.f, 134.f/255.f);
    f32 intensity = 1.0f;

    vec3 ambientColor = vec3(14.f/255.f, 30.f/255.f, 52.f/255.f);

    vec3 groundAmbientColor = ambientColor * 0.7f;
    f32 groundAmbientIntensity = 1.0f;
    vec3 skyAmbientColor = ambientColor * 1.1f;
    f32 skyAmbientIntensity = 1.0f;

    AddDirectionalLight(direction, color, intensity, groundAmbientColor, groundAmbientIntensity, skyAmbientColor, skyAmbientIntensity);
}

void MaterialRenderer::SyncToGPU()
{
    if (_directionalLights.SyncToGPU(_renderer))
    {
        _materialPassDescriptorSet.Bind("_directionalLights", _directionalLights.GetBuffer());
    }
}