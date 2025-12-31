#include "CulledRenderer.h"

#include "Game-Lib/Rendering/Debug/DebugRenderer.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/RenderUtils.h"

bool CulledRenderer::_pipelinesCreated = false;
Renderer::ComputePipelineID CulledRenderer::_fillInstancedDrawCallsFromBitmaskPipeline[2]; // [0] = non-indexed, [1] = indexed
Renderer::ComputePipelineID CulledRenderer::_fillDrawCallsFromBitmaskPipeline[2]; // [0] = non-indexed, [1] = indexed
Renderer::ComputePipelineID CulledRenderer::_createIndirectAfterCullingPipeline[2]; // [0] = non-indexed, [1] = indexed
Renderer::ComputePipelineID CulledRenderer::_createIndirectAfterCullingOrderedPipeline[2]; // [0] = non-indexed, [1] = indexed
Renderer::ComputePipelineID CulledRenderer::_cullingInstancedPipeline[2]; // [0] = no bitmasks, [1] = use bitmasks
Renderer::ComputePipelineID CulledRenderer::_cullingPipeline[2]; // [0] = no bitmasks, [1] = use bitmasks

void CulledRenderer::InitCullingResources(CullingResourcesBase& cullingResources)
{
    bool isIndexed = cullingResources.IsIndexed();
    bool isInstanced = cullingResources.IsInstanced();
    bool supportsTwoPassCulling = cullingResources.HasSupportForTwoStepCulling();

    Renderer::ComputePipelineID fillPipeline = (isInstanced) ? _fillInstancedDrawCallsFromBitmaskPipeline[isIndexed] : _fillDrawCallsFromBitmaskPipeline[isIndexed];
    Renderer::ComputePipelineID cullingPipeline = (isInstanced) ? _cullingInstancedPipeline[supportsTwoPassCulling] : _cullingPipeline[supportsTwoPassCulling];

    // Init descriptor sets
    Renderer::DescriptorSet& occluderFillDescriptorSet = cullingResources.GetOccluderFillDescriptorSet();
    occluderFillDescriptorSet.RegisterPipeline(_renderer, fillPipeline);
    occluderFillDescriptorSet.Init(_renderer);

    Renderer::DescriptorSet& cullingDescriptorSet = cullingResources.GetCullingDescriptorSet();
    cullingDescriptorSet.RegisterPipeline(_renderer, cullingPipeline);
    cullingDescriptorSet.Init(_renderer);

    Renderer::DescriptorSet& createIndirectAfterCullDescriptorSet = cullingResources.GetCreateIndirectAfterCullDescriptorSet();
    createIndirectAfterCullDescriptorSet.RegisterPipeline(_renderer, _createIndirectAfterCullingPipeline[isIndexed]);
    createIndirectAfterCullDescriptorSet.Init(_renderer);

    Renderer::DescriptorSet& geometryFillDescriptorSet = cullingResources.GetGeometryFillDescriptorSet();
    geometryFillDescriptorSet.RegisterPipeline(_renderer, fillPipeline);
    geometryFillDescriptorSet.Init(_renderer);
}

