#include "CullUtils.h"

#define A_CPU
#include "Downsampler/ffx_a.h"
#include "Downsampler/ffx_spd.h"

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

void DepthPyramidUtils::BuildPyramid(BuildPyramidParams& params)
{
    params.commandList->PushMarker("Depth Pyramid ", Color::White);

    // Copy first mip
    {
        Renderer::ComputePipelineDesc blitPipelineDesc;
        blitPipelineDesc.debugName = "Blit Depthpyramid";
        params.graphResources->InitializePipelineDesc(blitPipelineDesc);

        Renderer::ComputeShaderDesc shaderDesc;
        shaderDesc.path = "Blitting/blitDepth.cs.hlsl";
        blitPipelineDesc.computeShader = params.renderer->LoadShader(shaderDesc);

        Renderer::ComputePipelineID pipeline = params.renderer->CreatePipeline(blitPipelineDesc);
        params.commandList->BeginPipeline(pipeline);

        params.copyDescriptorSet.Bind("_source", params.depth);
        params.copyDescriptorSet.BindStorage("_target", params.depthPyramid, 0);

        struct CopyParams
        {
            glm::vec2 imageSize;
            u32 level;
            u32 dummy;
        };

        CopyParams* copyData = params.graphResources->FrameNew<CopyParams>();
        copyData->imageSize = glm::vec2(params.pyramidSize);
        copyData->level = 0;

        params.commandList->PushConstant(copyData, 0, sizeof(CopyParams));

        params.commandList->BindDescriptorSet(Renderer::GLOBAL, params.copyDescriptorSet, params.frameIndex);
        params.commandList->Dispatch(GetGroupCount(params.pyramidSize.x, 32), GetGroupCount(params.pyramidSize.y, 32), 1);

        params.commandList->EndPipeline(pipeline);
    }

    params.commandList->ImageBarrier(params.depthPyramid);

    // Downsample
    {
        Renderer::ComputePipelineDesc pipelineDesc;
        pipelineDesc.debugName = "Downsample Depthpyramid";
        params.graphResources->InitializePipelineDesc(pipelineDesc);

        Renderer::ComputeShaderDesc shaderDesc;
        shaderDesc.path = "DownSampler/SinglePassDownsampler.cs.hlsl";
        pipelineDesc.computeShader = params.renderer->LoadShader(shaderDesc);

        Renderer::ComputePipelineID pipeline = params.renderer->CreatePipeline(pipelineDesc);
        params.commandList->BeginPipeline(pipeline);

        params.pyramidDescriptorSet.Bind("imgSrc", params.depthPyramid, 0);
        params.pyramidDescriptorSet.BindStorage("imgDst", params.depthPyramid, 1, 12);
        params.pyramidDescriptorSet.BindStorage("imgDst5", params.depthPyramid, 6);

        varAU2(dispatchThreadGroupCountXY);
        varAU2(workGroupOffset);
        varAU2(numWorkGroupsAndMips);
        varAU4(rectInfo) = initAU4(0, 0, params.pyramidSize.x, params.pyramidSize.y); // left, top, width, height
        SpdSetup(dispatchThreadGroupCountXY, workGroupOffset, numWorkGroupsAndMips, rectInfo);

        struct Constants
        {
            u32 mips;
            u32 numWorkGroups;
            uvec2 workGroupOffset;
            vec2 invInputSize;
        };

        Constants* constants = params.graphResources->FrameNew<Constants>();
        constants->numWorkGroups = numWorkGroupsAndMips[0];
        constants->mips = numWorkGroupsAndMips[1];
        constants->workGroupOffset[0] = workGroupOffset[0];
        constants->workGroupOffset[1] = workGroupOffset[1];
        constants->invInputSize[0] = 1.0f / static_cast<f32>(params.pyramidSize.x);
        constants->invInputSize[1] = 1.0f / static_cast<f32>(params.pyramidSize.y);

        params.commandList->PushConstant(constants, 0, sizeof(Constants));

        params.commandList->BindDescriptorSet(Renderer::PER_PASS, params.pyramidDescriptorSet, params.frameIndex);

        params.commandList->Dispatch(dispatchThreadGroupCountXY[0], dispatchThreadGroupCountXY[1], 1);

        params.commandList->EndPipeline(pipeline);
    }

    params.commandList->PopMarker();
}
