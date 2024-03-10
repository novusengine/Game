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

    // Reset the counters
    params.commandList->FillBuffer(params.drawCountBuffer, 0, sizeof(u32) * Renderer::Settings::MAX_VIEWS, 0);
    params.commandList->FillBuffer(params.triangleCountBuffer, 0, sizeof(u32) * Renderer::Settings::MAX_VIEWS, 0);

    params.commandList->BufferBarrier(params.drawCountBuffer, Renderer::BufferPassUsage::TRANSFER);
    params.commandList->BufferBarrier(params.triangleCountBuffer, Renderer::BufferPassUsage::TRANSFER);

    const u32 numDrawCalls = params.cullingResources->GetDrawCallsSize();
    u32 numInstances = params.cullingResources->GetNumInstances();

    if (numDrawCalls == 0 || numInstances == 0)
        return;

    if (params.useInstancedCulling)
    {
        const bool debugOrdered = true;

        params.commandList->FillBuffer(params.culledInstanceCountsBuffer, 0, sizeof(u32) * numDrawCalls, 0);
        params.commandList->BufferBarrier(params.culledInstanceCountsBuffer, Renderer::BufferPassUsage::TRANSFER);

        Renderer::BufferID culledDrawCallsBitMaskBuffer = params.cullingResources->GetCulledDrawCallsBitMaskBuffer(!params.frameIndex);
        if (params.disableTwoStepCulling)
        {
            params.commandList->FillBuffer(params.culledDrawCallsBitMaskBuffer, 0, RenderUtils::CalcCullingBitmaskSize(numInstances), 0);
            params.commandList->BufferBarrier(params.culledDrawCallsBitMaskBuffer, Renderer::BufferPassUsage::TRANSFER);
        }

        // Fill the occluders to draw
        {
            params.commandList->PushMarker(params.passName + " Instanced Occlusion Fill", Color::White);

            Renderer::ComputePipelineDesc pipelineDesc;
            params.graphResources->InitializePipelineDesc(pipelineDesc);

            Renderer::ComputeShaderDesc shaderDesc;
            shaderDesc.path = "Utils/FillInstancedDrawCallsFromBitmask.cs.hlsl";
            shaderDesc.AddPermutationField("IS_INDEXED", params.isIndexed ? "1" : "0");

            pipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);

            Renderer::ComputePipelineID pipeline = _renderer->CreatePipeline(pipelineDesc);
            params.commandList->BeginPipeline(pipeline);

            struct FillDrawCallConstants
            {
                u32 numTotalInstances;
                u32 baseInstanceLookupOffset; // Byte offset into drawCallDatas where the baseInstanceLookup is stored
                u32 drawCallDataSize;
            };

            FillDrawCallConstants* fillConstants = params.graphResources->FrameNew<FillDrawCallConstants>();
            fillConstants->numTotalInstances = numInstances;
            fillConstants->baseInstanceLookupOffset = params.baseInstanceLookupOffset;
            fillConstants->drawCallDataSize = params.drawCallDataSize;

            params.commandList->PushConstant(fillConstants, 0, sizeof(FillDrawCallConstants));

            params.occluderFillDescriptorSet.Bind("_culledDrawCallsBitMask"_h, params.culledDrawCallsBitMaskBuffer);

            // Bind descriptorset
            //params.commandList->BindDescriptorSet(Renderer::DescriptorSetSlot::DEBUG, &params.renderResources->debugDescriptorSet, frameIndex);
            params.commandList->BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, params.globalDescriptorSet, params.frameIndex);
            //params.commandList->BindDescriptorSet(Renderer::DescriptorSetSlot::SHADOWS, &params.renderResources->shadowDescriptorSet, frameIndex);
            params.commandList->BindDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS, params.occluderFillDescriptorSet, params.frameIndex);

            params.commandList->Dispatch((numInstances + 31) / 32, 1, 1);

            params.commandList->EndPipeline(pipeline);

            params.commandList->PopMarker();
        }

        params.commandList->FillBuffer(params.culledDrawCallCountBuffer, 0, sizeof(u32), 0);
        params.commandList->BufferBarrier(params.culledDrawCallCountBuffer, Renderer::BufferPassUsage::TRANSFER);

        // Create indirect argument buffer
        {
            params.commandList->PushMarker("Create Indirect", Color::Yellow);

            Renderer::ComputePipelineDesc cullingPipelineDesc;
            params.graphResources->InitializePipelineDesc(cullingPipelineDesc);

            Renderer::ComputeShaderDesc shaderDesc;
            shaderDesc.path = "Utils/CreateIndirectAfterCulling.cs.hlsl";
            shaderDesc.AddPermutationField("IS_INDEXED", params.cullingResources->IsIndexed() ? "1" : "0");
            shaderDesc.AddPermutationField("DEBUG_ORDERED", debugOrdered ? "1" : "0");

            cullingPipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);

            Renderer::ComputePipelineID pipeline = _renderer->CreatePipeline(cullingPipelineDesc);
            params.commandList->BeginPipeline(pipeline);

            struct CullConstants
            {
                u32 numTotalDrawCalls;
                u32 baseInstanceLookupOffset;
                u32 drawCallDataSize;
            };
            CullConstants* cullConstants = params.graphResources->FrameNew<CullConstants>();

            cullConstants->numTotalDrawCalls = numDrawCalls;
            cullConstants->baseInstanceLookupOffset = params.baseInstanceLookupOffset;
            cullConstants->drawCallDataSize = params.drawCallDataSize;
            params.commandList->PushConstant(cullConstants, 0, sizeof(CullConstants));

            //params.commandList->BindDescriptorSet(Renderer::DescriptorSetSlot::DEBUG, params.debugDescriptorSet, params.frameIndex);
            params.commandList->BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, params.globalDescriptorSet, params.frameIndex);
            //params.commandList->BindDescriptorSet(Renderer::DescriptorSetSlot::SHADOWS, &params.renderResources->shadowDescriptorSet, params.frameIndex);
            params.commandList->BindDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS, params.createIndirectDescriptorSet, params.frameIndex);

            if (debugOrdered)
            {
                params.commandList->Dispatch(1, 1, 1);
            }
            else
            {
                params.commandList->Dispatch((numDrawCalls + 31) / 32, 1, 1);
            }

            params.commandList->EndPipeline(pipeline);
            params.commandList->PopMarker();
        }

        params.commandList->BufferBarrier(params.culledDrawCallsBuffer, Renderer::BufferPassUsage::COMPUTE);

        if (params.enableDrawing)
        {
            // Draw Occluders
            params.commandList->PushMarker(params.passName + " Occlusion Draw " + std::to_string(numDrawCalls), Color::White);

            DrawParams drawParams;
            drawParams.cullingEnabled = true; // The occuder pass only makes sense if culling is enabled
            drawParams.shadowPass = false;
            drawParams.globalDescriptorSet = params.globalDescriptorSet;
            drawParams.drawDescriptorSet = params.drawDescriptorSet;
            drawParams.rt0 = params.rt0;
            drawParams.rt1 = params.rt1;
            drawParams.depth = params.depth;
            drawParams.argumentBuffer = params.culledDrawCallsBuffer;
            drawParams.drawCountBuffer = params.culledDrawCallCountBuffer;
            drawParams.drawCountIndex = 0;
            drawParams.numMaxDrawCalls = numDrawCalls;

            params.drawCallback(drawParams);
        }

        params.commandList->BufferBarrier(params.drawCountBuffer, Renderer::BufferPassUsage::COMPUTE);
        params.commandList->BufferBarrier(params.triangleCountBuffer, Renderer::BufferPassUsage::COMPUTE);

        // Copy from our draw count buffer to the readback buffer
        params.commandList->CopyBuffer(params.drawCountReadBackBuffer, 0, params.drawCountBuffer, 0, sizeof(u32)* Renderer::Settings::MAX_VIEWS);
        params.commandList->CopyBuffer(params.triangleCountReadBackBuffer, 0, params.triangleCountBuffer, 0, sizeof(u32)* Renderer::Settings::MAX_VIEWS);

        if (params.enableDrawing)
        {
            params.commandList->PopMarker();
        }
    }
    else
    {
        Renderer::BufferID culledDrawCallsBitMaskBuffer = params.cullingResources->GetCulledDrawCallsBitMaskBuffer(!params.frameIndex);
        if (params.disableTwoStepCulling)
        {
            params.commandList->FillBuffer(params.culledDrawCallsBitMaskBuffer, 0, RenderUtils::CalcCullingBitmaskSize(numDrawCalls), 0);
            params.commandList->BufferBarrier(params.culledDrawCallsBitMaskBuffer, Renderer::BufferPassUsage::TRANSFER);
        }

        // Fill the occluders to draw
        {
            params.commandList->PushMarker(params.passName + " Occlusion Fill", Color::White);

            Renderer::ComputePipelineDesc pipelineDesc;
            params.graphResources->InitializePipelineDesc(pipelineDesc);

            Renderer::ComputeShaderDesc shaderDesc;
            shaderDesc.path = "Utils/FillDrawCallsFromBitmask.cs.hlsl";
            shaderDesc.AddPermutationField("IS_INDEXED", params.isIndexed ? "1" : "0");

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

            params.occluderFillDescriptorSet.Bind("_culledDrawCallsBitMask"_h, params.culledDrawCallsBitMaskBuffer);

            // Bind descriptorset
            //params.commandList->BindDescriptorSet(Renderer::DescriptorSetSlot::DEBUG, &params.renderResources->debugDescriptorSet, frameIndex);
            params.commandList->BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, params.globalDescriptorSet, params.frameIndex);
            //params.commandList->BindDescriptorSet(Renderer::DescriptorSetSlot::SHADOWS, &params.renderResources->shadowDescriptorSet, frameIndex);
            params.commandList->BindDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS, params.occluderFillDescriptorSet, params.frameIndex);

            params.commandList->Dispatch((numDrawCalls + 31) / 32, 1, 1);

            params.commandList->EndPipeline(pipeline);

            params.commandList->PopMarker();
        }

        params.commandList->BufferBarrier(params.culledDrawCallsBuffer, Renderer::BufferPassUsage::COMPUTE);
        params.commandList->BufferBarrier(params.drawCountBuffer, Renderer::BufferPassUsage::COMPUTE);
        params.commandList->BufferBarrier(params.triangleCountBuffer, Renderer::BufferPassUsage::COMPUTE);

        if (params.enableDrawing)
        {
            // Draw Occluders
            params.commandList->PushMarker(params.passName + " Occlusion Draw " + std::to_string(numDrawCalls), Color::White);

            DrawParams drawParams;
            drawParams.cullingEnabled = true; // The occuder pass only makes sense if culling is enabled
            drawParams.shadowPass = false;
            drawParams.globalDescriptorSet = params.globalDescriptorSet;
            drawParams.drawDescriptorSet = params.drawDescriptorSet;
            drawParams.rt0 = params.rt0;
            drawParams.rt1 = params.rt1;
            drawParams.depth = params.depth;
            drawParams.argumentBuffer = params.culledDrawCallsBuffer;
            drawParams.drawCountBuffer = params.drawCountBuffer;
            drawParams.drawCountIndex = 0;
            drawParams.numMaxDrawCalls = numDrawCalls;

            params.drawCallback(drawParams);
        }

        // Copy from our draw count buffer to the readback buffer
        params.commandList->CopyBuffer(params.drawCountReadBackBuffer, 0, params.drawCountBuffer, 0, sizeof(u32) * Renderer::Settings::MAX_VIEWS);
        params.commandList->CopyBuffer(params.triangleCountReadBackBuffer, 0, params.triangleCountBuffer, 0, sizeof(u32) * Renderer::Settings::MAX_VIEWS);

        if (params.enableDrawing)
        {
            params.commandList->PopMarker();
        }
    }
}

