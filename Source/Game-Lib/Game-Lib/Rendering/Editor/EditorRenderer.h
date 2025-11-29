#pragma once
#include <Base/Types.h>

#include <Renderer/Descriptors/GraphicsPipelineDesc.h>

namespace Renderer
{
    class Renderer;
    class RenderGraph;
}

class DebugRenderer;
class GameRenderer;
struct RenderResources;

class EditorRenderer
{
public:
    EditorRenderer(Renderer::Renderer* renderer, GameRenderer* gameRenderer, DebugRenderer* debugRenderer);
    ~EditorRenderer();

    void Update(f32 deltaTime);

    void AddWorldGridPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);

private:
    void CreatePermanentResources();

    Renderer::Renderer* _renderer;
    GameRenderer* _gameRenderer;
    //DebugRenderer* _debugRenderer;

    Renderer::GraphicsPipelineID _worldGridPipeline;
};
