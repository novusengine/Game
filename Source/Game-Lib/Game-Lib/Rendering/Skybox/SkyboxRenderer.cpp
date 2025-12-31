#include "SkyboxRenderer.h"

#include "Game-Lib/Rendering/Debug/DebugRenderer.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/RenderResources.h"
#include "Game-Lib/Util/ServiceLocator.h"
#include "Game-Lib/Application/EnttRegistries.h"

#include <Renderer/Renderer.h>
#include <Renderer/RenderGraph.h>

#include <entt/entt.hpp>

SkyboxRenderer::SkyboxRenderer(Renderer::Renderer* renderer, GameRenderer* gameRenderer, DebugRenderer* debugRenderer)
    : _renderer(renderer)
    , _gameRenderer(gameRenderer)
    //, _debugRenderer(debugRenderer)
{
    CreatePermanentResources();
}

SkyboxRenderer::~SkyboxRenderer()
{

}

void SkyboxRenderer::Update(f32 deltaTime)
{
    ZoneScoped;
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

            Renderer::RenderPassDesc renderPassDesc;
            graphResources.InitializeRenderPassDesc(renderPassDesc);

            // Render targets
            renderPassDesc.renderTargets[0] = data.skyboxColor;
            commandList.BeginRenderPass(renderPassDesc);

            // Set pipeline
            Renderer::GraphicsPipelineID pipeline = _skyboxPipeline;
            commandList.BeginPipeline(pipeline);

            commandList.BindDescriptorSet(data.globalSet, frameIndex);

            // Skyband Color Push Constant
            commandList.PushConstant(&_skybandColors, 0, sizeof(SkybandColors));

            // NumVertices hardcoded as we use a Fullscreen Triangle (Check FullscreenTriangle.vs for more information)
            commandList.Draw(3, 1, 0, 0);

            commandList.EndPipeline(pipeline);
            commandList.EndRenderPass(renderPassDesc);
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
    Renderer::GraphicsPipelineDesc pipelineDesc;

    // Shaders
    Renderer::VertexShaderDesc vertexShaderDesc;
    vertexShaderDesc.shaderEntry = _gameRenderer->GetShaderEntry("PostProcess/FullscreenTriangle.vs"_h, "PostProcess/FullscreenTriangle.vs");
    pipelineDesc.states.vertexShader = _renderer->LoadShader(vertexShaderDesc);

    Renderer::PixelShaderDesc pixelShaderDesc;
    pixelShaderDesc.shaderEntry = _gameRenderer->GetShaderEntry("Skybox/Skybox.ps"_h, "Skybox/Skybox.ps");
    pipelineDesc.states.pixelShader = _renderer->LoadShader(pixelShaderDesc);

    // Depth state
    pipelineDesc.states.depthStencilState.depthEnable = false;

    // Rasterizer state
    pipelineDesc.states.rasterizerState.cullMode = Renderer::CullMode::NONE;
    pipelineDesc.states.rasterizerState.frontFaceMode = Renderer::FrontFaceState::COUNTERCLOCKWISE;

    // Render targets
    pipelineDesc.states.renderTargetFormats[0] = _renderer->GetSwapChainImageFormat();

    _skyboxPipeline = _renderer->CreatePipeline(pipelineDesc);
}