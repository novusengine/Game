#include "TerrainRenderer.h"
#include "Game/Rendering/GameRenderer.h"
#include "Game/Rendering/RenderResources.h"
#include "Game/Rendering/Debug/DebugRenderer.h"
#include "Game/Util/ServiceLocator.h"

#include <Base/Util/Timer.h>
#include <Base/CVarSystem/CVarSystem.h>
#include <Input/InputManager.h>
#include <Renderer/Renderer.h>
#include <Renderer/RenderGraph.h>
#include <Renderer/Descriptors/ImageDesc.h>

#include <imgui/imgui.h>

AutoCVar_Int CVAR_TerrainRendererEnabled("terrainRenderer.enabled", "enable terrainrendering", 0, CVarFlags::EditCheckbox);

TerrainRenderer::TerrainRenderer(Renderer::Renderer* renderer, DebugRenderer* debugRenderer)
    : _renderer(renderer)
    , _debugRenderer(debugRenderer)
{
    CreatePermanentResources();

    // Gotta keep these here to make sure they're not unused...
    _renderer->GetGPUName();
    _debugRenderer->UnProject(vec3(0, 0, 0), mat4x4(1.0f));
}

TerrainRenderer::~TerrainRenderer()
{

}

void TerrainRenderer::Update(f32 deltaTime)
{
    ZoneScoped;

    if (!CVAR_TerrainRendererEnabled.Get())
        return;

    SyncToGPU();
}

void TerrainRenderer::Clear()
{
    ZoneScoped;

}

void TerrainRenderer::AddCullingPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    ZoneScoped;

    if (!CVAR_TerrainRendererEnabled.Get())
        return;
}

void TerrainRenderer::AddGeometryPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    ZoneScoped;
    
    if (!CVAR_TerrainRendererEnabled.Get())
        return;
}

void TerrainRenderer::CreatePermanentResources()
{
    ZoneScoped;

}

void TerrainRenderer::SyncToGPU()
{
    
}
