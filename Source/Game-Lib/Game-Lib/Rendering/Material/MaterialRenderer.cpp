#include "MaterialRenderer.h"

#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/Editor/EditorHandler.h"
#include "Game-Lib/Editor/TerrainTools.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Debug/DebugRenderer.h"
#include "Game-Lib/Rendering/Light/LightRenderer.h"
#include "Game-Lib/Rendering/Model/ModelRenderer.h"
#include "Game-Lib/Rendering/RenderResources.h"
#include "Game-Lib/Rendering/Shadow/ShadowRenderer.h"
#include "Game-Lib/Rendering/Terrain/TerrainRenderer.h"
#include "Game-Lib/Util/PhysicsUtil.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/CVarSystem/CVarSystem.h>

#include <Renderer/Renderer.h>
#include <Renderer/RenderGraph.h>

#include <entt/entt.hpp>

AutoCVar_Int CVAR_VisibilityBufferDebugID(CVarCategory::Client | CVarCategory::Rendering, "visibilityBufferDebugID", "Debug visualizers: 0 - Off, 1 - TypeID, 2 - ObjectID, 3 - TriangleID, 4 - Retired, 5 - SVSM Compare Margin", 0);
AutoCVar_ShowFlag CVAR_DrawTerrainWireframe(CVarCategory::Client | CVarCategory::Rendering, "drawTerrainWireframe", "Draw terrain wireframe", ShowFlag::DISABLED);
AutoCVar_ShowFlag CVAR_EnableFog(CVarCategory::Client | CVarCategory::Rendering, "enableFog", "Toggle fog", ShowFlag::DISABLED);
AutoCVar_VecFloat CVAR_FogColor(CVarCategory::Client | CVarCategory::Rendering, "fogColor", "Change fog color", vec4(0.33f, 0.2f, 0.38f, 1.0f), CVarFlags::None);
AutoCVar_Float CVAR_FogBeginDist(CVarCategory::Client | CVarCategory::Rendering, "fogBlendBegin", "Fog blending start distance", 200.0f, CVarFlags::EditFloatDrag);
AutoCVar_Float CVAR_FogEndDist(CVarCategory::Client | CVarCategory::Rendering, "fogBlendEnd", "Fog blending end distance", 600.0f, CVarFlags::EditFloatDrag);

MaterialRenderer::MaterialRenderer(Renderer::Renderer* renderer, GameRenderer* gameRenderer, TerrainRenderer* terrainRenderer, ModelRenderer* modelRenderer, LightRenderer* lightRenderer)
    : _renderer(renderer)
    , _gameRenderer(gameRenderer)
    , _terrainRenderer(terrainRenderer)
    , _modelRenderer(modelRenderer)
    , _lightRenderer(lightRenderer)
    , _preEffectsPassDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS)
    , _materialPassDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS)
{
    CreatePermanentResources();
}

MaterialRenderer::~MaterialRenderer()
{

}

void MaterialRenderer::Update(f32 deltaTime)
{
    ZoneScoped;

    SyncToGPU();

    Editor::Viewport* viewport = ServiceLocator::GetEditorHandler()->GetViewport();
    Util::Physics::GetMouseWorldPosition(viewport, _mouseWorldPosition);
}

