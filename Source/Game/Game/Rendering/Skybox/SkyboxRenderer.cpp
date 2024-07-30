#include "SkyboxRenderer.h"

#include "Game/Rendering/Debug/DebugRenderer.h"
#include "Game/Rendering/RenderResources.h"
#include "Game/Util/ServiceLocator.h"
#include "Game/Application/EnttRegistries.h"

#include <Renderer/Renderer.h>
#include <Renderer/RenderGraph.h>

#include <entt/entt.hpp>

SkyboxRenderer::SkyboxRenderer(Renderer::Renderer* renderer, DebugRenderer* debugRenderer)
    : _renderer(renderer)
    //, _debugRenderer(debugRenderer)
{
    CreatePermanentResources();
}

SkyboxRenderer::~SkyboxRenderer()
{

}

void SkyboxRenderer::Update(f32 deltaTime)
{

}

void SkyboxRenderer::AddSkyboxPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    struct Data
    {
        Renderer::ImageMutableResource skyboxColor;
        Renderer::DepthImageMutableResource depth;

        Renderer::BufferResource cameras;

        Renderer::DescriptorSetResource globalSet;
    };

    renderGraph->AddPass<Data>("Skybox Pass",
        [this, &resources](Data& data, Renderer::RenderGraphBuilder& builder)
        {
            using BufferUsage = Renderer::BufferPassUsage;

            data.skyboxColor = builder.Write(resources.skyboxColor, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);
            data.depth = builder.Write(resources.depth, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);

            data.cameras = builder.Read(resources.cameras.GetBuffer(), BufferUsage::GRAPHICS);

            data.globalSet = builder.Use(resources.globalDescriptorSet);

            return true; // Return true from setup to enable this pass, return false to disable it
        },
        [this, frameIndex](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList)
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, SkyboxPass);

            Renderer::GraphicsPipelineDesc pipelineDesc;
            graphResources.InitializePipelineDesc(pipelineDesc);

            // Shaders
            Renderer::VertexShaderDesc vertexShaderDesc;
            vertexShaderDesc.path = "PostProcess/FullscreenTriangle.vs.hlsl";

            pipelineDesc.states.vertexShader = _renderer->LoadShader(vertexShaderDesc);

            Renderer::PixelShaderDesc pixelShaderDesc;
            pixelShaderDesc.path = "Skybox/Skybox.ps.hlsl";

            pipelineDesc.states.pixelShader = _renderer->LoadShader(pixelShaderDesc);

            // Depth state
            pipelineDesc.states.depthStencilState.depthEnable = false;

            // Rasterizer state
            pipelineDesc.states.rasterizerState.cullMode = Renderer::CullMode::NONE;
            pipelineDesc.states.rasterizerState.frontFaceMode = Renderer::FrontFaceState::COUNTERCLOCKWISE;

            // Render targets
            pipelineDesc.renderTargets[0] = data.skyboxColor;

            // Set pipeline
            Renderer::GraphicsPipelineID pipeline = _renderer->CreatePipeline(pipelineDesc); // This will compile the pipeline and return the ID, or just return ID of cached pipeline
            commandList.BeginPipeline(pipeline);

            commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, data.globalSet, frameIndex);

            // Skyband Color Push Constant
            commandList.PushConstant(&_skybandColors, 0, sizeof(SkybandColors));

            // NumVertices hardcoded as we use a Fullscreen Triangle (Check FullscreenTriangle.vs.hlsl for more information)
            commandList.Draw(3, 1, 0, 0);

            commandList.EndPipeline(pipeline);
        });
}

void SkyboxRenderer::SetSkybandColors(const vec3& skyTopColor, const vec3& skyMiddleColor, const vec3& skyBottomColor, const vec3& skyAboveHorizonColor, const vec3& skyHorizonColor)
{
    _skybandColors.top = vec4(skyTopColor, 0.0f);
    _skybandColors.middle = vec4(skyMiddleColor, 0.0f);
    _skybandColors.bottom = vec4(skyBottomColor, 0.0f);
    _skybandColors.aboveHorizon = vec4(skyAboveHorizonColor, 0.0f);
    _skybandColors.horizon = vec4(skyHorizonColor, 0.0f);
}

void SkyboxRenderer::CreatePermanentResources()
{

}