#pragma once
#include "Canvas.h"

#include <Base/Types.h>

#include <Renderer/Descriptors/ImageDesc.h>
#include <Renderer/Descriptors/TextureDesc.h>
#include <Renderer/DescriptorSet.h>

namespace Renderer
{
    class RenderGraph;
    class Renderer;
}
struct RenderResources;

class Window;
class CanvasRenderer
{
public:
    CanvasRenderer(Renderer::Renderer* renderer);

    void Update(f32 deltaTime);

    CanvasTextureID AddTexture(Renderer::TextureID textureID);

    Canvas& CreateCanvas(Renderer::ImageID renderTarget);

    void AddCanvasPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);

private:
    void CreatePermanentResources();

private:
    Renderer::Renderer* _renderer;
    struct Data& _data;

    Renderer::SamplerID _sampler;

    Renderer::DescriptorSet _textDescriptorSet;
    Renderer::DescriptorSet _textureDescriptorSet;
};