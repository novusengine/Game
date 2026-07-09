#pragma once
#include <Base/Types.h>
#include <Renderer/RenderSettings.h>
#include <Renderer/GPUVector.h>
#include <Renderer/Buffer.h>

#include <Renderer/Descriptors/SamplerDesc.h>
#include <Renderer/Descriptors/TextureArrayDesc.h>

namespace Renderer
{
    class Renderer;
    class RenderGraph;
}

struct RenderResources;
class DebugRenderer;
class GameRenderer;
class ModelRenderer;
class TerrainRenderer;

class ShadowRenderer
{
public:
    ShadowRenderer(Renderer::Renderer* renderer, GameRenderer* gameRenderer, DebugRenderer* debugRenderer, TerrainRenderer* terrainRenderer, ModelRenderer* modelRenderer, RenderResources& resources);
    ~ShadowRenderer();

    void Update(f32 deltaTime, RenderResources& resources);

    void AddShadowPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);

private:
    void CreatePermanentResources(RenderResources& resources);

private:
    Renderer::Renderer* _renderer = nullptr;
    GameRenderer* _gameRenderer = nullptr;
    DebugRenderer* _debugRenderer = nullptr;
    TerrainRenderer* _terrainRenderer = nullptr;
    ModelRenderer* _modelRenderer = nullptr;

    Renderer::SamplerID _shadowCmpSampler;
    Renderer::SamplerID _shadowPointClampSampler;
};