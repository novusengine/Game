#include "CulledRenderer.h"

#include "Game/Rendering/Debug/DebugRenderer.h"
#include "Game/Rendering/RenderUtils.h"


CulledRenderer::CulledRenderer(Renderer::Renderer* renderer, DebugRenderer* debugRenderer)
    : _renderer(renderer)
    , _debugRenderer(debugRenderer)
{
    CreatePermanentResources();
}

CulledRenderer::~CulledRenderer()
{

}

void CulledRenderer::Update(f32 deltaTime)
{

}

void CulledRenderer::Clear()
{

}

void CulledRenderer::OccluderPass(OccluderPassParams& params)
{
    DebugHandler::Assert(params.drawCallback != nullptr, "CulledRenderer : OccluderPass got params with invalid drawCallback");

    Renderer::GPUVector<Renderer::IndexedIndirectDraw>& drawCalls = params.cullingResources->GetDrawCalls();

    Renderer::BufferID drawCountBuffer = params.cullingResources->GetDrawCountBuffer();
    Renderer::BufferID triangleCountBuffer = params.cullingResources->GetTriangleCountBuffer();

    // Reset the counters
    params.commandList->FillBuffer(drawCountBuffer, 0, sizeof(u32) * Renderer::Settings::MAX_VIEWS, 0);
    params.commandList->FillBuffer(triangleCountBuffer, 0, sizeof(u32) * Renderer::Settings::MAX_VIEWS, 0);

    params.commandList->PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToComputeShaderRW, drawCountBuffer);
    params.commandList->PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToTransferDest, drawCountBuffer);
    params.commandList->PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToComputeShaderRW, triangleCountBuffer);

    const u32 numDrawCalls = static_cast<u32>(drawCalls.Size());
    if (numDrawCalls == 0)
        return;

    Renderer::BufferID culledDrawCallsBitMaskBuffer = params.cullingResources->GetCulledDrawCallsBitMaskBuffer(!params.frameIndex);
    if (params.disableTwoStepCulling)
    {
        params.commandList->FillBuffer(culledDrawCallsBitMaskBuffer, 0, RenderUtils::CalcCullingBitmaskSize(numDrawCalls), 0);
        params.commandList->PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToComputeShaderRW, culledDrawCallsBitMaskBuffer);
    }

    // Fill the occluders to draw
    {
        params.commandList->PushMarker(params.passName + " Occlusion Fill", Color::White);

        Renderer::ComputePipelineDesc pipelineDesc;
        params.graphResources->InitializePipelineDesc(pipelineDesc);

        Renderer::ComputeShaderDesc shaderDesc;
        shaderDesc.path = "Utils/FillDrawCallsFromBitmask.cs.hlsl";
        pipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);

        Renderer::ComputePipelineID pipeline = _renderer->CreatePipeline(pipelineDesc);
        params.commandList->BeginPipeline(pipeline);

        struct FillDrawCallConstants
        {
            u32 numTotalDraws;
        };

        FillDrawCallConstants* fillConstants = params.graphResources->FrameNew<FillDrawCallConstants>();
        fillConstants->numTotalDraws = numDrawCalls;
        params.commandList->PushConstant(fillConstants, 0, sizeof(FillDrawCallConstants));

        Renderer::DescriptorSet& occluderFillDescriptorSet = params.cullingResources->GetOccluderFillDescriptorSet();
        occluderFillDescriptorSet.Bind("_culledDrawCallsBitMask"_h, culledDrawCallsBitMaskBuffer);

        // Bind descriptorset
        //params.commandList->BindDescriptorSet(Renderer::DescriptorSetSlot::DEBUG, &params.renderResources->debugDescriptorSet, frameIndex);
        params.commandList->BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, &params.renderResources->globalDescriptorSet, params.frameIndex);
        //params.commandList->BindDescriptorSet(Renderer::DescriptorSetSlot::SHADOWS, &params.renderResources->shadowDescriptorSet, frameIndex);
        params.commandList->BindDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS, &occluderFillDescriptorSet, params.frameIndex);

        params.commandList->Dispatch((numDrawCalls + 31) / 32, 1, 1);

        params.commandList->EndPipeline(pipeline);

        params.commandList->PopMarker();
    }

    for (u32 i = 0; i < Renderer::Settings::MAX_VIEWS; i++)
    {
        params.commandList->PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToIndirectArguments, params.cullingResources->GetCulledDrawsBuffer(i));
    }
    params.commandList->PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToIndirectArguments, drawCountBuffer);

    if (params.enableDrawing)
    {
        // Draw Occluders
        params.commandList->PushMarker(params.passName + " Occlusion Draw " + std::to_string(numDrawCalls), Color::White);

        DrawParams drawParams;
        drawParams.cullingEnabled = true; // The occuder pass only makes sense if culling is enabled
        drawParams.shadowPass = false;
        drawParams.drawDescriptorSet = &params.cullingResources->GetGeometryPassDescriptorSet();
        drawParams.rt0 = params.rt0;
        drawParams.rt1 = params.rt1;
        drawParams.depth = params.depth;
        drawParams.argumentBuffer = params.cullingResources->GetCulledDrawsBuffer(0);
        drawParams.drawCountBuffer = drawCountBuffer;
        drawParams.drawCountIndex = 0;
        drawParams.numMaxDrawCalls = numDrawCalls;

        params.drawCallback(drawParams);
    }

    // Copy from our draw count buffer to the readback buffer
    Renderer::BufferID drawCountReadBackBuffer = params.cullingResources->GetOccluderDrawCountReadBackBuffer();
    Renderer::BufferID triangleCountReadBackBuffer = params.cullingResources->GetOccluderTriangleCountReadBackBuffer();

    params.commandList->PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToTransferSrc, drawCountBuffer);
    params.commandList->CopyBuffer(drawCountReadBackBuffer, 0, drawCountBuffer, 0, sizeof(u32) * Renderer::Settings::MAX_VIEWS);
    params.commandList->PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToTransferSrc, drawCountReadBackBuffer);

    params.commandList->PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToTransferSrc, triangleCountBuffer);
    params.commandList->CopyBuffer(triangleCountReadBackBuffer, 0, triangleCountBuffer, 0, sizeof(u32) * Renderer::Settings::MAX_VIEWS);
    params.commandList->PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToTransferSrc, triangleCountReadBackBuffer);

    if (params.enableDrawing)
    {
        params.commandList->PopMarker();
    }
}

