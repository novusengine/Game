#pragma once
#include "Game-Lib/Rendering/Camera.h"

#include <Base/Types.h>
#include <Renderer/RenderSettings.h>
#include <Renderer/GPUVector.h>
#include <Renderer/Buffer.h>
#include <Renderer/DescriptorSet.h>

#include <Renderer/Descriptors/ComputePipelineDesc.h>
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

    void AddDepthMinMaxPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
    void AddCascadeFitPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
    void AddShadowPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);

    // Reduced scene depth bounds as view distances, false while no valid depth has been reduced yet
    bool GetDepthBoundsViewDistances(const RenderResources& resources, f32& outMinDistance, f32& outMaxDistance) const;

    // Effective cascade range after hysteresis and quantization, false while SDSM has not fitted yet
    bool GetEffectiveShadowRange(f32& outMinDistance, f32& outMaxDistance) const;

    // Fitted light-space extents per cascade, false while the XY reduction is not running
    bool GetCascadeFittedBounds(u32 cascadeIndex, vec3& outExtents, bool& outValid) const;

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

    Renderer::ComputePipelineID _depthMinMaxPipeline;
    Renderer::DescriptorSet _depthMinMaxDescriptorSet;
    Renderer::BufferID _depthMinMaxBuffer;
    Renderer::BufferID _depthMinMaxReadBackBuffer;
    u32 _depthMinMaxReadBack[2] = { 0xFFFFFFFF, 0 };

    static constexpr u32 SDSM_DATA_FLOAT_COUNT = 8 + 8 + 8 + 4 * Renderer::Settings::MAX_SHADOW_CASCADES * 2; // SDSMState + splitDist + splitDepth + cascadeStable + cascadeDiag

    Renderer::ComputePipelineID _cascadeFitRangePipeline;
    Renderer::ComputePipelineID _cascadeXYReducePipeline;
    Renderer::ComputePipelineID _cascadeFitCamerasPipeline;
    Renderer::DescriptorSet _cascadeFitRangeDescriptorSet;
    Renderer::DescriptorSet _cascadeXYReduceDescriptorSet;
    Renderer::DescriptorSet _cascadeFitCamerasDescriptorSet;
    Renderer::BufferID _sdsmDataBuffer;
    Renderer::BufferID _cascadeBoundsBuffer;
    Renderer::BufferID _sdsmDataReadBackBuffer;
    Renderer::BufferID _cascadeCamerasReadBackBuffer;
    Camera _readBackCascadeCameras[Renderer::Settings::MAX_SHADOW_CASCADES];
    f32 _sdsmDataReadBack[SDSM_DATA_FLOAT_COUNT] = { 0.0f };

    f32 _lastDeltaTime = 0.0f;
};