CulledRenderer::CulledRenderer(Renderer::Renderer* renderer, GameRenderer* gameRenderer, DebugRenderer* debugRenderer)
    : _renderer(renderer)
    , _gameRenderer(gameRenderer)
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
    NC_ASSERT(params.drawCallback != nullptr, "CulledRenderer : OccluderPass got params with invalid drawCallback");

    const u32 numDrawCalls = params.cullingResources->GetDrawCallCount();
    u32 numInstances = params.cullingResources->GetNumInstances();

    if (numDrawCalls == 0 || numInstances == 0)
    {
        // Reset the counters
        params.commandList->FillBuffer(params.drawCountBuffer, 0, sizeof(u32), 0);
        params.commandList->FillBuffer(params.triangleCountBuffer, 0, sizeof(u32), 0);

        params.commandList->BufferBarrier(params.drawCountBuffer, Renderer::BufferPassUsage::TRANSFER);
        params.commandList->BufferBarrier(params.triangleCountBuffer, Renderer::BufferPassUsage::TRANSFER);

        return;
    }
    
    if (params.cullingResources->IsInstanced())
    {
        const bool debugOrdered = false;

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
            std::string debugName = params.passName + " Instanced Occlusion Fill";
            params.commandList->PushMarker(debugName, Color::White);

            Renderer::ComputePipelineID pipeline = _fillInstancedDrawCallsFromBitmaskPipeline[params.cullingResources->IsIndexed()];
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
            params.commandList->BindDescriptorSet(params.globalDescriptorSet, params.frameIndex);
            params.commandList->BindDescriptorSet(params.occluderFillDescriptorSet, params.frameIndex);

            params.commandList->Dispatch((numInstances + 31) / 32, 1, 1);

            params.commandList->EndPipeline(pipeline);

            params.commandList->PopMarker();
        }

        params.commandList->FillBuffer(params.culledDrawCallCountBuffer, 0, sizeof(u32), 0);
        params.commandList->BufferBarrier(params.culledDrawCallCountBuffer, Renderer::BufferPassUsage::TRANSFER);

        // Create indirect argument buffer
        {
            std::string debugName = params.passName + " Create Indirect";
            params.commandList->PushMarker(debugName, Color::Yellow);

            Renderer::ComputePipelineDesc cullingPipelineDesc;
            cullingPipelineDesc.debugName = debugName;
            params.graphResources->InitializePipelineDesc(cullingPipelineDesc);

            Renderer::ComputePipelineID pipeline;
            if (debugOrdered)
            {
                pipeline = _createIndirectAfterCullingOrderedPipeline[params.cullingResources->IsIndexed()];
            }
            else
            {
                pipeline = _createIndirectAfterCullingPipeline[params.cullingResources->IsIndexed()];
            }
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

            params.commandList->BindDescriptorSet(params.globalDescriptorSet, params.frameIndex);
            params.commandList->BindDescriptorSet(params.createIndirectDescriptorSet, params.frameIndex);

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
            drawParams.viewIndex = 0;
            drawParams.rt0 = params.rt0;
            drawParams.rt1 = params.rt1;
            drawParams.depth = params.depth[0];
            drawParams.argumentBuffer = params.culledDrawCallsBuffer;
            drawParams.drawCountBuffer = params.culledDrawCallCountBuffer;
            drawParams.drawCountIndex = 0;
            drawParams.numMaxDrawCalls = numDrawCalls;

            params.drawCallback(drawParams);
        }

        params.commandList->BufferBarrier(params.drawCountBuffer, Renderer::BufferPassUsage::COMPUTE);
        params.commandList->BufferBarrier(params.triangleCountBuffer, Renderer::BufferPassUsage::COMPUTE);

        // Copy from our draw count buffer to the readback buffer
        params.commandList->CopyBuffer(params.drawCountReadBackBuffer, 0, params.drawCountBuffer, 0, sizeof(u32));
        params.commandList->CopyBuffer(params.triangleCountReadBackBuffer, 0, params.triangleCountBuffer, 0, sizeof(u32));

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
            u32 sizePerView = params.cullingResources->GetBitMaskBufferSizePerView();

            params.commandList->FillBuffer(params.culledDrawCallsBitMaskBuffer, 0, sizePerView * Renderer::Settings::MAX_VIEWS, 0);
            params.commandList->BufferBarrier(params.culledDrawCallsBitMaskBuffer, Renderer::BufferPassUsage::TRANSFER);
        }

        for (u32 i = 0; i < params.numCascades + 1; i++)
        {
            std::string markerName = (i == 0) ? "Main" : "Cascade " + std::to_string(i - 1);
            params.commandList->PushMarker(markerName, Color::PastelYellow);

            // Reset the counters
            {
                params.commandList->BufferBarrier(params.drawCountBuffer, Renderer::BufferPassUsage::TRANSFER);
                params.commandList->BufferBarrier(params.triangleCountBuffer, Renderer::BufferPassUsage::TRANSFER);

                // Reset the counters
                params.commandList->FillBuffer(params.drawCountBuffer, 0, sizeof(u32), 0);
                params.commandList->FillBuffer(params.triangleCountBuffer, 0, sizeof(u32), 0);

                params.commandList->BufferBarrier(params.drawCountBuffer, Renderer::BufferPassUsage::TRANSFER);
                params.commandList->BufferBarrier(params.triangleCountBuffer, Renderer::BufferPassUsage::TRANSFER);
            }

            if (i == 1)
            {
                uvec2 shadowDepthDimensions = params.graphResources->GetImageDimensions(params.depth[1]);

                params.commandList->SetViewport(0, 0, static_cast<f32>(shadowDepthDimensions.x), static_cast<f32>(shadowDepthDimensions.y), 0.0f, 1.0f);
                params.commandList->SetScissorRect(0, shadowDepthDimensions.x, 0, shadowDepthDimensions.y);

                params.commandList->SetDepthBias(params.biasConstantFactor, params.biasClamp, params.biasSlopeFactor);
            }

            // Fill the occluders to draw
            {
                std::string debugName = params.passName + " Occlusion Fill";
                params.commandList->PushMarker(debugName, Color::White);

                Renderer::ComputePipelineID pipeline = _fillDrawCallsFromBitmaskPipeline[params.cullingResources->IsIndexed()];
                params.commandList->BeginPipeline(pipeline);

                struct FillDrawCallConstants
                {
                    u32 numTotalDraws;
                    u32 bitmaskOffset;
                    u32 diffAgainstPrev;
                };

                FillDrawCallConstants* fillConstants = params.graphResources->FrameNew<FillDrawCallConstants>();
                fillConstants->numTotalDraws = numDrawCalls;
                fillConstants->bitmaskOffset = i * params.cullingResources->GetBitMaskBufferUintsPerView();
                fillConstants->diffAgainstPrev = 0; // Occluders should not diff against prev
                params.commandList->PushConstant(fillConstants, 0, sizeof(FillDrawCallConstants));

                params.occluderFillDescriptorSet.Bind("_culledDrawCallsBitMask"_h, params.culledDrawCallsBitMaskBuffer);
                params.occluderFillDescriptorSet.Bind("_prevCulledDrawCallsBitMask"_h, params.prevCulledDrawCallsBitMaskBuffer);

                // Bind descriptorset
                params.commandList->BindDescriptorSet(params.globalDescriptorSet, params.frameIndex);
                params.commandList->BindDescriptorSet(params.occluderFillDescriptorSet, params.frameIndex);

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
                drawParams.shadowPass = i > 0;
                drawParams.viewIndex = i;
                drawParams.rt0 = params.rt0;
                drawParams.rt1 = params.rt1;
                drawParams.depth = params.depth[i];
                drawParams.argumentBuffer = params.culledDrawCallsBuffer;
                drawParams.drawCountBuffer = params.drawCountBuffer;
                drawParams.drawCountIndex = 0;
                drawParams.numMaxDrawCalls = numDrawCalls;

                params.drawCallback(drawParams);
            }

            // Copy from our draw count buffer to the readback buffer
            params.commandList->CopyBuffer(params.drawCountReadBackBuffer, sizeof(u32) * i, params.drawCountBuffer, 0, sizeof(u32));
            params.commandList->CopyBuffer(params.triangleCountReadBackBuffer, sizeof(u32) * i, params.triangleCountBuffer, 0, sizeof(u32));

            if (params.enableDrawing)
            {
                params.commandList->PopMarker();
            }

            params.commandList->PopMarker();
        }

        // Finish by resetting the viewport, scissor and depth bias
        vec2 renderSize = _renderer->GetRenderSize();
        params.commandList->SetViewport(0, 0, renderSize.x, renderSize.y, 0.0f, 1.0f);
        params.commandList->SetScissorRect(0, static_cast<u32>(renderSize.x), 0, static_cast<u32>(renderSize.y));
        params.commandList->SetDepthBias(0, 0, 0);
    }
}

