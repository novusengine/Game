#pragma once
#include <Base/Types.h>

#include <Renderer/DescriptorSet.h>
#include <Renderer/Descriptors/SamplerDesc.h>
#include <Renderer/GPUVector.h>

namespace Renderer
{
    class Renderer;
    class RenderGraph;

}

struct RenderResources;
//struct FfxCacaoContext;
//struct FfxCacaoSettings;

class EffectRenderer
{
public:
    EffectRenderer(Renderer::Renderer* renderer);
    ~EffectRenderer();

    void Update(f32 deltaTime);

    void AddSSAOPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);

private:
    void CreatePermanentResources();

private:
    Renderer::Renderer* _renderer;

    //FfxCacaoContext* _cacaoContext = nullptr;
    //FfxCacaoSettings* _cacaoSettings = nullptr;

    mat4x4 _proj;
    mat4x4 _normalsWorldToView;

    vec2 _lastRenderSize = vec2(0,0);
};
