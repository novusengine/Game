#pragma once
#include <Base/Types.h>

#include <Renderer/DescriptorSet.h>
#include <Renderer/Descriptors/SamplerDesc.h>

namespace Renderer
{
    class Renderer;
    class RenderGraph;

}

class TerrainRenderer;
class ModelRenderer;
struct RenderResources;

class MaterialRenderer
{
public:
    MaterialRenderer(Renderer::Renderer* renderer, TerrainRenderer* terrainRenderer, ModelRenderer* modelRenderer);
    ~MaterialRenderer();

    void Update(f32 deltaTime);

    void AddMaterialPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);

private:
    void CreatePermanentResources();

private:
    Renderer::Renderer* _renderer;

    Renderer::DescriptorSet _materialPassDescriptorSet;

    Renderer::SamplerID _sampler;

    TerrainRenderer* _terrainRenderer = nullptr;
    ModelRenderer* _modelRenderer = nullptr;
};
