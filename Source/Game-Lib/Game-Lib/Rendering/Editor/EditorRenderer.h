#pragma once
#include <Base/Types.h>

namespace Renderer
{
    class Renderer;
    class RenderGraph;
}

class DebugRenderer;
struct RenderResources;

class EditorRenderer
{
public:
    EditorRenderer(Renderer::Renderer* renderer, DebugRenderer* debugRenderer);
    ~EditorRenderer();

    void Update(f32 deltaTime);

    void AddWorldGridPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);

private:
    void CreatePermanentResources();

    Renderer::Renderer* _renderer;
    //DebugRenderer* _debugRenderer;
};