void CulledRenderer::CullingPass(CullingPassParams& params)
{
    DebugHandler::Assert(params.drawCallDataSize > 0, "CulledRenderer : CullingPass params provided an invalid drawCallDataSize");

    const u32 numDrawCalls = static_cast<u32>(params.cullingResources->GetDrawCalls().Size());

    // TODO: Animations
    // clear visible instance counter
    /*if (numDrawCalls > 0)
    {
        params.commandList->PushMarker(params.passName + " Clear instance visibility", Color::Grey);
        params.commandList->FillBuffer(_visibleInstanceCountBuffer, 0, sizeof(u32), 0);
        params.commandList->FillBuffer(_visibleInstanceMaskBuffer, 0, sizeof(u32) * ((numInstances + 31) / 32), 0);
        params.commandList->PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToComputeShaderRW, _visibleInstanceCountBuffer);
        params.commandList->PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToComputeShaderRW, _visibleInstanceMaskBuffer);
        params.commandList->PopMarker();
    }*/

    Renderer::BufferID drawCountBuffer = params.cullingResources->GetDrawCountBuffer();

    if (numDrawCalls > 0)
    {
        params.commandList->PushMarker(params.passName + " Culling", Color::Yellow);

        for (u32 i = 0; i < Renderer::Settings::MAX_VIEWS; i++)
        {
            params.commandList->PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToComputeShaderRead, params.cullingResources->GetCulledDrawsBuffer(i));
        }
        
        Renderer::BufferID triangleCountBuffer = params.cullingResources->GetTriangleCountBuffer();

        // Reset the counters
        params.commandList->FillBuffer(drawCountBuffer, 0, sizeof(u32) * Renderer::Settings::MAX_VIEWS, 0);
        params.commandList->FillBuffer(triangleCountBuffer, 0, sizeof(u32) * Renderer::Settings::MAX_VIEWS, 0);

        params.commandList->PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToComputeShaderRW, drawCountBuffer);
        params.commandList->PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToTransferDest, drawCountBuffer);
        params.commandList->PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToComputeShaderRW, triangleCountBuffer);

        Renderer::ComputePipelineDesc cullingPipelineDesc;
        params.graphResources->InitializePipelineDesc(cullingPipelineDesc);

        Renderer::ComputeShaderDesc shaderDesc;
        shaderDesc.path = "Utils/Culling.cs.hlsl";
        shaderDesc.AddPermutationField("USE_BITMASKS", params.disableTwoStepCulling ? "0" : "1");
        cullingPipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);

        // Do culling
        Renderer::ComputePipelineID pipeline = _renderer->CreatePipeline(cullingPipelineDesc);
        params.commandList->BeginPipeline(pipeline);

        struct CullConstants
        {
            u32 maxDrawCount;
            u32 numCascades;
            u32 occlusionCull;
            u32 instanceIDOffset;
            u32 modelIDOffset;
            u32 drawCallDataSize;
            bool debugDrawColliders;
        };
        CullConstants* cullConstants = params.graphResources->FrameNew<CullConstants>();

        cullConstants->maxDrawCount = numDrawCalls;
        cullConstants->numCascades = params.numCascades;
        cullConstants->occlusionCull = params.occlusionCull;
        cullConstants->instanceIDOffset = params.instanceIDOffset;
        cullConstants->modelIDOffset = params.modelIDOffset;
        cullConstants->drawCallDataSize = params.drawCallDataSize;
        cullConstants->debugDrawColliders = params.debugDrawColliders;
        params.commandList->PushConstant(cullConstants, 0, sizeof(CullConstants));

        Renderer::DescriptorSet& cullingDescriptorSet = params.cullingResources->GetCullingDescriptorSet();
        cullingDescriptorSet.Bind("_depthPyramid"_h, params.renderResources->depthPyramid);

        if (params.cullingResources->HasSupportForTwoStepCulling())
        {
            cullingDescriptorSet.Bind("_prevCulledDrawCallsBitMask"_h, params.cullingResources->GetCulledDrawCallsBitMaskBuffer(!params.frameIndex));
            cullingDescriptorSet.Bind("_culledDrawCallsBitMask"_h, params.cullingResources->GetCulledDrawCallsBitMaskBuffer(params.frameIndex));
        }

        params.commandList->BindDescriptorSet(Renderer::DescriptorSetSlot::DEBUG, &_debugRenderer->GetDebugDescriptorSet(), params.frameIndex);
        params.commandList->BindDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS, &cullingDescriptorSet, params.frameIndex);
        params.commandList->BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, &params.renderResources->globalDescriptorSet, params.frameIndex);
        //params.commandList->BindDescriptorSet(Renderer::DescriptorSetSlot::SHADOWS, &params.renderResources->shadowDescriptorSet, params.frameIndex);

        params.commandList->Dispatch((numDrawCalls + 31) / 32, 1, 1);

        params.commandList->EndPipeline(pipeline);

        params.commandList->PopMarker();
    }
    else
    {
        // Reset the counter
        params.commandList->FillBuffer(drawCountBuffer, 0, sizeof(u32) * Renderer::Settings::MAX_VIEWS, numDrawCalls);
        params.commandList->PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToIndirectArguments, drawCountBuffer);
        params.commandList->PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToTransferDest, drawCountBuffer);
    }
}

