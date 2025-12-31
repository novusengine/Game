#pragma once
#include <Base/Types.h>

#include <Renderer/Descriptors/ImageDesc.h>
#include <Renderer/DescriptorSet.h>

namespace Renderer
{
    class RenderGraph;
    class Renderer;
}
struct RenderResources;

class Window;
class UIRenderer
{
public:
    UIRenderer(Renderer::Renderer* renderer);

    void Update(f32 deltaTime);

    void AddImguiPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex, Renderer::ImageID imguiTarget);

private:
    void CreatePermanentResources();

private:
    Renderer::Renderer* _renderer;
};