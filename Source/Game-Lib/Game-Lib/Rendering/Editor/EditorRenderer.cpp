#include "EditorRenderer.h"

#include "Game-Lib/Rendering/Debug/DebugRenderer.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/RenderResources.h"
#include "Game-Lib/Util/ServiceLocator.h"
#include "Game-Lib/Application/EnttRegistries.h"

#include <Renderer/Renderer.h>
#include <Renderer/RenderGraph.h>

#include <Base/CVarSystem/CVarSystem.h>

#include <entt/entt.hpp>

AutoCVar_Int CVAR_WorldGridEnabled(CVarCategory::Client | CVarCategory::Rendering, "worldGridEnabled", "enable world grid rendering", 1, CVarFlags::EditCheckbox);
AutoCVar_Float CVAR_WorldGridFadeStart(CVarCategory::Client | CVarCategory::Rendering, "worldGridFadeStart", "set the starting value from where the world grid will start fading", 80.0f);
AutoCVar_Float CVAR_WorldGridFadeEnd(CVarCategory::Client | CVarCategory::Rendering, "worldGridFadeEnd", "set the starting value from where the world grid will stop fading", 110.0f);

EditorRenderer::EditorRenderer(Renderer::Renderer* renderer, GameRenderer* gameRenderer, DebugRenderer* debugRenderer)
    : _renderer(renderer)
    , _gameRenderer(gameRenderer)
    //, _debugRenderer(debugRenderer)
{
    CreatePermanentResources();
}

EditorRenderer::~EditorRenderer()
{

}

void EditorRenderer::Update(f32 deltaTime)
{
    ZoneScoped;
}

void EditorRenderer::AddWorldGridPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    if (!CVAR_WorldGridEnabled.Get())
        return;

    struct Data
    {
        Renderer::ImageMutableResource sceneColor;
        Renderer::DepthImageMutableResource depth;

        Renderer::DescriptorSetResource globalSet;
    };

    renderGraph->AddPass<Data>("World Grid",
        [this, &resources](Data& data, Renderer::RenderGraphBuilder& builder)
        {
            using BufferUsage = Renderer::BufferPassUsage;

            data.sceneColor = builder.Write(resources.sceneColor, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);
            data.depth = builder.Write(resources.depth, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);

            builder.Read(resources.cameras.GetBuffer(), BufferUsage::GRAPHICS);

            data.globalSet = builder.Use(resources.globalDescriptorSet);

            return true; // Return true from setup to enable this pass, return false to disable it
        },
        [this, frameIndex](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList)
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, SkyboxPass);

            Renderer::RenderPassDesc renderPassDesc;
            graphResources.InitializeRenderPassDesc(renderPassDesc);

            // Render targets
            renderPassDesc.renderTargets[0] = data.sceneColor;
            renderPassDesc.depthStencil = data.depth;
            commandList.BeginRenderPass(renderPassDesc);

            // Set pipeline
            Renderer::GraphicsPipelineID pipeline = _worldGridPipeline;
            commandList.BeginPipeline(pipeline);

            commandList.BindDescriptorSet(data.globalSet, frameIndex);

            struct Constants
            {
            public:
                f32 fadeStart;
                f32 fadeEnd;
            };

            Constants* constants = graphResources.FrameNew<Constants>();
            constants->fadeStart = CVAR_WorldGridFadeStart.GetFloat();
            constants->fadeEnd = CVAR_WorldGridFadeEnd.GetFloat();

            commandList.PushConstant(constants, 0, sizeof(Constants));

            // NumVertices hardcoded as we use a fullscreen quad
            commandList.Draw(6, 1, 0, 0);

            commandList.EndPipeline(pipeline);
            commandList.EndRenderPass(renderPassDesc);
        });
}

void EditorRenderer::CreatePermanentResources()
{
    Renderer::GraphicsPipelineDesc pipelineDesc;

    // Shaders
    Renderer::VertexShaderDesc vertexShaderDesc;
    vertexShaderDesc.shaderEntry = _gameRenderer->GetShaderEntry("Editor/WorldGrid.vs"_h, "Editor/WorldGrid.vs");
    pipelineDesc.states.vertexShader = _renderer->LoadShader(vertexShaderDesc);

    Renderer::PixelShaderDesc pixelShaderDesc;
    pixelShaderDesc.shaderEntry = _gameRenderer->GetShaderEntry("Editor/WorldGrid.ps"_h, "Editor/WorldGrid.ps");
    pipelineDesc.states.pixelShader = _renderer->LoadShader(pixelShaderDesc);

    // Depth state
    pipelineDesc.states.depthStencilState.depthEnable = true;
    pipelineDesc.states.depthStencilState.depthFunc = Renderer::ComparisonFunc::GREATER_EQUAL;

    // Rasterizer state
    pipelineDesc.states.rasterizerState.cullMode = Renderer::CullMode::NONE;
    pipelineDesc.states.rasterizerState.frontFaceMode = Renderer::FrontFaceState::COUNTERCLOCKWISE;

    // Blending
    pipelineDesc.states.blendState.renderTargets[0].blendEnable = true;
    pipelineDesc.states.blendState.renderTargets[0].srcBlend = Renderer::BlendMode::SRC_ALPHA;
    pipelineDesc.states.blendState.renderTargets[0].destBlend = Renderer::BlendMode::INV_SRC_ALPHA;
    pipelineDesc.states.blendState.renderTargets[0].srcBlendAlpha = Renderer::BlendMode::ZERO;
    pipelineDesc.states.blendState.renderTargets[0].destBlendAlpha = Renderer::BlendMode::ONE;

    // Render targets
    pipelineDesc.states.renderTargetFormats[0] = _renderer->GetSwapChainImageFormat();
    pipelineDesc.states.depthStencilFormat = Renderer::DepthImageFormat::D32_FLOAT;

    _worldGridPipeline = _worldGridPipeline = _renderer->CreatePipeline(pipelineDesc);
}