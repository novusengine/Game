#include "MaterialRenderer.h"

#include "Game/Rendering/Debug/DebugRenderer.h"
#include "Game/Rendering/RenderResources.h"
#include "Game/Rendering/Terrain/TerrainRenderer.h"
#include "Game/Rendering/Model/ModelRenderer.h"
#include "Game/Util/ServiceLocator.h"
#include "Game/Application/EnttRegistries.h"

#include <Base/CVarSystem/CVarSystem.h>

#include <Renderer/Renderer.h>
#include <Renderer/RenderGraph.h>

#include <entt/entt.hpp>

AutoCVar_Int CVAR_VisibilityBufferDebugID("material.visibilityBufferDebugID", "Debug visualizers: 0 - Off, 1 - TypeID, 2 - ObjectID, 3 - TriangleID, 4 - ShadowCascade", 0);

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
        [=, &resources](MaterialPassData& data, Renderer::RenderGraphBuilder& builder) // Setup
        {
            data.visibilityBuffer = builder.Read(resources.visibilityBuffer, Renderer::PipelineType::COMPUTE);
            data.transparency = builder.Read(resources.transparency, Renderer::PipelineType::COMPUTE);
            data.transparencyWeights = builder.Read(resources.transparencyWeights, Renderer::PipelineType::COMPUTE);
            data.depth = builder.Read(resources.depth, Renderer::PipelineType::COMPUTE);
            data.resolvedColor = builder.Write(resources.finalColor, Renderer::PipelineType::COMPUTE, Renderer::LoadMode::LOAD);

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
        [=](MaterialPassData& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, MaterialPass);

            Renderer::ComputePipelineDesc pipelineDesc;
            graphResources.InitializePipelineDesc(pipelineDesc);

            const i32 shadowFilterMode = 0;// *CVarSystem::Get()->GetIntCVar("shadows.filter.mode");

            Renderer::ComputeShaderDesc shaderDesc;
            shaderDesc.path = "materialPass.cs.hlsl";
            shaderDesc.AddPermutationField("DEBUG_ID", std::to_string(visibilityBufferDebugID));
            shaderDesc.AddPermutationField("SHADOW_FILTER_MODE", std::to_string(shadowFilterMode));
            pipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);

            Renderer::ComputePipelineID pipeline = _renderer->CreatePipeline(pipelineDesc);
            commandList.BeginPipeline(pipeline);

            // Reenable this in shader as well
            /*
            if (visibilityBufferDebugID == 0 || visibilityBufferDebugID == 4)
            {
                // Push constant
                struct PushConstants
                {
                    vec4 mouseWorldPosition;
                    u32 numCascades;
                    f32 shadowFilterSize;
                    f32 shadowPenumbraFilterSize;
                    u32 enabledShadows;
                    u32 numTextureDecals;
                    u32 numProceduralDecals;
                };

                PushConstants* constants = graphResources.FrameNew<PushConstants>();

                /*Editor::Viewport* viewport = ServiceLocator::GetEditorHandler()->GetViewport();
                vec3 mouseWorldPosition;
                viewport->GetMouseWorldPosition(mouseWorldPosition);
                constants->mouseWorldPosition = vec4(mouseWorldPosition, 1.0f);*/
                /*
                constants->numCascades = 0;// *CVarSystem::Get()->GetIntCVar("shadows.cascade.num");
                constants->shadowFilterSize = 0;// static_cast<f32>(*CVarSystem::Get()->GetFloatCVar("shadows.filter.size"));
                constants->shadowPenumbraFilterSize = 0;//static_cast<f32>(*CVarSystem::Get()->GetFloatCVar("shadows.filter.penumbraSize"));
                constants->enabledShadows = 0;//*CVarSystem::Get()->GetIntCVar("shadows.enable");
                constants->numTextureDecals = 0;//_numTextureDecals;
                constants->numProceduralDecals = 0;// _numProceduralDecals;

                commandList.PushConstant(constants, 0, sizeof(PushConstants));
            }*/

            data.modelSet.Bind("_visibilityBuffer", data.visibilityBuffer);
            data.modelSet.Bind("_transparency", data.transparency);
            data.modelSet.Bind("_transparencyWeights", data.transparencyWeights);
            data.modelSet.Bind("_depth"_h, data.depth);
            //_materialPassDescriptorSet.Bind("_ambientOcclusion", data.ambientObscurance);
            data.modelSet.BindStorage("_resolvedColor", data.resolvedColor, 0);

            // Bind descriptorset
            commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, data.globalSet, frameIndex);
            //commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::SHADOWS, &resources.shadowDescriptorSet, frameIndex);
            commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::TERRAIN, data.terrainSet, frameIndex);
            commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::MODEL, data.modelSet, frameIndex);
            commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS, data.materialSet, frameIndex);

            uvec2 outputSize = _renderer->GetImageDimensions(resources.finalColor, 0);

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