void CulledRenderer::GeometryPass(GeometryPassParams& params)
{
    DebugHandler::Assert(params.drawCallback != nullptr, "CulledRenderer : GeometryPass got params with invalid drawCallback");

    const u32 numDrawCalls = static_cast<u32>(params.cullingResources->GetDrawCalls().Size());

    Renderer::BufferID drawCountBuffer = params.cullingResources->GetDrawCountBuffer();
    
    if (params.cullingEnabled)
    {
        for (u32 i = 0; i < Renderer::Settings::MAX_VIEWS; i++)
        {
            params.commandList->PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToIndirectArguments, params.cullingResources->GetCulledDrawsBuffer(i));//_opaqueCulledDrawCallBuffer[i]);
        }
        params.commandList->PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToIndirectArguments, drawCountBuffer);
    }
    else
    {
        // Reset the counters
        params.commandList->FillBuffer(drawCountBuffer, 0, sizeof(u32) * Renderer::Settings::MAX_VIEWS, numDrawCalls);
        params.commandList->PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToIndirectArguments, drawCountBuffer);
        params.commandList->PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToTransferDest, drawCountBuffer);
    }

    if (params.enableDrawing)
    {
        params.commandList->PushMarker(params.passName + " " + std::to_string(numDrawCalls), Color::White);

        const u32 debugDrawCallBufferIndex = 0;//CVAR_ComplexModelDebugShadowDraws.Get();

        DrawParams drawParams;
        drawParams.cullingEnabled = params.cullingEnabled;
        drawParams.shadowPass = false;
        drawParams.drawDescriptorSet = &params.cullingResources->GetGeometryPassDescriptorSet();
        drawParams.rt0 = params.rt0;
        drawParams.rt1 = params.rt1;
        drawParams.depth = params.depth;
        drawParams.argumentBuffer = (params.cullingEnabled) ? params.cullingResources->GetCulledDrawsBuffer(debugDrawCallBufferIndex) : params.cullingResources->GetDrawCalls().GetBuffer();
        drawParams.drawCountBuffer = drawCountBuffer;
        drawParams.drawCountIndex = debugDrawCallBufferIndex;
        drawParams.numMaxDrawCalls = numDrawCalls;

        params.drawCallback(drawParams);
    }

    // Copy from our draw count buffer to the readback buffer
    Renderer::BufferID triangleCountBuffer = params.cullingResources->GetTriangleCountBuffer();
    Renderer::BufferID drawCountReadBackBuffer = params.cullingResources->GetDrawCountReadBackBuffer();
    Renderer::BufferID triangleCountReadBackBuffer = params.cullingResources->GetTriangleCountReadBackBuffer();

    params.commandList->PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToTransferSrc, drawCountBuffer);
    params.commandList->CopyBuffer(drawCountReadBackBuffer, 0, drawCountBuffer, 0, sizeof(u32) * Renderer::Settings::MAX_VIEWS);
    params.commandList->PipelineBarrier(Renderer::PipelineBarrierType::TransferDestToTransferSrc, drawCountReadBackBuffer);

    params.commandList->PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToTransferSrc, triangleCountBuffer);
    params.commandList->CopyBuffer(triangleCountReadBackBuffer, 0, triangleCountBuffer, 0, sizeof(u32) * Renderer::Settings::MAX_VIEWS);
    params.commandList->PipelineBarrier(Renderer::PipelineBarrierType::ComputeWriteToTransferSrc, triangleCountReadBackBuffer);

    if (params.enableDrawing)
    {
        params.commandList->PopMarker();
    }
}

