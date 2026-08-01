#pragma once

#include <Renderer/Descriptors/GraphicsPipelineDesc.h>

struct RenderResources;
class GameRenderer;

namespace Renderer
{
    class Renderer;
    class RenderGraph;
}

class MeshShaderSmoke
{
public:
    MeshShaderSmoke(Renderer::Renderer* renderer, GameRenderer* gameRenderer);

    void AddPass(Renderer::RenderGraph* renderGraph, RenderResources& resources);

private:
    Renderer::Renderer* _renderer = nullptr;
    Renderer::GraphicsPipelineID _pipeline = Renderer::GraphicsPipelineID::Invalid();
};
