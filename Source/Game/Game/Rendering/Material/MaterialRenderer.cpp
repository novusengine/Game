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

AutoCVar_Int CVAR_VisibilityBufferDebugID("material.visibilityBufferDebugID", "Debug visualizers: 0 - Off, 1 - TypeID, 2 - ObjectID, 3 - TriangleID, 4 - ShadowCascade", 0);
AutoCVar_ShowFlag CVAR_DrawTerrainWireframe("material.drawTerrainWireframe", "Draw terrain wireframe", ShowFlag::DISABLED);
AutoCVar_ShowFlag CVAR_EnableFog("camera.enableFog", "Toggle fog", ShowFlag::DISABLED);
AutoCVar_VecFloat CVAR_FogColor("camera.fogColor", "Change fog color", vec4(0.33f, 0.2f, 0.38f, 1.0f), CVarFlags::None);
AutoCVar_Float CVAR_FogBeginDist("camera.fogBlendBegin", "Fog blending start distance", 200.0f, CVarFlags::EditFloatDrag);
AutoCVar_Float CVAR_FogEndDist("camera.fogBlendEnd", "Fog blending end distance", 600.0f, CVarFlags::EditFloatDrag);

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

}

void MaterialRenderer::AddMaterialPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    struct MaterialPassData
    {
        Renderer::ImageResource visibilityBuffer;
        Renderer::ImageResource transparency;
        Renderer::ImageResource transparencyWeights;
        Renderer::DepthImageResource depth;
        Renderer::ImageMutableResource resolvedColor;

        Renderer::DescriptorSetResource globalSet;
        Renderer::DescriptorSetResource materialSet;
        Renderer::DescriptorSetResource terrainSet;
        Renderer::DescriptorSetResource modelSet;
    };

    const i32 visibilityBufferDebugID = Math::Clamp(CVAR_VisibilityBufferDebugID.Get(), 0, 4);

    renderGraph->AddPass<MaterialPassData>("Material Pass",
        [this, &resources](MaterialPassData& data, Renderer::RenderGraphBuilder& builder) // Setup
        {
            data.visibilityBuffer = builder.Read(resources.visibilityBuffer, Renderer::PipelineType::COMPUTE);
            data.transparency = builder.Read(resources.transparency, Renderer::PipelineType::COMPUTE);
            data.transparencyWeights = builder.Read(resources.transparencyWeights, Renderer::PipelineType::COMPUTE);
            data.depth = builder.Read(resources.depth, Renderer::PipelineType::COMPUTE);
            data.resolvedColor = builder.Write(resources.sceneColor, Renderer::PipelineType::COMPUTE, Renderer::LoadMode::LOAD);

            builder.Read(resources.cameras.GetBuffer(), Renderer::BufferPassUsage::COMPUTE);

            Renderer::DescriptorSet& terrainDescriptorSet = _terrainRenderer->GetMaterialPassDescriptorSet();
            Renderer::DescriptorSet& modelDescriptorSet = _modelRenderer->GetMaterialPassDescriptorSet();

            data.globalSet = builder.Use(resources.globalDescriptorSet);
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

            const i32 shadowFilterMode = 0;// *CVarSystem::Get()->GetIntCVar("shadows.filter.mode");

            Renderer::ComputeShaderDesc shaderDesc;
            shaderDesc.path = "materialPass.cs.hlsl";
            shaderDesc.AddPermutationField("DEBUG_ID", std::to_string(visibilityBufferDebugID));
            shaderDesc.AddPermutationField("SHADOW_FILTER_MODE", std::to_string(shadowFilterMode));
            shaderDesc.AddPermutationField("SUPPORTS_EXTENDED_TEXTURES", _renderer->HasExtendedTextureSupport() ? "1" : "0");
            shaderDesc.AddPermutationField("EDITOR_MODE", CVAR_DrawTerrainWireframe.Get() == ShowFlag::ENABLED ? "1" : "0");
            pipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);

            Renderer::ComputePipelineID pipeline = _renderer->CreatePipeline(pipelineDesc);
            commandList.BeginPipeline(pipeline);

            data.modelSet.Bind("_visibilityBuffer", data.visibilityBuffer);
            data.modelSet.Bind("_transparency", data.transparency);
            data.modelSet.Bind("_transparencyWeights", data.transparencyWeights);
            data.modelSet.Bind("_depth"_h, data.depth);
            data.modelSet.BindStorage("_resolvedColor", data.resolvedColor, 0);

            // Bind descriptorset
            commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, data.globalSet, frameIndex);
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

                constants->fogColor = CVAR_FogColor.Get();
                constants->fogSettings.x = CVAR_EnableFog.Get() == ShowFlag::ENABLED;
                constants->fogSettings.y = CVAR_FogBeginDist.GetFloat();
                constants->fogSettings.z = CVAR_FogEndDist.GetFloat();

                commandList.PushConstant(constants, 0, sizeof(Constants));
            }

            uvec2 outputSize = _renderer->GetImageDimensions(resources.sceneColor, 0);

            uvec2 dispatchSize = uvec2((outputSize.x + 7) / 8, (outputSize.y + 7) / 8);
            commandList.Dispatch(dispatchSize.x, dispatchSize.y, 1);

            commandList.EndPipeline(pipeline);
        });
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
}