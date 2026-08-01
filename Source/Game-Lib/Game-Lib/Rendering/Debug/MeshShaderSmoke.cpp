#include "MeshShaderSmoke.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/RenderResources.h"

#include <Base/CVarSystem/CVarSystem.h>

#include <Renderer/Renderer.h>
#include <Renderer/RenderGraph.h>

AutoCVar_ShowFlag CVAR_MeshShaderSmoke(CVarCategory::Client | CVarCategory::Rendering, "meshShaderSmoke", "Draw the mesh shader bring-up triangle", ShowFlag::DISABLED);

MeshShaderSmoke::MeshShaderSmoke(Renderer::Renderer* renderer, GameRenderer* gameRenderer)
    : _renderer(renderer)
{
    const Renderer::MeshShaderProperties& properties = _renderer->GetMeshShaderProperties();
    if (properties.maxOutputVertices < 3 || properties.maxOutputPrimitives < 1 || properties.maxWorkGroupInvocations < 1)
    {
        NC_LOG_CRITICAL("Mesh shader smoke requirements exceed the selected GPU limits");
    }

    Renderer::MeshShaderDesc meshShaderDesc;
    meshShaderDesc.shaderEntry = gameRenderer->GetShaderEntry("Debug/MeshShaderSmoke.ms"_h, "Debug/MeshShaderSmoke.ms");

    Renderer::PixelShaderDesc pixelShaderDesc;
    pixelShaderDesc.shaderEntry = gameRenderer->GetShaderEntry("Debug/MeshShaderSmoke.ps"_h, "Debug/MeshShaderSmoke.ps");

    Renderer::GraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "MeshShaderSmoke";
    pipelineDesc.states.rasterizerState.cullMode = Renderer::CullMode::NONE;
    pipelineDesc.states.renderTargetFormats[0] = _renderer->GetSwapChainImageFormat();
    pipelineDesc.shaderStages = Renderer::MeshPipelineStages{ .meshShader = _renderer->LoadShader(meshShaderDesc) };
    pipelineDesc.states.pixelShader = _renderer->LoadShader(pixelShaderDesc);
    _pipeline = _renderer->CreatePipeline(pipelineDesc);
}

void MeshShaderSmoke::AddPass(Renderer::RenderGraph* renderGraph, RenderResources& resources)
{
    struct Data
    {
        Renderer::ImageMutableResource color;
    };

    renderGraph->AddPass<Data>("MeshShaderSmoke",
        [&resources](Data& data, Renderer::RenderGraphBuilder& builder)
        {
            data.color = builder.Write(resources.sceneColor, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);
            return CVAR_MeshShaderSmoke.Get() == ShowFlag::ENABLED;
        },
        [this](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList)
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, MeshShaderSmoke);

            Renderer::RenderPassDesc renderPassDesc;
            graphResources.InitializeRenderPassDesc(renderPassDesc);
            renderPassDesc.renderTargets[0] = data.color;
            commandList.BeginRenderPass(renderPassDesc);
            commandList.BeginPipeline(_pipeline);
            commandList.DrawMeshTasks(1, 1, 1);
            commandList.EndPipeline(_pipeline);
            commandList.EndRenderPass(renderPassDesc);
        });
}