void MaterialRenderer::AddPreEffectsPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    struct PreEffectsPassData
    {
        Renderer::ImageResource visibilityBuffer;
        Renderer::ImageMutableResource packedNormals;

        Renderer::DescriptorSetResource globalSet;
        Renderer::DescriptorSetResource terrainSet;
        Renderer::DescriptorSetResource modelSet;
        Renderer::DescriptorSetResource preEffectsSet;
    };

    renderGraph->AddPass<PreEffectsPassData>("Pre Effects",
        [this, &resources](PreEffectsPassData& data, Renderer::RenderGraphBuilder& builder) // Setup
        {
            data.visibilityBuffer = builder.Read(resources.visibilityBuffer, Renderer::PipelineType::COMPUTE);
            data.packedNormals = builder.Write(resources.packedNormals, Renderer::PipelineType::COMPUTE, Renderer::LoadMode::LOAD);

            builder.Read(resources.cameras.GetBuffer(), Renderer::BufferPassUsage::COMPUTE);

            data.globalSet = builder.Use(resources.globalDescriptorSet);
            data.terrainSet = builder.Use(resources.terrainDescriptorSet);
            data.modelSet = builder.Use(resources.modelDescriptorSet);
            data.preEffectsSet = builder.Use(_preEffectsPassDescriptorSet);

            _terrainRenderer->RegisterMaterialPassBufferUsage(builder);
            _modelRenderer->RegisterMaterialPassBufferUsage(builder);

            return true; // Return true from setup to enable this pass, return false to disable it
        },
        [this, &resources, frameIndex](PreEffectsPassData& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, PreEffectsPass);

            commandList.BeginPipeline(_preEffectsPipeline);

            data.preEffectsSet.Bind("_visibilityBuffer", data.visibilityBuffer);
            data.preEffectsSet.Bind("_packedNormals", data.packedNormals);

            // Bind descriptorset
            commandList.BindDescriptorSet(data.globalSet, frameIndex);
            commandList.BindDescriptorSet(data.terrainSet, frameIndex);
            commandList.BindDescriptorSet(data.modelSet, frameIndex);
            commandList.BindDescriptorSet(data.preEffectsSet, frameIndex);

            vec2 outputSize = static_cast<vec2>(_renderer->GetImageDimensions(resources.packedNormals, 0));

            struct Constants
            {
                vec4 renderInfo; // x = Render Width, y = Render Height, z = 1/Width, w = 1/Height 
            };

            Constants* constants = graphResources.FrameNew<Constants>();
            constants->renderInfo = vec4(outputSize, 1.0f / outputSize);

            uvec2 dispatchSize = uvec2((outputSize.x + 7) / 8, (outputSize.y + 7) / 8);
            commandList.Dispatch(dispatchSize.x, dispatchSize.y, 1);

            commandList.EndPipeline(_preEffectsPipeline);
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
        Renderer::ImageMutableResource resolvedColor;

        Renderer::ImageResource ambientOcclusion;

        Renderer::ImageResource svsmPagePool;
        Renderer::ImageResource svsmDynamicPagePool;

        Renderer::DescriptorSetResource debugSet;
        Renderer::DescriptorSetResource globalSet;
        Renderer::DescriptorSetResource lightSet;
        Renderer::DescriptorSetResource terrainSet;
        Renderer::DescriptorSetResource modelSet;
        Renderer::DescriptorSetResource materialSet;
    };

    const i32 visibilityBufferDebugID = Math::Clamp(CVAR_VisibilityBufferDebugID.Get(), 0, 5);

    renderGraph->AddPass<MaterialPassData>("Material Pass",
        [this, &resources](MaterialPassData& data, Renderer::RenderGraphBuilder& builder) // Setup
        {
            data.visibilityBuffer = builder.Read(resources.visibilityBuffer, Renderer::PipelineType::COMPUTE);
            data.skyboxColor = builder.Read(resources.skyboxColor, Renderer::PipelineType::COMPUTE);
            data.transparency = builder.Read(resources.transparency, Renderer::PipelineType::COMPUTE);
            data.transparencyWeights = builder.Read(resources.transparencyWeights, Renderer::PipelineType::COMPUTE);
            data.resolvedColor = builder.Write(resources.sceneColor, Renderer::PipelineType::COMPUTE, Renderer::LoadMode::LOAD);

            data.ambientOcclusion = builder.Read(resources.ssaoTarget, Renderer::PipelineType::COMPUTE);

            builder.Read(resources.cameras.GetBuffer(), Renderer::BufferPassUsage::COMPUTE);
            builder.Read(_directionalLights.GetBuffer(), Renderer::BufferPassUsage::COMPUTE);

            // SVSM sampling reads the page pools the geometry passes wrote this frame, the graph
            // needs the dependencies declared for the barriers. The pool bindings must always be
            // valid, a placeholder stands in until the real pools are lazily created
            ShadowRenderer* shadowRenderer = _gameRenderer->GetShadowRenderer();
            data.svsmPagePool = builder.Read(shadowRenderer->GetSVSMPagePoolOrPlaceholder(), Renderer::PipelineType::COMPUTE);
            data.svsmDynamicPagePool = builder.Read(shadowRenderer->GetSVSMDynamicPagePoolOrPlaceholder(), Renderer::PipelineType::COMPUTE);
            builder.Read(shadowRenderer->GetSVSMDataBuffer(), Renderer::BufferPassUsage::COMPUTE);
            builder.Read(shadowRenderer->GetSVSMPageTableBuffer(), Renderer::BufferPassUsage::COMPUTE);
            builder.Read(shadowRenderer->GetSVSMDynamicPageTableBuffer(), Renderer::BufferPassUsage::COMPUTE);

            data.debugSet = builder.Use(resources.debugDescriptorSet);
            data.globalSet = builder.Use(resources.globalDescriptorSet);
            data.lightSet = builder.Use(resources.lightDescriptorSet);
            data.terrainSet = builder.Use(resources.terrainDescriptorSet);
            data.modelSet = builder.Use(resources.modelDescriptorSet);
            data.materialSet = builder.Use(_materialPassDescriptorSet);

            _terrainRenderer->RegisterMaterialPassBufferUsage(builder);
            _modelRenderer->RegisterMaterialPassBufferUsage(builder);
            _lightRenderer->RegisterMaterialPassBufferUsage(builder);

            return true; // Return true from setup to enable this pass, return false to disable it
        },
        [this, &resources, frameIndex, visibilityBufferDebugID](MaterialPassData& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, MaterialPass);

            commandList.BeginPipeline(_materialPipeline);

            // For some reason, this works when we're binding to modelSet despite the GPU side being bound as PER_PASS???
            data.materialSet.Bind("_visibilityBuffer", data.visibilityBuffer);
            data.materialSet.Bind("_skyboxColor", data.skyboxColor);
            data.materialSet.Bind("_transparency", data.transparency);
            data.materialSet.Bind("_transparencyWeights", data.transparencyWeights);
            data.materialSet.Bind("_resolvedColor", data.resolvedColor);

            data.materialSet.Bind("_ambientOcclusion", data.ambientOcclusion);

            data.lightSet.Bind("_svsmPagePool"_h, data.svsmPagePool);
            data.lightSet.Bind("_svsmDynamicPagePool"_h, data.svsmDynamicPagePool);

            // The debug permutations dead-strip descriptor set usage, so each variant binds
            // exactly what it statically references or the set-usage validation fires
            switch (visibilityBufferDebugID)
            {
                case 1: // TypeID, pure math on the visibility buffer
                case 3: // TriangleID
                    commandList.BindDescriptorSet(data.materialSet, frameIndex);
                    break;
                case 2: // ObjectID reads terrain instance data
                    commandList.BindDescriptorSet(data.terrainSet, frameIndex);
                    commandList.BindDescriptorSet(data.materialSet, frameIndex);
                    break;
                case 4: // Retired (was CascadeID), the variant only reads the visibility buffer
                    commandList.BindDescriptorSet(data.materialSet, frameIndex);
                    break;
                case 5: // SVSM compare margin reconstructs vertex data and samples the virtual shadow map
                    commandList.BindDescriptorSet(data.globalSet, frameIndex);
                    commandList.BindDescriptorSet(data.lightSet, frameIndex);
                    commandList.BindDescriptorSet(data.terrainSet, frameIndex);
                    commandList.BindDescriptorSet(data.modelSet, frameIndex);
                    commandList.BindDescriptorSet(data.materialSet, frameIndex);
                    break;
                default: // Full shading
                    commandList.BindDescriptorSet(data.debugSet, frameIndex);
                    commandList.BindDescriptorSet(data.globalSet, frameIndex);
                    commandList.BindDescriptorSet(data.lightSet, frameIndex);
                    commandList.BindDescriptorSet(data.terrainSet, frameIndex);
                    commandList.BindDescriptorSet(data.modelSet, frameIndex);
                    commandList.BindDescriptorSet(data.materialSet, frameIndex);
                    break;
            }

            //if (CVAR_DrawTerrainWireframe.Get() == ShowFlag::ENABLED)
            {
                struct Constants
                {
                    vec4 renderInfo; // x = Render Width, y = Render Height, z = 1/Width, w = 1/Height
                    uvec4 lightInfo; // x = Directional Light Count, y = Shadows Ready (enabled, strength > 0, pool created), zw = UNUSED
                    uvec4 tileInfo; // xy = Num Tiles, zw = UNUSED
                    vec4 fogColor;
                    vec4 fogSettings; // x = Enabled, y = Begin Fog Blend Dist, z = End Fog Blend Dist, w = UNUSED
                    vec4 mouseWorldPos;
                    vec4 brushSettings; // x = hardness, y = radius, z = pressure, w = falloff
                    Color chunkEdgeColor;
                    Color cellEdgeColor;
                    Color patchEdgeColor;
                    Color vertexColor;
                    Color brushColor;
                    vec4 shadowSettings; // x = Shadow Strength, y = Normal Offset Bias, z = SVSM Constant Bias (world meters), w = UNUSED
                };

                Constants* constants = graphResources.FrameNew<Constants>();

                vec2 outputSize = static_cast<vec2>(_renderer->GetImageDimensions(resources.sceneColor, 0));
                constants->renderInfo = vec4(outputSize, 1.0f / outputSize);

                CVarSystem* cvarSystem = CVarSystem::Get();
                const f32 shadowStrength = static_cast<f32>(*cvarSystem->GetFloatCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowStrength"));
                // Shadows sample only once the (lazily created) page pool exists, the placeholder
                // pool bound before then must never be read
                ShadowRenderer* shadowRenderer = _gameRenderer->GetShadowRenderer();
                const bool shadowsReady = static_cast<u32>(*cvarSystem->GetIntCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowEnabled")) != 0
                    && shadowStrength > 0.0f
                    && shadowRenderer != nullptr && shadowRenderer->GetSVSMPagePool() != Renderer::ImageID::Invalid();
                constants->lightInfo = uvec4(static_cast<u32>(_directionalLights.Count()), shadowsReady ? 1 : 0, 0, 0);

                constants->tileInfo = uvec4(_lightRenderer->CalculateNumTiles2D(outputSize), 0, 0);

                constants->fogColor = CVAR_FogColor.Get();
                constants->fogSettings.x = CVAR_EnableFog.Get() == ShowFlag::ENABLED;
                constants->fogSettings.y = CVAR_FogBeginDist.GetFloat();
                constants->fogSettings.z = CVAR_FogEndDist.GetFloat();

                constants->mouseWorldPos = vec4(_mouseWorldPosition, 1.0f);
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

                f32 shadowNormalOffsetBias = static_cast<f32>(*cvarSystem->GetFloatCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowNormalOffsetBias"));
                f32 svsmConstantBias = static_cast<f32>(*cvarSystem->GetFloatCVar(CVarCategory::Client | CVarCategory::Rendering, "svsmConstantBias"));
                constants->shadowSettings = vec4(shadowStrength, shadowNormalOffsetBias, svsmConstantBias, 0.0f);

                commandList.PushConstant(constants, 0, sizeof(Constants));
            }

            uvec2 outputSize = _renderer->GetImageDimensions(resources.sceneColor, 0);

            uvec2 dispatchSize = uvec2((outputSize.x + 7) / 8, (outputSize.y + 7) / 8);
            commandList.Dispatch(dispatchSize.x, dispatchSize.y, 1);

            commandList.EndPipeline(_materialPipeline);
        });
}

void MaterialRenderer::AddDirectionalLight(const vec3& direction, const vec3& color, f32 intensity, const vec3& groundAmbientColor, f32 groundAmbientIntensity, const vec3& skyAmbientColor, f32 skyAmbientIntensity, const vec3& shadowColor)
{
    DirectionalLight light
    {
        .direction = vec4(direction, 0.0f),
        .color = vec4(color, intensity),
        .groundAmbientColor = vec4(groundAmbientColor, groundAmbientIntensity),
        .skyAmbientColor = vec4(skyAmbientColor, skyAmbientIntensity),
        .shadowColor = vec4(shadowColor, 0.0f)
    };

    _directionalLights.Add(light);
}

bool MaterialRenderer::SetDirectionalLight(u32 index, const vec3& direction, const vec3& color, f32 intensity, const vec3& groundAmbientColor, f32 groundAmbientIntensity, const vec3& skyAmbientColor, f32 skyAmbientIntensity, const vec3& shadowColor)
{
    u32 numDirectionalLights = static_cast<u32>(_directionalLights.Count());
    if (index >= numDirectionalLights)
        return false;

    DirectionalLight& light = _directionalLights[index];

    light.direction = vec4(direction, 0.0f);
    light.color = vec4(color, intensity);
    light.groundAmbientColor = vec4(groundAmbientColor, groundAmbientIntensity);
    light.skyAmbientColor = vec4(skyAmbientColor, skyAmbientIntensity);
    light.shadowColor = vec4(shadowColor, 0.0f);

    _directionalLights.SetDirtyElement(index);

    return true;
}

void MaterialRenderer::CreatePermanentResources()
{
    // Create Pre-Effects Pipeline
    Renderer::ComputeShaderDesc shaderDesc;
    shaderDesc.shaderEntry = _gameRenderer->GetShaderEntry("Material/PreEffectsPass.cs"_h, "Material/PreEffectsPass.cs");

    Renderer::ComputePipelineDesc pipelineDesc;
    pipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);
    _preEffectsPipeline = _renderer->CreatePipeline(pipelineDesc);

    // Create Material Pipeline
    CreateMaterialPipeline();

    CVAR_VisibilityBufferDebugID.AddOnValueChanged([this](const i32& val)
    {
        CreateMaterialPipeline();
    });
    CVAR_DrawTerrainWireframe.AddOnValueChanged([this](const ShowFlag& val)
    {
        CreateMaterialPipeline();
    });

    // Register pipelines with descriptor sets and init
    _preEffectsPassDescriptorSet.RegisterPipeline(_renderer, _preEffectsPipeline);
    _preEffectsPassDescriptorSet.Init(_renderer);

    _materialPassDescriptorSet.RegisterPipeline(_renderer, _materialPipeline);
    _materialPassDescriptorSet.Init(_renderer);

    Renderer::SamplerDesc samplerDesc;
    samplerDesc.enabled = true;
    samplerDesc.filter = Renderer::SamplerFilter::MIN_MAG_MIP_LINEAR;
    samplerDesc.addressU = Renderer::TextureAddressMode::WRAP;
    samplerDesc.addressV = Renderer::TextureAddressMode::WRAP;
    samplerDesc.addressW = Renderer::TextureAddressMode::CLAMP;
    samplerDesc.shaderVisibility = Renderer::ShaderVisibility::ALL;

    _sampler = _renderer->CreateSampler(samplerDesc);
    _preEffectsPassDescriptorSet.Bind("_sampler"_h, _sampler);
    _materialPassDescriptorSet.Bind("_sampler"_h, _sampler);

    _directionalLights.SetDebugName("Directional Lights");
    _directionalLights.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

    // Debug directional light
    vec3 direction = vec3(0.0f, -1.0f, 0.0f);
    vec3 color = vec3(66.f/255.f, 101.f/255.f, 134.f/255.f);
    f32 intensity = 1.0f;

    vec3 ambientColor = vec3(14.f/255.f, 30.f/255.f, 52.f/255.f);

    vec3 groundAmbientColor = ambientColor * 0.7f;
    f32 groundAmbientIntensity = 1.0f;
    vec3 skyAmbientColor = ambientColor * 1.1f;
    f32 skyAmbientIntensity = 1.0f;
    vec3 shadowColor = vec3(77.f/255.f, 77.f/255.f, 77.f/255.f);

    AddDirectionalLight(direction, color, intensity, groundAmbientColor, groundAmbientIntensity, skyAmbientColor, skyAmbientIntensity, shadowColor);
}

void MaterialRenderer::CreateMaterialPipeline()
{
    std::vector<Renderer::PermutationField> permutationFields =
    {
        { "DEBUG_ID", std::to_string(CVAR_VisibilityBufferDebugID.Get()) },
        { "EDITOR_MODE", CVAR_DrawTerrainWireframe.Get() == ShowFlag::ENABLED ? "1" : "0" }
    };
    u32 shaderEntryNameHash = Renderer::GetShaderEntryNameHash("Material/MaterialPass.cs", permutationFields);

    Renderer::ComputeShaderDesc shaderDesc;
    shaderDesc.shaderEntry = shaderDesc.shaderEntry = _gameRenderer->GetShaderEntry(shaderEntryNameHash, "Material/MaterialPass.cs");

    Renderer::ComputePipelineDesc pipelineDesc;
    pipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);

    _materialPipeline = _renderer->CreatePipeline(pipelineDesc);
}

void MaterialRenderer::SyncToGPU()
{
    if (_directionalLights.SyncToGPU(_renderer))
    {
        _materialPassDescriptorSet.Bind("_directionalLights", _directionalLights.GetBuffer());
    }
}