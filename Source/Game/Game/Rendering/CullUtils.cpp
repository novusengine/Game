#include "CullUtils.h"

#define A_CPU
#include "DownSampler/ffx_a.h"
#include "DownSampler/ffx_spd.h"

#include <Renderer/Renderer.h>
#include <Renderer/RenderGraphResources.h>
#include <Renderer/CommandList.h>

#include <Renderer/Descriptors/ComputeShaderDesc.h>
#include <Renderer/Descriptors/ComputePipelineDesc.h>

Renderer::SamplerID DepthPyramidUtils::_copySampler;
Renderer::SamplerID DepthPyramidUtils::_pyramidSampler;
Renderer::DescriptorSet DepthPyramidUtils::_copyDescriptorSet;
Renderer::DescriptorSet DepthPyramidUtils::_pyramidDescriptorSet;
Renderer::BufferID DepthPyramidUtils::_atomicBuffer;

inline u32 GetGroupCount(u32 threadCount, u32 localSize)
{
    return (threadCount + localSize - 1) / localSize;
}

void DepthPyramidUtils::InitBuffers(Renderer::Renderer* renderer)
{
    Renderer::BufferDesc desc;
    desc.name = "DepthPyramidAtomicCounters";
    desc.size = sizeof(u32) * 6;
    desc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;

    _atomicBuffer = renderer->CreateAndFillBuffer(_atomicBuffer, desc, [](void* mappedMemory, size_t size)
        {
            memset(mappedMemory, 0, size);
        });
    _pyramidDescriptorSet.Bind("spdGlobalAtomic", _atomicBuffer);

    Renderer::SamplerDesc copySamplerDesc;
    copySamplerDesc.filter = Renderer::SamplerFilter::MINIMUM_MIN_MAG_MIP_LINEAR;
    copySamplerDesc.addressU = Renderer::TextureAddressMode::CLAMP;
    copySamplerDesc.addressV = Renderer::TextureAddressMode::CLAMP;
    copySamplerDesc.addressW = Renderer::TextureAddressMode::CLAMP;
    copySamplerDesc.minLOD = 0.f;
    copySamplerDesc.maxLOD = 16.f;
    copySamplerDesc.mode = Renderer::SamplerReductionMode::MIN;

    _copySampler = renderer->CreateSampler(copySamplerDesc);
    _copyDescriptorSet.Bind("_sampler", _copySampler);

    Renderer::SamplerDesc pyramidSamplerDesc;
    pyramidSamplerDesc.filter = /*Renderer::SamplerFilter::MIN_MAG_LINEAR_MIP_POINT;//*/Renderer::SamplerFilter::MINIMUM_MIN_MAG_MIP_LINEAR;
    pyramidSamplerDesc.addressU = Renderer::TextureAddressMode::CLAMP;
    pyramidSamplerDesc.addressV = Renderer::TextureAddressMode::CLAMP;
    pyramidSamplerDesc.addressW = Renderer::TextureAddressMode::CLAMP;
    pyramidSamplerDesc.minLOD = 0.f;
    pyramidSamplerDesc.maxLOD = 16.f;
    pyramidSamplerDesc.mode = Renderer::SamplerReductionMode::MIN;

    _pyramidSampler = renderer->CreateSampler(pyramidSamplerDesc);
    _pyramidDescriptorSet.Bind("srcSampler", _pyramidSampler);
}

void DepthPyramidUtils::BuildPyramid(Renderer::Renderer* renderer, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, RenderResources& resources, u32 frameIndex)
{
    Renderer::ComputePipelineDesc queryPipelineDesc;
    graphResources.InitializePipelineDesc(queryPipelineDesc);

    Renderer::ComputeShaderDesc shaderDesc;
    shaderDesc.path = "Blitting/blitDepth.cs.hlsl";
    queryPipelineDesc.computeShader = renderer->LoadShader(shaderDesc);

    // Do culling
    Renderer::ComputePipelineID pipeline = renderer->CreatePipeline(queryPipelineDesc);
    commandList.BeginPipeline(pipeline);

    commandList.PushMarker("Depth Pyramid ", Color::White);

    Renderer::ImageDesc pyramidInfo = renderer->GetImageDesc(resources.depthPyramid);
    Renderer::DepthImageDesc depthInfo = renderer->GetDepthImageDesc(resources.depth);
    uvec2 pyramidSize = renderer->GetImageDimension(resources.depthPyramid, 0);

    Renderer::SamplerDesc samplerDesc;
    samplerDesc.filter = Renderer::SamplerFilter::MINIMUM_MIN_MAG_MIP_LINEAR;
    samplerDesc.addressU = Renderer::TextureAddressMode::CLAMP;
    samplerDesc.addressV = Renderer::TextureAddressMode::CLAMP;
    samplerDesc.addressW = Renderer::TextureAddressMode::CLAMP;
    samplerDesc.minLOD = 0.f;
    samplerDesc.maxLOD = 16.f;
    samplerDesc.mode = Renderer::SamplerReductionMode::MIN;

    Renderer::SamplerID occlusionSampler = renderer->CreateSampler(samplerDesc);
    _pyramidDescriptorSet.Bind("_sampler", occlusionSampler);

    for (uint32_t i = 0; i < pyramidInfo.mipLevels; ++i)
    {

        _pyramidDescriptorSet.BindStorage("_target", resources.depthPyramid, i);

        if (i == 0)
        {
            _pyramidDescriptorSet.Bind("_source", resources.depth);
        }
        else
        {
            _pyramidDescriptorSet.Bind("_source", resources.depthPyramid, i - 1);
        }

        u32 levelWidth = pyramidSize.x >> i;
        u32 levelHeight = pyramidSize.y >> i;
        if (levelHeight < 1) levelHeight = 1;
        if (levelWidth < 1) levelWidth = 1;

        struct DepthReduceParams
        {
            glm::vec2 imageSize;
            u32 level;
            u32 dummy;
        };

        DepthReduceParams* reduceData = graphResources.FrameNew<DepthReduceParams>();
        reduceData->imageSize = glm::vec2(levelWidth, levelHeight);
        reduceData->level = i;

        commandList.PushConstant(reduceData, 0, sizeof(DepthReduceParams));

        commandList.BindDescriptorSet(Renderer::GLOBAL, &_pyramidDescriptorSet, frameIndex);
        commandList.Dispatch(GetGroupCount(levelWidth, 32), GetGroupCount(levelHeight, 32), 1);

        commandList.ImageBarrier(resources.depthPyramid);
    }

    commandList.EndPipeline(pipeline);
    commandList.PopMarker();
}

