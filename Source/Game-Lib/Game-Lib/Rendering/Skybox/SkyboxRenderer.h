#pragma once
#include <Base/Types.h>

namespace Renderer
{
    class Renderer;
    class RenderGraph;
}

class DebugRenderer;
struct RenderResources;

class SkyboxRenderer
{
public:
    struct SkybandColors
    {
    public:
        vec4 top = vec4(0.00f, 0.12f, 0.29f, 0.0f);
        vec4 middle = vec4(0.23f, 0.64f, 0.81f, 0.0f);
        vec4 bottom = vec4(0.60f, 0.86f, 0.96f, 0.0f);
        vec4 aboveHorizon = vec4(0.69f, 0.85f, 0.88f, 0.0f);
        vec4 horizon = vec4(0.71f, 0.71f, 0.71f, 0.0f);
    };

public:
    SkyboxRenderer(Renderer::Renderer* renderer, DebugRenderer* debugRenderer);
    ~SkyboxRenderer();

    void Update(f32 deltaTime);

    void AddSkyboxPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);

    void SetSkybandColors(const vec3& skyTopColor, const vec3& skyMiddleColor, const vec3& skyBottomColor, const vec3& skyAboveHorizonColor, const vec3& skyHorizonColor);

private:
    void CreatePermanentResources();

    Renderer::Renderer* _renderer;
    //DebugRenderer* _debugRenderer;

    SkybandColors _skybandColors;
};