void CulledRenderer::SetupCullingResource(CullingResourcesBase& resources)
{
    resources.GetCullingDescriptorSet().Bind("_depthSampler"_h, _occlusionSampler);
    resources.GetCullingDescriptorSet().Bind("_cullingDatas"_h, _cullingDatas.GetBuffer());
}

void CulledRenderer::CreatePermanentResources()
{
    Renderer::SamplerDesc occlusionSamplerDesc;
    occlusionSamplerDesc.filter = Renderer::SamplerFilter::MINIMUM_MIN_MAG_MIP_LINEAR;

    occlusionSamplerDesc.addressU = Renderer::TextureAddressMode::CLAMP;
    occlusionSamplerDesc.addressV = Renderer::TextureAddressMode::CLAMP;
    occlusionSamplerDesc.addressW = Renderer::TextureAddressMode::CLAMP;
    occlusionSamplerDesc.minLOD = 0.f;
    occlusionSamplerDesc.maxLOD = 16.f;
    occlusionSamplerDesc.mode = Renderer::SamplerReductionMode::MIN;

    _occlusionSampler = _renderer->CreateSampler(occlusionSamplerDesc);

    _cullingDatas.SetDebugName("ModelCullDataBuffer");
    _cullingDatas.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
}

void CulledRenderer::SyncToGPU()
{
    // Sync CullingData buffer to GPU
    _cullingDatas.SyncToGPU(_renderer);
}