void CulledRenderer::CullingPass(CullingPassParams& params)
{
    DebugHandler::Assert(params.drawCallDataSize > 0, "CulledRenderer : CullingPass params provided an invalid drawCallDataSize");

    const u32 numDrawCalls = params.cullingResources->GetDrawCallsSize();
    u32 numInstances = params.cullingResources->GetNumInstances();

    if (numDrawCalls > 0 && numInstances > 0)
    {
        if (params.useInstancedCulling)
        {
            const bool debugOrdered = true;

            params.commandList->PushMarker(params.passName + " Culling", Color::Yellow);

            // Reset the counters
            params.commandList->FillBuffer(params.drawCountBuffer, 0, sizeof(u32) * Renderer::Settings::MAX_VIEWS, 0);
            params.commandList->FillBuffer(params.triangleCountBuffer, 0, sizeof(u32) * Renderer::Settings::MAX_VIEWS, 0);
            params.commandList->FillBuffer(params.culledInstanceCountsBuffer, 0, sizeof(u32) * numDrawCalls, 0);
            params.commandList->FillBuffer(params.culledDrawCallCountBuffer, 0, sizeof(u32), 0);

            params.commandList->BufferBarrier(params.drawCountBuffer, Renderer::BufferPassUsage::TRANSFER);
            params.commandList->BufferBarrier(params.triangleCountBuffer, Renderer::BufferPassUsage::TRANSFER);
            params.commandList->BufferBarrier(params.culledInstanceCountsBuffer, Renderer::BufferPassUsage::TRANSFER);
            params.commandList->BufferBarrier(params.culledDrawCallCountBuffer, Renderer::BufferPassUsage::TRANSFER);

            // Do culling
            {
                params.commandList->PushMarker("Instanced Culling", Color::Yellow);

                Renderer::ComputePipelineDesc cullingPipelineDesc;
                params.graphResources->InitializePipelineDesc(cullingPipelineDesc);

                Renderer::ComputeShaderDesc shaderDesc;
                shaderDesc.path = "Utils/CullingInstanced.cs.hlsl";

                shaderDesc.AddPermutationField("USE_BITMASKS", params.disableTwoStepCulling ? "0" : "1");
                cullingPipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);

                Renderer::ComputePipelineID pipeline = _renderer->CreatePipeline(cullingPipelineDesc);
                params.commandList->BeginPipeline(pipeline);

                struct CullConstants
                {
                    u32 numTotalInstances;
                    u32 occlusionCull;
                    u32 instanceCountOffset; // Byte offset into drawCalls where the instanceCount is stored
                    u32 drawCallSize;
                    u32 baseInstanceLookupOffset; // Byte offset into drawCallDatas where the baseInstanceLookup is stored
                    u32 drawCallDataSize;
                    u32 cullingDataIsWorldspace; // TODO: This controls two things, are both needed? I feel like one counters the other but I'm not sure...
                    u32 debugDrawColliders;
                };
                CullConstants* cullConstants = params.graphResources->FrameNew<CullConstants>();

                cullConstants->numTotalInstances = numInstances;
                cullConstants->occlusionCull = params.occlusionCull;

                u32 instanceCountOffset = params.cullingResources->IsIndexed() ? offsetof(Renderer::IndexedIndirectDraw, Renderer::IndexedIndirectDraw::instanceCount) : offsetof(Renderer::IndirectDraw, Renderer::IndirectDraw::instanceCount);
                cullConstants->instanceCountOffset = instanceCountOffset;
                cullConstants->drawCallSize = params.cullingResources->IsIndexed() ? sizeof(Renderer::IndexedIndirectDraw) : sizeof(Renderer::IndirectDraw);

                cullConstants->baseInstanceLookupOffset = params.baseInstanceLookupOffset;
                cullConstants->drawCallDataSize = params.drawCallDataSize;

                cullConstants->cullingDataIsWorldspace = params.cullingDataIsWorldspace;
                cullConstants->debugDrawColliders = params.debugDrawColliders;
                params.commandList->PushConstant(cullConstants, 0, sizeof(CullConstants));

                params.cullingDescriptorSet.Bind("_depthPyramid"_h, params.depthPyramid);

                if (params.cullingResources->HasSupportForTwoStepCulling())
                {
                    params.cullingDescriptorSet.Bind("_prevCulledDrawCallsBitMask"_h, params.prevCulledDrawCallsBitMask);
                    params.cullingDescriptorSet.Bind("_culledDrawCallsBitMask"_h, params.currentCulledDrawCallsBitMask);
                }

                params.commandList->BindDescriptorSet(Renderer::DescriptorSetSlot::DEBUG, params.debugDescriptorSet, params.frameIndex);
                params.commandList->BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, params.globalDescriptorSet, params.frameIndex);
                //params.commandList->BindDescriptorSet(Renderer::DescriptorSetSlot::SHADOWS, &params.renderResources->shadowDescriptorSet, params.frameIndex);
                params.commandList->BindDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS, params.cullingDescriptorSet, params.frameIndex);

                params.commandList->Dispatch((numInstances + 31) / 32, 1, 1);

                params.commandList->EndPipeline(pipeline);
                params.commandList->PopMarker();
            }

            params.commandList->BufferBarrier(params.culledDrawCallCountBuffer, Renderer::BufferPassUsage::COMPUTE);

            // Create indirect argument buffer
            {
                params.commandList->PushMarker("Create Indirect", Color::Yellow);

                Renderer::ComputePipelineDesc cullingPipelineDesc;
                params.graphResources->InitializePipelineDesc(cullingPipelineDesc);

                Renderer::ComputeShaderDesc shaderDesc;
                shaderDesc.path = "Utils/CreateIndirectAfterCulling.cs.hlsl";
                shaderDesc.AddPermutationField("IS_INDEXED", params.cullingResources->IsIndexed() ? "1" : "0");
                shaderDesc.AddPermutationField("DEBUG_ORDERED", debugOrdered ? "1" : "0");

                cullingPipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);

                Renderer::ComputePipelineID pipeline = _renderer->CreatePipeline(cullingPipelineDesc);
                params.commandList->BeginPipeline(pipeline);

                struct CullConstants
                {
                    u32 numTotalDrawCalls;
                    u32 baseInstanceLookupOffset;
                    u32 drawCallDataSize;
                };
                CullConstants* cullConstants = params.graphResources->FrameNew<CullConstants>();

                cullConstants->numTotalDrawCalls = numDrawCalls;
                cullConstants->baseInstanceLookupOffset = params.baseInstanceLookupOffset;
                cullConstants->drawCallDataSize = params.drawCallDataSize;
                params.commandList->PushConstant(cullConstants, 0, sizeof(CullConstants));

                params.commandList->BindDescriptorSet(Renderer::DescriptorSetSlot::DEBUG, params.debugDescriptorSet, params.frameIndex);
                params.commandList->BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, params.globalDescriptorSet, params.frameIndex);
                //params.commandList->BindDescriptorSet(Renderer::DescriptorSetSlot::SHADOWS, &params.renderResources->shadowDescriptorSet, params.frameIndex);
                params.commandList->BindDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS, params.cullingDescriptorSet, params.frameIndex);

                if (debugOrdered)
                {
                    params.commandList->Dispatch(1, 1, 1);
                }
                else
                {
                    params.commandList->Dispatch((numDrawCalls + 31) / 32, 1, 1);
                }

                params.commandList->EndPipeline(pipeline);
                params.commandList->PopMarker();
            }

            params.commandList->PopMarker();
        }
        else
        {
            params.commandList->PushMarker(params.passName + " Culling", Color::Yellow);

            // Reset the counters
            params.commandList->FillBuffer(params.drawCountBuffer, 0, sizeof(u32) * Renderer::Settings::MAX_VIEWS, 0);
            params.commandList->FillBuffer(params.triangleCountBuffer, 0, sizeof(u32) * Renderer::Settings::MAX_VIEWS, 0);

            params.commandList->BufferBarrier(params.drawCountBuffer, Renderer::BufferPassUsage::TRANSFER);
            params.commandList->BufferBarrier(params.triangleCountBuffer, Renderer::BufferPassUsage::TRANSFER);

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
                u32 modelIDIsDrawCallID;
                u32 cullingDataIsWorldspace;
                u32 debugDrawColliders;
            };
            CullConstants* cullConstants = params.graphResources->FrameNew<CullConstants>();

            cullConstants->maxDrawCount = numDrawCalls;
            cullConstants->numCascades = params.numCascades;
            cullConstants->occlusionCull = params.occlusionCull;
            cullConstants->instanceIDOffset = params.instanceIDOffset;
            cullConstants->modelIDOffset = params.modelIDOffset;
            cullConstants->drawCallDataSize = params.drawCallDataSize;

            cullConstants->modelIDIsDrawCallID = params.modelIDIsDrawCallID;
            cullConstants->cullingDataIsWorldspace = params.cullingDataIsWorldspace;
            cullConstants->debugDrawColliders = params.debugDrawColliders;
            params.commandList->PushConstant(cullConstants, 0, sizeof(CullConstants));

            params.cullingDescriptorSet.Bind("_depthPyramid"_h, params.depthPyramid);

            if (params.cullingResources->HasSupportForTwoStepCulling())
            {
                params.cullingDescriptorSet.Bind("_prevCulledDrawCallsBitMask"_h, params.prevCulledDrawCallsBitMask);
                params.cullingDescriptorSet.Bind("_culledDrawCallsBitMask"_h, params.currentCulledDrawCallsBitMask);
            }

            params.commandList->BindDescriptorSet(Renderer::DescriptorSetSlot::DEBUG, params.debugDescriptorSet, params.frameIndex);
            params.commandList->BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, params.globalDescriptorSet, params.frameIndex);
            //params.commandList->BindDescriptorSet(Renderer::DescriptorSetSlot::SHADOWS, &params.renderResources->shadowDescriptorSet, params.frameIndex);
            params.commandList->BindDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS, params.cullingDescriptorSet, params.frameIndex);

            params.commandList->Dispatch((numDrawCalls + 31) / 32, 1, 1);

            params.commandList->EndPipeline(pipeline);

            params.commandList->PopMarker();
        }
    }
    else
    {
        if (!params.useInstancedCulling)
        {
            // Reset the counter
            params.commandList->FillBuffer(params.drawCountBuffer, 0, sizeof(u32)* Renderer::Settings::MAX_VIEWS, numDrawCalls);
        }
    }
}

