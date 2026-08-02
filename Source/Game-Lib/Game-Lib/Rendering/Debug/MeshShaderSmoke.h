#pragma once

#include <Renderer/Descriptors/GraphicsPipelineDesc.h>

struct RenderResources;
class GameRenderer;

namespace Renderer
{
    class Renderer;
    class RenderGraph;
}

// Owns the GPU-side bring-up pipeline for the opt-in mesh-shader smoke draw.
// It emits a known triangle used to verify mesh-pipeline creation and mesh-task dispatch.
class MeshShaderSmoke
{
public:
    MeshShaderSmoke(Renderer::Renderer* renderer, GameRenderer* gameRenderer);

    void AddPass(Renderer::RenderGraph* renderGraph, RenderResources& resources);

private:
    Renderer::Renderer* _renderer = nullptr;
    Renderer::GraphicsPipelineID _pipeline = Renderer::GraphicsPipelineID::Invalid();
};
