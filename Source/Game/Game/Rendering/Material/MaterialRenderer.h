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

class TerrainRenderer;
class ModelRenderer;
struct RenderResources;

class MaterialRenderer
{
public:
    MaterialRenderer(Renderer::Renderer* renderer, TerrainRenderer* terrainRenderer, ModelRenderer* modelRenderer);
    ~MaterialRenderer();

    void Update(f32 deltaTime);

    // Resolves normals for the effect passes
    void AddPreEffectsPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
    void AddMaterialPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);

    void AddDirectionalLight(const vec3& direction, const vec3& color, f32 intensity, const vec3& groundAmbientColor, f32 groundAmbientIntensity, const vec3& skyAmbientColor, f32 skyAmbientIntensity);
    bool SetDirectionalLight(u32 index, const vec3& direction, const vec3& color, f32 intensity, const vec3& groundAmbientColor, f32 groundAmbientIntensity, const vec3& skyAmbientColor, f32 skyAmbientIntensity);

private:
    void CreatePermanentResources();

    void SyncToGPU();

    struct DirectionalLight
    {
        vec4 direction;
        vec4 color; // a = intensity
        vec4 groundAmbientColor; // a = intensity
        vec4 skyAmbientColor; // a = intensity
    };

private:
    Renderer::Renderer* _renderer;

    Renderer::DescriptorSet _preEffectsPassDescriptorSet;
    Renderer::DescriptorSet _materialPassDescriptorSet;

    Renderer::GPUVector<DirectionalLight> _directionalLights;

    Renderer::SamplerID _sampler;

    TerrainRenderer* _terrainRenderer = nullptr;
    ModelRenderer* _modelRenderer = nullptr;
};