void CulledRenderer::GeometryPass(GeometryPassParams& params)
{
    DebugHandler::Assert(params.drawCallback != nullptr, "CulledRenderer : GeometryPass got params with invalid drawCallback");

    const u32 numDrawCalls = params.cullingResources->GetDrawCallsSize();

    if (!params.cullingEnabled)
    {
        // Reset the counters
        params.commandList->FillBuffer(params.drawCountBuffer, 0, sizeof(u32) * Renderer::Settings::MAX_VIEWS, numDrawCalls);
        params.commandList->BufferBarrier(params.drawCountBuffer, Renderer::BufferPassUsage::TRANSFER);
    }

    if (params.enableDrawing)
    {
        params.commandList->PushMarker(params.passName + " " + std::to_string(numDrawCalls), Color::White);

        const u32 debugDrawCallBufferIndex = 0;//CVAR_ComplexModelDebugShadowDraws.Get();

        DrawParams drawParams;
        drawParams.cullingEnabled = params.cullingEnabled;
        drawParams.shadowPass = false;
        drawParams.globalDescriptorSet = params.globalDescriptorSet;
        drawParams.drawDescriptorSet = params.drawDescriptorSet;
        drawParams.rt0 = params.rt0;
        drawParams.rt1 = params.rt1;
        drawParams.depth = params.depth;

        if (params.cullingEnabled)
        {
            if (params.culledDrawCallsBuffer == Renderer::BufferMutableResource::Invalid())
            {
                DebugHandler::PrintFatal("Tried to draw with culling enabled but no culled draw calls buffer was provided");
            }
            drawParams.argumentBuffer = params.culledDrawCallsBuffer;
        }
        else
        {
            if (params.drawCallsBuffer == Renderer::BufferMutableResource::Invalid())
            {
                DebugHandler::PrintFatal("Tried to draw with culling disabled but no draw calls buffer was provided");
            }
            drawParams.argumentBuffer = params.drawCallsBuffer;
        }
        
        if (params.useInstancedCulling)
        {
            drawParams.drawCountBuffer = params.culledDrawCallCountBuffer;
            drawParams.drawCountIndex = debugDrawCallBufferIndex;
            drawParams.numMaxDrawCalls = numDrawCalls;
        }
        else
        {
            drawParams.drawCountBuffer = params.drawCountBuffer;
            drawParams.drawCountIndex = debugDrawCallBufferIndex;
            drawParams.numMaxDrawCalls = numDrawCalls;
        }
        

        params.drawCallback(drawParams);
    }

    // Copy from our draw count buffer to the readback buffer
    params.commandList->CopyBuffer(params.drawCountReadBackBuffer, 0, params.drawCountBuffer, 0, sizeof(u32) * Renderer::Settings::MAX_VIEWS);
    params.commandList->CopyBuffer(params.triangleCountReadBackBuffer, 0, params.triangleCountBuffer, 0, sizeof(u32) * Renderer::Settings::MAX_VIEWS);

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

    _cullingDatas.SetDebugName("CullDataBuffer");
    _cullingDatas.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
}

void CulledRenderer::SyncToGPU()
{
    // Sync CullingData buffer to GPU
    _cullingDatas.SyncToGPU(_renderer);
}