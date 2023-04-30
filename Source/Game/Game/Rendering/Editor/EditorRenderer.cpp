#include "EditorRenderer.h"

#include "Game/Rendering/Debug/DebugRenderer.h"
#include "Game/Rendering/RenderResources.h"
#include "Game/Util/ServiceLocator.h"
#include "Game/Application/EnttRegistries.h"

#include <Renderer/Renderer.h>
#include <Renderer/RenderGraph.h>

#include <entt/entt.hpp>

AutoCVar_Int CVAR_WorldGridEnabled("editorRenderer.worldgrid.enabled", "enable world grid rendering", 1, CVarFlags::EditCheckbox);
AutoCVar_Float CVAR_WorldGridFadeStart("editorRenderer.worldgrid.fadestart", "set the starting value from where the world grid will start fading", 80.0f);
AutoCVar_Float CVAR_WorldGridFadeEnd("editorRenderer.worldgrid.fadeend", "set the starting value from where the world grid will stop fading", 110.0f);

EditorRenderer::EditorRenderer(Renderer::Renderer* renderer, DebugRenderer* debugRenderer)
    : _renderer(renderer)
    //, _debugRenderer(debugRenderer)
{
    CreatePermanentResources();
}

EditorRenderer::~EditorRenderer()
{

}

void EditorRenderer::Update(f32 deltaTime)
{

}

void EditorRenderer::AddWorldGridPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    if (!CVAR_WorldGridEnabled.Get())
        return;

    struct Data
    {
        Renderer::ImageMutableResource finalColor;
        Renderer::DepthImageMutableResource depth;

        Renderer::DescriptorSetResource globalSet;
    };

    renderGraph->AddPass<Data>("World Grid",
        [=, &resources](Data& data, Renderer::RenderGraphBuilder& builder)
        {
            data.finalColor = builder.Write(resources.finalColor, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);
            data.depth = builder.Write(resources.depth, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);

            data.globalSet = builder.Use(resources.globalDescriptorSet);

            return true; // Return true from setup to enable this pass, return false to disable it
        },
        [=](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList)
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, SkyboxPass);

            Renderer::GraphicsPipelineDesc pipelineDesc;
            graphResources.InitializePipelineDesc(pipelineDesc);

            // Shaders
            Renderer::VertexShaderDesc vertexShaderDesc;
            vertexShaderDesc.path = "Editor/WorldGrid.vs.hlsl";

            pipelineDesc.states.vertexShader = _renderer->LoadShader(vertexShaderDesc);

            Renderer::PixelShaderDesc pixelShaderDesc;
            pixelShaderDesc.path = "Editor/WorldGrid.ps.hlsl";

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
            pipelineDesc.renderTargets[0] = data.finalColor;

            pipelineDesc.depthStencil = data.depth;

            // Set pipeline
            Renderer::GraphicsPipelineID pipeline = _renderer->CreatePipeline(pipelineDesc); // This will compile the pipeline and return the ID, or just return ID of cached pipeline
            commandList.BeginPipeline(pipeline);

            commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, data.globalSet, frameIndex);

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
        });
}

void EditorRenderer::CreatePermanentResources()
{

}