void CulledRenderer::CullingPass(CullingPassParams& params)
{
    NC_ASSERT(params.drawCallDataSize > 0, "CulledRenderer : CullingPass params provided an invalid drawCallDataSize");

    const u32 numDrawCalls = params.cullingResources->GetDrawCallCount();
    u32 numInstances = params.cullingResources->GetNumInstances();

    if (numDrawCalls > 0 && numInstances > 0)
    {
        if (params.cullingResources->IsInstanced())
        {
            const bool debugOrdered = false;

            params.commandList->PushMarker(params.passName + " Culling", Color::Yellow);

            // Reset the counters
            params.commandList->FillBuffer(params.drawCountBuffer, 0, sizeof(u32), 0);
            params.commandList->FillBuffer(params.triangleCountBuffer, 0, sizeof(u32), 0);
            params.commandList->FillBuffer(params.culledInstanceCountsBuffer, 0, sizeof(u32) * numDrawCalls, 0);
            params.commandList->FillBuffer(params.culledDrawCallCountBuffer, 0, sizeof(u32), 0);

            params.commandList->BufferBarrier(params.drawCountBuffer, Renderer::BufferPassUsage::TRANSFER);
            params.commandList->BufferBarrier(params.triangleCountBuffer, Renderer::BufferPassUsage::TRANSFER);
            params.commandList->BufferBarrier(params.culledInstanceCountsBuffer, Renderer::BufferPassUsage::TRANSFER);
            params.commandList->BufferBarrier(params.culledDrawCallCountBuffer, Renderer::BufferPassUsage::TRANSFER);

            // Do culling
            {
                std::string debugName = params.passName + " Instanced Culling";
                params.commandList->PushMarker(debugName, Color::Yellow);

                Renderer::ComputePipelineID pipeline = _cullingInstancedPipeline[!params.disableTwoStepCulling];
                params.commandList->BeginPipeline(pipeline);

                vec2 viewportSize = _renderer->GetRenderSize();

                struct CullConstants
                {
                    u32 viewportSizeX;
                    u32 viewportSizeY;
                    u32 numTotalInstances;
                    u32 occlusionCull;
                    u32 instanceCountOffset; // Byte offset into drawCalls where the instanceCount is stored
                    u32 drawCallSize;
                    u32 baseInstanceLookupOffset; // Byte offset into drawCallDatas where the baseInstanceLookup is stored
                    u32 modelIDOffset; // Byte offset into drawCallDatas where the modelID is stored
                    u32 drawCallDataSize;
                    u32 cullingDataIsWorldspace; // TODO: This controls two things, are both needed? I feel like one counters the other but I'm not sure...
                    u32 debugDrawColliders;
                };
                CullConstants* cullConstants = params.graphResources->FrameNew<CullConstants>();
                cullConstants->viewportSizeX = u32(viewportSize.x);
                cullConstants->viewportSizeY = u32(viewportSize.y);
                cullConstants->numTotalInstances = numInstances;
                cullConstants->occlusionCull = params.occlusionCull;

                u32 instanceCountOffset = params.cullingResources->IsIndexed() ? offsetof(Renderer::IndexedIndirectDraw, Renderer::IndexedIndirectDraw::instanceCount) : offsetof(Renderer::IndirectDraw, Renderer::IndirectDraw::instanceCount);
                cullConstants->instanceCountOffset = instanceCountOffset;
                cullConstants->drawCallSize = params.cullingResources->IsIndexed() ? sizeof(Renderer::IndexedIndirectDraw) : sizeof(Renderer::IndirectDraw);

                cullConstants->baseInstanceLookupOffset = params.baseInstanceLookupOffset;
                cullConstants->modelIDOffset = params.modelIDOffset;
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

                params.commandList->BindDescriptorSet(params.debugDescriptorSet, params.frameIndex);
                params.commandList->BindDescriptorSet(params.globalDescriptorSet, params.frameIndex);
                params.commandList->BindDescriptorSet(params.cullingDescriptorSet, params.frameIndex);

                params.commandList->Dispatch((numInstances + 31) / 32, 1, 1);

                params.commandList->EndPipeline(pipeline);
                params.commandList->PopMarker();
            }

            params.commandList->BufferBarrier(params.culledDrawCallCountBuffer, Renderer::BufferPassUsage::COMPUTE);

            // Create indirect argument buffer
            {
                std::string debugName = params.passName + " Create Indirect";
                params.commandList->PushMarker(debugName, Color::Yellow);


                Renderer::ComputePipelineID pipeline;
                if (debugOrdered)
                {
                    pipeline = _createIndirectAfterCullingOrderedPipeline[params.cullingResources->IsIndexed()];
                }
                else
                {
                    pipeline = _createIndirectAfterCullingPipeline[params.cullingResources->IsIndexed()];
                }
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

                params.commandList->BindDescriptorSet(params.debugDescriptorSet, params.frameIndex);
                params.commandList->BindDescriptorSet(params.globalDescriptorSet, params.frameIndex);
                params.commandList->BindDescriptorSet(params.createIndirectAfterCullSet, params.frameIndex);

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
            std::string debugName = params.passName + " Culling";
            params.commandList->PushMarker(debugName, Color::Yellow);

            // Reset the counters
            params.commandList->FillBuffer(params.drawCountBuffer, 0, sizeof(u32), 0);
            params.commandList->FillBuffer(params.triangleCountBuffer, 0, sizeof(u32), 0);

            params.commandList->BufferBarrier(params.drawCountBuffer, Renderer::BufferPassUsage::TRANSFER);
            params.commandList->BufferBarrier(params.triangleCountBuffer, Renderer::BufferPassUsage::TRANSFER);

            // Do culling
            Renderer::ComputePipelineID pipeline = _cullingPipeline[!params.disableTwoStepCulling];
            params.commandList->BeginPipeline(pipeline);

            vec2 viewportSize = _renderer->GetRenderSize();

            struct CullConstants
            {
                u32 viewportSizeX;
                u32 viewportSizeY;
                u32 maxDrawCount;
                u32 numCascades;
                u32 occlusionCull;
                u32 instanceIDOffset;
                u32 modelIDOffset;
                u32 drawCallDataSize;
                u32 modelIDIsDrawCallID;
                u32 cullingDataIsWorldspace;
                u32 debugDrawColliders;
                u32 bitMaskBufferUintsPerView;
            };
            CullConstants* cullConstants = params.graphResources->FrameNew<CullConstants>();
            cullConstants->viewportSizeX = u32(viewportSize.x);
            cullConstants->viewportSizeY = u32(viewportSize.y);
            cullConstants->maxDrawCount = numDrawCalls;
            cullConstants->numCascades = params.numCascades;
            cullConstants->occlusionCull = params.occlusionCull;
            cullConstants->instanceIDOffset = params.instanceIDOffset;
            cullConstants->modelIDOffset = params.modelIDOffset;
            cullConstants->drawCallDataSize = params.drawCallDataSize;

            cullConstants->modelIDIsDrawCallID = params.modelIDIsDrawCallID;
            cullConstants->cullingDataIsWorldspace = params.cullingDataIsWorldspace;
            cullConstants->debugDrawColliders = params.debugDrawColliders;
            cullConstants->bitMaskBufferUintsPerView = params.cullingResources->GetBitMaskBufferUintsPerView();
            params.commandList->PushConstant(cullConstants, 0, sizeof(CullConstants));

            params.cullingDescriptorSet.Bind("_depthPyramid"_h, params.depthPyramid);

            if (params.cullingResources->HasSupportForTwoStepCulling())
            {
                params.cullingDescriptorSet.Bind("_prevCulledDrawCallsBitMask"_h, params.prevCulledDrawCallsBitMask);
                params.cullingDescriptorSet.Bind("_culledDrawCallsBitMask"_h, params.currentCulledDrawCallsBitMask);
            }

            params.commandList->BindDescriptorSet(params.debugDescriptorSet, params.frameIndex);
            params.commandList->BindDescriptorSet(params.globalDescriptorSet, params.frameIndex);
            params.commandList->BindDescriptorSet(params.cullingDescriptorSet, params.frameIndex);

            params.commandList->Dispatch((numDrawCalls + 31) / 32, 1, 1);

            params.commandList->EndPipeline(pipeline);

            params.commandList->PopMarker();
        }
    }
    else if (numDrawCalls > 0)
    {
        if (params.cullingResources->IsInstanced())
        {
            // Reset the counters
            params.commandList->FillBuffer(params.drawCountBuffer, 0, sizeof(u32), 0);
            params.commandList->FillBuffer(params.triangleCountBuffer, 0, sizeof(u32), 0);
            params.commandList->FillBuffer(params.culledInstanceCountsBuffer, 0, sizeof(u32) * numDrawCalls, 0);
            params.commandList->FillBuffer(params.culledDrawCallCountBuffer, 0, sizeof(u32), 0);
        }
        else
        {
            // Reset the counter
            params.commandList->FillBuffer(params.drawCountBuffer, 0, sizeof(u32), numDrawCalls);
            params.commandList->FillBuffer(params.triangleCountBuffer, 0, sizeof(u32), 0);
        }
    }
}

void CulledRenderer::GeometryPass(GeometryPassParams& params)
{
    NC_ASSERT(params.drawCallback != nullptr, "CulledRenderer : GeometryPass got params with invalid drawCallback");

    const u32 numDrawCalls = params.cullingResources->GetDrawCallCount();

    for (u32 i = 0; i < params.numCascades + 1; i++)
    {
        std::string markerName = (i == 0) ? "Main" : "Cascade " + std::to_string(i - 1);
        params.commandList->PushMarker(markerName, Color::PastelYellow);

        // Reset the counters
        if (!params.cullingResources->IsInstanced() && params.cullingResources->HasSupportForTwoStepCulling())
        {
            params.commandList->BufferBarrier(params.drawCountBuffer, Renderer::BufferPassUsage::TRANSFER);
            params.commandList->FillBuffer(params.drawCountBuffer, 0, sizeof(u32), 0);
            params.commandList->BufferBarrier(params.drawCountBuffer, Renderer::BufferPassUsage::TRANSFER);
        
            if (i == 1)
            {
                uvec2 shadowDepthDimensions = params.graphResources->GetImageDimensions(params.depth[1]);

                params.commandList->SetViewport(0, 0, static_cast<f32>(shadowDepthDimensions.x), static_cast<f32>(shadowDepthDimensions.y), 0.0f, 1.0f);
                params.commandList->SetScissorRect(0, shadowDepthDimensions.x, 0, shadowDepthDimensions.y);

                params.commandList->SetDepthBias(params.biasConstantFactor, params.biasClamp, params.biasSlopeFactor);
            }

            // Fill the geometry to draw
            {
                std::string debugName = params.passName + " Geometry Fill";
                params.commandList->PushMarker(debugName, Color::White);

                Renderer::ComputePipelineID pipeline = _fillDrawCallsFromBitmaskPipeline[params.cullingResources->IsIndexed()];
                params.commandList->BeginPipeline(pipeline);

                struct FillDrawCallConstants
                {
                    u32 numTotalDraws;
                    u32 bitmaskOffset;
                    u32 diffAgainstPrev;
                };

                FillDrawCallConstants* fillConstants = params.graphResources->FrameNew<FillDrawCallConstants>();
                fillConstants->numTotalDraws = numDrawCalls;
                fillConstants->bitmaskOffset = i * params.cullingResources->GetBitMaskBufferUintsPerView();
                fillConstants->diffAgainstPrev = 1; // Geomeyry should diff against prev
                params.commandList->PushConstant(fillConstants, 0, sizeof(FillDrawCallConstants));

                params.fillDescriptorSet.Bind("_culledDrawCallsBitMask"_h, params.culledDrawCallsBitMaskBuffer);
                params.fillDescriptorSet.Bind("_prevCulledDrawCallsBitMask"_h, params.prevCulledDrawCallsBitMaskBuffer);

                // Bind descriptorset
                params.commandList->BindDescriptorSet(params.globalDescriptorSet, params.frameIndex);
                params.commandList->BindDescriptorSet(params.fillDescriptorSet, params.frameIndex);

                params.commandList->Dispatch((numDrawCalls + 31) / 32, 1, 1);

                params.commandList->EndPipeline(pipeline);

                params.commandList->PopMarker();
            }

            params.commandList->BufferBarrier(params.culledDrawCallsBuffer, Renderer::BufferPassUsage::COMPUTE);
            params.commandList->BufferBarrier(params.drawCountBuffer, Renderer::BufferPassUsage::COMPUTE);
            params.commandList->BufferBarrier(params.triangleCountBuffer, Renderer::BufferPassUsage::COMPUTE);
        }

        if (!params.cullingEnabled)
        {
            // Override drawcount to numDrawCalls to draw everything
            params.commandList->BufferBarrier(params.drawCountBuffer, Renderer::BufferPassUsage::TRANSFER);
            params.commandList->FillBuffer(params.drawCountBuffer, 0, sizeof(u32), numDrawCalls);
            params.commandList->BufferBarrier(params.drawCountBuffer, Renderer::BufferPassUsage::TRANSFER);
        }

        if (params.enableDrawing)
        {
            params.commandList->PushMarker(params.passName + " " + std::to_string(numDrawCalls), Color::White);

            const u32 debugDrawCallBufferIndex = 0;//CVAR_ComplexModelDebugShadowDraws.Get();

            DrawParams drawParams;
            drawParams.cullingEnabled = params.cullingEnabled;
            drawParams.shadowPass = i > 0;
            drawParams.viewIndex = i;
            drawParams.cullingResources = params.cullingResources;
            drawParams.rt0 = params.rt0;
            drawParams.rt1 = params.rt1;
            drawParams.depth = params.depth[i];

            if (params.cullingEnabled)
            {
                if (params.culledDrawCallsBuffer == Renderer::BufferMutableResource::Invalid())
                {
                    NC_LOG_CRITICAL("Tried to draw with culling enabled but no culled draw calls buffer was provided");
                }
                drawParams.argumentBuffer = params.culledDrawCallsBuffer;
            }
            else
            {
                if (params.drawCallsBuffer == Renderer::BufferMutableResource::Invalid())
                {
                    NC_LOG_CRITICAL("Tried to draw with culling disabled but no draw calls buffer was provided");
                }
                drawParams.argumentBuffer = params.drawCallsBuffer;
            }

            if (params.cullingResources->IsInstanced())
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
        params.commandList->CopyBuffer(params.drawCountReadBackBuffer, sizeof(u32) * i, params.drawCountBuffer, 0, sizeof(u32));
        params.commandList->CopyBuffer(params.triangleCountReadBackBuffer, sizeof(u32) * i, params.triangleCountBuffer, 0, sizeof(u32));

        if (params.enableDrawing)
        {
            params.commandList->PopMarker();
        }

        params.commandList->PopMarker();
    }

    // Finish by resetting the viewport, scissor and depth bias
    vec2 renderSize = _renderer->GetRenderSize();
    params.commandList->SetViewport(0, 0, renderSize.x, renderSize.y, 0.0f, 1.0f);
    params.commandList->SetScissorRect(0, static_cast<u32>(renderSize.x), 0, static_cast<u32>(renderSize.y));
    params.commandList->SetDepthBias(0, 0, 0);
}

void CulledRenderer::BindCullingResource(CullingResourcesBase& resources)
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

    // Create pipelines
    if (!_pipelinesCreated)
    {
        _pipelinesCreated = true;
        CreatePipelines();
    }
}

void CulledRenderer::CreatePipelines()
{
    // Fill Drawcalls From Bitmask pipelines
    Renderer::ComputePipelineDesc pipelineDesc;
    {
        pipelineDesc.debugName = "FillInstancedDrawcallsFromBitmask";

        for (u32 i = 0; i < 2; i++)
        {
            std::vector<Renderer::PermutationField> permutationFields =
            {
                { "IS_INDEXED", std::to_string(i) }
            };
            u32 shaderEntryNameHash = Renderer::GetShaderEntryNameHash("Utils/FillInstancedDrawCallsFromBitmask.cs", permutationFields);

            Renderer::ComputeShaderDesc shaderDesc;
            shaderDesc.shaderEntry = shaderDesc.shaderEntry = _gameRenderer->GetShaderEntry(shaderEntryNameHash, "Utils/FillInstancedDrawCallsFromBitmask.cs");

            pipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);

            _fillInstancedDrawCallsFromBitmaskPipeline[i] = _renderer->CreatePipeline(pipelineDesc);
        }
    }
    {
        pipelineDesc.debugName = "FillDrawCallsFromBitmask";

        for (u32 i = 0; i < 2; i++)
        {
            std::vector<Renderer::PermutationField> permutationFields =
            {
                { "IS_INDEXED", std::to_string(i) }
            };
            u32 shaderEntryNameHash = Renderer::GetShaderEntryNameHash("Utils/FillDrawCallsFromBitmask.cs", permutationFields);

            Renderer::ComputeShaderDesc shaderDesc;
            shaderDesc.shaderEntry = shaderDesc.shaderEntry = _gameRenderer->GetShaderEntry(shaderEntryNameHash, "Utils/FillDrawCallsFromBitmask.cs");

            pipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);

            _fillDrawCallsFromBitmaskPipeline[i] = _renderer->CreatePipeline(pipelineDesc);
        }
    }
    // Create Indirect After Culling pipelines
    {
        pipelineDesc.debugName = "CreateIndirectAfterCulling";

        for (u32 i = 0; i < 2; i++)
        {
            std::vector<Renderer::PermutationField> permutationFields =
            {
                { "IS_INDEXED", std::to_string(i) },
                { "DEBUG_ORDERED", "0" }
            };
            u32 shaderEntryNameHash = Renderer::GetShaderEntryNameHash("Utils/CreateIndirectAfterCulling.cs", permutationFields);

            Renderer::ComputeShaderDesc shaderDesc;
            shaderDesc.shaderEntry = shaderDesc.shaderEntry = _gameRenderer->GetShaderEntry(shaderEntryNameHash, "Utils/CreateIndirectAfterCulling.cs");

            pipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);

            _createIndirectAfterCullingPipeline[i] = _renderer->CreatePipeline(pipelineDesc);
        }
    }
    {
        pipelineDesc.debugName = "CreateIndirectAfterCullingOrdered";

        for (u32 i = 0; i < 2; i++)
        {
            std::vector<Renderer::PermutationField> permutationFields =
            {
                { "IS_INDEXED", std::to_string(i) },
                { "DEBUG_ORDERED", "1" }
            };
            u32 shaderEntryNameHash = Renderer::GetShaderEntryNameHash("Utils/CreateIndirectAfterCulling.cs", permutationFields);

            Renderer::ComputeShaderDesc shaderDesc;
            shaderDesc.shaderEntry = shaderDesc.shaderEntry = _gameRenderer->GetShaderEntry(shaderEntryNameHash, "Utils/CreateIndirectAfterCulling.cs");

            pipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);

            _createIndirectAfterCullingOrderedPipeline[i] = _renderer->CreatePipeline(pipelineDesc);
        }
    }
    // Culling Instanced Pipelines
    {
        pipelineDesc.debugName = "CullingInstanced";

        for (u32 i = 0; i < 2; i++)
        {
            std::vector<Renderer::PermutationField> permutationFields =
            {
                { "USE_BITMASKS", std::to_string(i) }
            };
            u32 shaderEntryNameHash = Renderer::GetShaderEntryNameHash("Utils/CullingInstanced.cs", permutationFields);

            Renderer::ComputeShaderDesc shaderDesc;
            shaderDesc.shaderEntry = shaderDesc.shaderEntry = _gameRenderer->GetShaderEntry(shaderEntryNameHash, "Utils/CullingInstanced.cs");

            pipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);

            _cullingInstancedPipeline[i] = _renderer->CreatePipeline(pipelineDesc);
        }
    }
    // Culling Pipelines
    {
        pipelineDesc.debugName = "Culling";

        for (u32 i = 0; i < 2; i++)
        {
            std::vector<Renderer::PermutationField> permutationFields =
            {
                { "USE_BITMASKS", std::to_string(i) }
            };
            u32 shaderEntryNameHash = Renderer::GetShaderEntryNameHash("Utils/Culling.cs", permutationFields);

            Renderer::ComputeShaderDesc shaderDesc;
            shaderDesc.shaderEntry = shaderDesc.shaderEntry = _gameRenderer->GetShaderEntry(shaderEntryNameHash, "Utils/Culling.cs");

            pipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);

            _cullingPipeline[i] = _renderer->CreatePipeline(pipelineDesc);
        }
    }
}

void CulledRenderer::SyncToGPU()
{
    // Sync CullingData buffer to GPU
    _cullingDatas.SyncToGPU(_renderer);
}