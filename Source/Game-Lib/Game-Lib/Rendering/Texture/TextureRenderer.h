#pragma once
#include "Game-Lib/ECS/Components/UI/Widget.h"

#include <Base/Types.h>
#include <Base/Container/ConcurrentQueue.h>

#include <Renderer/Descriptors/TextureDesc.h>
#include <Renderer/DescriptorSet.h>
#include <Renderer/GPUVector.h>

#include <robinhood/robinhood.h>

namespace Renderer
{
    class RenderGraph;
    class Renderer;
}

struct RenderResources;
class Window;
class DebugRenderer;

class TextureRenderer
{
public:
    TextureRenderer(Renderer::Renderer* renderer, DebugRenderer* debugRenderer);
    void Clear();

    void Update(f32 deltaTime);

    void AddTexturePass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);

    // Takes a textureID, creates a new renderable texture and queues up a copy operation so it will contain the same data
    // TextureID is immediately useable
    Renderer::TextureID MakeRenderableCopy(Renderer::TextureID texture);

    // Requests a render operation draw a region of a texture to a region of another texture
    // The region is specified in UV
    void RequestRenderTextureToTexture(Renderer::TextureID dst, const vec2& dstRectMin, const vec2& dstRectMax, Renderer::TextureID src, const vec2& srcRectMin, const vec2& srcRectMax);

private:
    void CreatePermanentResources();

    void RenderTextureToTexture(Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, u32 frameIndex, Renderer::DescriptorSetResource& descriptorSet, Renderer::TextureID dst, const vec2& dstRectMin, const vec2& dstRectMax, Renderer::TextureID src, const vec2& srcRectMin, const vec2& srcRectMax);
    void ResolveMips(Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, u32 frameIndex, Renderer::DescriptorSetResource& descriptorSet, Renderer::TextureID textureID);

    Renderer::GraphicsPipelineID GetPipelineForFormat(Renderer::ImageFormat format);
    Renderer::GraphicsPipelineID CreatePipeline(Renderer::ImageFormat format);

private:
    struct CopyTextureToTextureRequest
    {
        Renderer::TextureID dst;
        Renderer::TextureID src;
    };

    struct RenderTextureToTextureRequest
    {
        Renderer::TextureID dst;
        vec2 dstRectMin;
        vec2 dstRectMax;
        Renderer::TextureID src;
        vec2 srcRectMin;
        vec2 srcRectMax;
    };

private:
    Renderer::Renderer* _renderer;
    DebugRenderer* _debugRenderer;

    robin_hood::unordered_map<Renderer::ImageFormat, Renderer::GraphicsPipelineID> _pipelines;

    Renderer::ComputePipelineID _mipDownsamplerPipeline;
    Renderer::BufferID _mipAtomicBuffer;
    Renderer::SamplerID _mipResolveSampler;
    
    Renderer::DescriptorSet _descriptorSet;
    Renderer::DescriptorSet _mipResolveDescriptorSet;

    moodycamel::ConcurrentQueue<RenderTextureToTextureRequest> _renderTextureToTextureRequests;
    std::vector<RenderTextureToTextureRequest> _renderTextureToTextureWork;

    robin_hood::unordered_set<Renderer::TextureID::type> _texturesNeedingMipResolve;

    Renderer::TextureID _debugTexture;
    const bool _debugEveryFrame = true;
};