void DepthPyramidUtils::BuildPyramid2(Renderer::Renderer* renderer, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, RenderResources& resources, u32 frameIndex)
{
    commandList.PushMarker("Depth Pyramid ", Color::White);

    // Copy first mip
    {
        Renderer::ComputePipelineDesc blitPipelineDesc;
        graphResources.InitializePipelineDesc(blitPipelineDesc);

        Renderer::ComputeShaderDesc shaderDesc;
        shaderDesc.path = "Blitting/blitDepth.cs.hlsl";
        blitPipelineDesc.computeShader = renderer->LoadShader(shaderDesc);

        Renderer::ComputePipelineID pipeline = renderer->CreatePipeline(blitPipelineDesc);
        commandList.BeginPipeline(pipeline);

        _copyDescriptorSet.Bind("_source", resources.depth);
        _copyDescriptorSet.BindStorage("_target", resources.depthPyramid, 0);

        uvec2 pyramidSize = renderer->GetImageDimension(resources.depthPyramid, 0);

        struct CopyParams
        {
            glm::vec2 imageSize;
            u32 level;
            u32 dummy;
        };

        CopyParams* copyData = graphResources.FrameNew<CopyParams>();
        copyData->imageSize = glm::vec2(pyramidSize);
        copyData->level = 0;

        commandList.PushConstant(copyData, 0, sizeof(CopyParams));

        commandList.BindDescriptorSet(Renderer::GLOBAL, &_copyDescriptorSet, frameIndex);
        commandList.Dispatch(GetGroupCount(pyramidSize.x, 32), GetGroupCount(pyramidSize.y, 32), 1);

        commandList.EndPipeline(pipeline);
    }

    commandList.ImageBarrier(resources.depthPyramid);

    // Downsample
    {
        Renderer::ComputePipelineDesc pipelineDesc;
        graphResources.InitializePipelineDesc(pipelineDesc);

        Renderer::ComputeShaderDesc shaderDesc;
        shaderDesc.path = "DownSampler/SinglePassDownsampler.cs.hlsl";
        pipelineDesc.computeShader = renderer->LoadShader(shaderDesc);

        Renderer::ComputePipelineID pipeline = renderer->CreatePipeline(pipelineDesc);
        commandList.BeginPipeline(pipeline);

        _pyramidDescriptorSet.Bind("imgSrc", resources.depthPyramid, 0);
        _pyramidDescriptorSet.BindStorage("imgDst", resources.depthPyramid, 1, 12);
        _pyramidDescriptorSet.BindStorage("imgDst5", resources.depthPyramid, 6);

        uvec2 inputSize = renderer->GetImageDimension(resources.depthPyramid);

        varAU2(dispatchThreadGroupCountXY);
        varAU2(workGroupOffset);
        varAU2(numWorkGroupsAndMips);
        varAU4(rectInfo) = initAU4(0, 0, inputSize.x, inputSize.y); // left, top, width, height
        SpdSetup(dispatchThreadGroupCountXY, workGroupOffset, numWorkGroupsAndMips, rectInfo);

        struct Constants
        {
            u32 mips;
            u32 numWorkGroups;
            uvec2 workGroupOffset;
            vec2 invInputSize;
        };

        Constants* constants = graphResources.FrameNew<Constants>();
        constants->numWorkGroups = numWorkGroupsAndMips[0];
        constants->mips = numWorkGroupsAndMips[1];
        constants->workGroupOffset[0] = workGroupOffset[0];
        constants->workGroupOffset[1] = workGroupOffset[1];
        constants->invInputSize[0] = 1.0f / static_cast<f32>(inputSize.x);
        constants->invInputSize[1] = 1.0f / static_cast<f32>(inputSize.y);

        commandList.PushConstant(constants, 0, sizeof(Constants));

        commandList.BindDescriptorSet(Renderer::PER_PASS, &_pyramidDescriptorSet, frameIndex);

        const Renderer::ImageDesc& pyramidDesc = renderer->GetImageDesc(resources.depthPyramid);
        commandList.Dispatch(dispatchThreadGroupCountXY[0], dispatchThreadGroupCountXY[1], 1);

        commandList.EndPipeline(pipeline);
    }

    commandList.ImageBarrier(resources.depthPyramid);

    commandList.PopMarker();
}