#include "CulledRenderer.h"

#include "Game-Lib/Rendering/Debug/DebugRenderer.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/RenderUtils.h"

#include <Base/CVarSystem/CVarSystem.h>

AutoCVar_Int CVAR_ShadowDebugCullingView(CVarCategory::Client | CVarCategory::Rendering, "shadowDebugCullingView", "debug draw culling results for a view as AABBs (green = kept, red = culled), 0 = main view, 1..N = clipmaps, -1 = off", -1);

bool CulledRenderer::_pipelinesCreated = false;
Renderer::ComputePipelineID CulledRenderer::_fillInstancedDrawCallsFromBitmaskPipeline[2]; // [0] = non-indexed, [1] = indexed
Renderer::ComputePipelineID CulledRenderer::_fillInstancedDrawCallsFilteredPipeline[2]; // Same, with the SVSM dynamic-mask filter
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
    if (isInstanced)
    {
        // SVSM caster-split fills, only ever dispatched for resources that bind the dynamic mask
        geometryFillDescriptorSet.RegisterPipeline(_renderer, _fillInstancedDrawCallsFilteredPipeline[isIndexed]);
    }
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

// One thread per instance against a view's bitmask slice, appending survivors to the shared
// per-drawcall counts/lookup buffers. An indirect args buffer routes the dispatch through
// Finalize-written per-view group counts instead of covering every instance
void CulledRenderer::DispatchInstancedFill(PassParams& params, const std::string& markerName, Renderer::DescriptorSetResource& fillSet, bool filtered, u32 currentBitmaskIndex, u32 bitmaskOffset, bool keepDynamic, u32 baseInstanceLookupOffset, u32 drawCallDataSize, Renderer::BufferResource indirectArgsBuffer, u32 indirectArgsByteOffset)
{
    const u32 numInstances = params.cullingResources->GetNumInstances();

    params.commandList->PushMarker(params.passName + " " + markerName, Color::White);

    Renderer::ComputePipelineID pipeline = filtered ? _fillInstancedDrawCallsFilteredPipeline[params.cullingResources->IsIndexed()]
                                                    : _fillInstancedDrawCallsFromBitmaskPipeline[params.cullingResources->IsIndexed()];
    params.commandList->BeginPipeline(pipeline);

    struct FillDrawCallConstants
    {
        u32 numTotalInstances;
        u32 baseInstanceLookupOffset; // Byte offset into drawCallDatas where the baseInstanceLookup is stored
        u32 drawCallDataSize;
        u32 currentBitmaskIndex;
        u32 bitmaskOffset;
        u32 keepDynamic;
    };

    FillDrawCallConstants* fillConstants = params.graphResources->FrameNew<FillDrawCallConstants>();
    fillConstants->numTotalInstances = numInstances;
    fillConstants->baseInstanceLookupOffset = baseInstanceLookupOffset;
    fillConstants->drawCallDataSize = drawCallDataSize;
    fillConstants->currentBitmaskIndex = currentBitmaskIndex;
    fillConstants->bitmaskOffset = bitmaskOffset;
    fillConstants->keepDynamic = keepDynamic;
    params.commandList->PushConstant(fillConstants, 0, sizeof(FillDrawCallConstants));

    params.commandList->BindDescriptorSet(fillSet, params.frameIndex);

    if (indirectArgsBuffer != Renderer::BufferResource::Invalid())
    {
        // Finalize zeroed the group count for rings with no page work this frame
        params.commandList->DispatchIndirect(indirectArgsBuffer, indirectArgsByteOffset);
    }
    else
    {
        params.commandList->Dispatch((numInstances + 31) / 32, 1, 1);
    }

    params.commandList->EndPipeline(pipeline);
    params.commandList->PopMarker();
}

// Per-drawcall instance counts become indirect draw args, each count cleared after consumption
// (the counts buffer is always zero between uses). An indirect args buffer applies the same
// Finalize gate as the fill that produced the counts
void CulledRenderer::DispatchCreateIndirect(PassParams& params, Renderer::DescriptorSetResource& createIndirectSet, Renderer::DescriptorSetResource* debugSet, u32 baseInstanceLookupOffset, u32 drawCallDataSize, Renderer::BufferResource indirectArgsBuffer, u32 indirectArgsByteOffset)
{
    const u32 numDrawCalls = params.cullingResources->GetDrawCallCount();
    const bool debugOrdered = false; // Single-group ordered variant for determinism debugging

    params.commandList->PushMarker(params.passName + " Create Indirect", Color::Yellow);

    Renderer::ComputePipelineID pipeline = debugOrdered ? _createIndirectAfterCullingOrderedPipeline[params.cullingResources->IsIndexed()]
                                                        : _createIndirectAfterCullingPipeline[params.cullingResources->IsIndexed()];
    params.commandList->BeginPipeline(pipeline);

    struct CreateIndirectConstants
    {
        u32 numTotalDrawCalls;
        u32 baseInstanceLookupOffset;
        u32 drawCallDataSize;
    };
    CreateIndirectConstants* createIndirectConstants = params.graphResources->FrameNew<CreateIndirectConstants>();
    createIndirectConstants->numTotalDrawCalls = numDrawCalls;
    createIndirectConstants->baseInstanceLookupOffset = baseInstanceLookupOffset;
    createIndirectConstants->drawCallDataSize = drawCallDataSize;
    params.commandList->PushConstant(createIndirectConstants, 0, sizeof(CreateIndirectConstants));

    if (debugSet != nullptr)
    {
        params.commandList->BindDescriptorSet(*debugSet, params.frameIndex);
    }
    params.commandList->BindDescriptorSet(createIndirectSet, params.frameIndex);

    if (indirectArgsBuffer != Renderer::BufferResource::Invalid())
    {
        // Zero groups for views with no page work this frame, same gate as the fill
        params.commandList->DispatchIndirect(indirectArgsBuffer, indirectArgsByteOffset);
    }
    else if (debugOrdered)
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
        params.commandList->FillBuffer(params.culledInstanceCountsBuffer, 0, sizeof(u32) * numDrawCalls, 0);
        params.commandList->FillBuffer(params.drawCountBuffer, 0, sizeof(u32), 0); // CreateIndirect accumulates the surviving counts, reset or the readback stats carry over from last frame
        params.commandList->FillBuffer(params.triangleCountBuffer, 0, sizeof(u32), 0);
        params.commandList->BufferBarrier(params.culledInstanceCountsBuffer, Renderer::BufferPassUsage::TRANSFER);
        params.commandList->BufferBarrier(params.drawCountBuffer, Renderer::BufferPassUsage::TRANSFER);
        params.commandList->BufferBarrier(params.triangleCountBuffer, Renderer::BufferPassUsage::TRANSFER);

        if (params.disableTwoStepCulling)
        {
            u32 sizePerView = params.cullingResources->GetBitMaskBufferSizePerView();
            params.commandList->FillBuffer(params.culledDrawCallsBitMaskBuffer, 0, sizePerView * Renderer::Settings::MAX_VIEWS, 0);
            params.commandList->BufferBarrier(params.culledDrawCallsBitMaskBuffer, Renderer::BufferPassUsage::TRANSFER);
        }

        // Fill the occluders to draw: last frame's main-view culling output
        DispatchInstancedFill(params, "Instanced Occlusion Fill", params.occluderFillDescriptorSet, false, !params.frameIndex, 0, false, params.baseInstanceLookupOffset, params.drawCallDataSize, Renderer::BufferResource::Invalid(), 0);

        params.commandList->FillBuffer(params.culledDrawCallCountBuffer, 0, sizeof(u32), 0);
        params.commandList->BufferBarrier(params.culledDrawCallCountBuffer, Renderer::BufferPassUsage::TRANSFER);

        // Create indirect argument buffer
        DispatchCreateIndirect(params, params.createIndirectDescriptorSet, nullptr, params.baseInstanceLookupOffset, params.drawCallDataSize, Renderer::BufferResource::Invalid(), 0);

        params.commandList->BufferBarrier(params.culledDrawCallsBuffer, Renderer::BufferPassUsage::COMPUTE);

        if (params.enableDrawing)
        {
            // Draw Occluders
            params.commandList->PushMarker(params.passName + " Occlusion Draw " + std::to_string(numDrawCalls), Color::White);

            DrawParams drawParams;
            drawParams.cullingEnabled = true; // The occuder pass only makes sense if culling is enabled
            drawParams.viewIndex = 0;
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
        params.commandList->CopyBuffer(params.drawCountReadBackBuffer, 0, params.drawCountBuffer, 0, sizeof(u32));
        params.commandList->CopyBuffer(params.triangleCountReadBackBuffer, 0, params.triangleCountBuffer, 0, sizeof(u32));

        if (params.enableDrawing)
        {
            params.commandList->PopMarker();
        }
    }
    else
    {
        if (params.disableTwoStepCulling)
        {
            u32 sizePerView = params.cullingResources->GetBitMaskBufferSizePerView();

            params.commandList->FillBuffer(params.culledDrawCallsBitMaskBuffer, 0, sizePerView * Renderer::Settings::MAX_VIEWS, 0);
            params.commandList->BufferBarrier(params.culledDrawCallsBitMaskBuffer, Renderer::BufferPassUsage::TRANSFER);
        }

        // Reset the counters
        {
            params.commandList->BufferBarrier(params.drawCountBuffer, Renderer::BufferPassUsage::TRANSFER);
            params.commandList->BufferBarrier(params.triangleCountBuffer, Renderer::BufferPassUsage::TRANSFER);

            params.commandList->FillBuffer(params.drawCountBuffer, 0, sizeof(u32), 0);
            params.commandList->FillBuffer(params.triangleCountBuffer, 0, sizeof(u32), 0);

            params.commandList->BufferBarrier(params.drawCountBuffer, Renderer::BufferPassUsage::TRANSFER);
            params.commandList->BufferBarrier(params.triangleCountBuffer, Renderer::BufferPassUsage::TRANSFER);
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
                u32 currentBitmaskIndex;
            };

            FillDrawCallConstants* fillConstants = params.graphResources->FrameNew<FillDrawCallConstants>();
            fillConstants->numTotalDraws = numDrawCalls;
            fillConstants->bitmaskOffset = 0; // Occluders only draw the main view
            fillConstants->diffAgainstPrev = 0; // Occluders should not diff against prev
            fillConstants->currentBitmaskIndex = !params.frameIndex; // Occluders consume last frame's culling output
            params.commandList->PushConstant(fillConstants, 0, sizeof(FillDrawCallConstants));

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
            drawParams.viewIndex = 0;
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
        params.commandList->CopyBuffer(params.drawCountReadBackBuffer, 0, params.drawCountBuffer, 0, sizeof(u32));
        params.commandList->CopyBuffer(params.triangleCountReadBackBuffer, 0, params.triangleCountBuffer, 0, sizeof(u32));

        if (params.enableDrawing)
        {
            params.commandList->PopMarker();
        }
    }
}

// The instanced cull dispatch shared by CullingPass and ClipmapCullingPass: one thread per
// instance against the per-view frustums, writing bitmask slices and instance counts.
// occlusionCull/cullMainView/debugDrawColliders come from params; the clipmap pass overrides
// them before calling
void CulledRenderer::RunInstancedCullingDispatch(CullingPassParams& params, bool useBitmasks, bool bindDepthPyramid)
{
    const u32 numInstances = params.cullingResources->GetNumInstances();

    Renderer::ComputePipelineID pipeline = _cullingInstancedPipeline[useBitmasks];
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
        u32 currentBitmaskIndex;
        u32 numShadowViews;
        u32 bitMaskBufferUintsPerView;
        u32 debugDrawView;
        u32 cullMainView;
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
    cullConstants->currentBitmaskIndex = params.frameIndex;
    cullConstants->numShadowViews = params.numShadowViews;
    cullConstants->bitMaskBufferUintsPerView = params.cullingResources->GetBitMaskBufferUintsPerView();
    cullConstants->debugDrawView = static_cast<u32>(CVAR_ShadowDebugCullingView.Get());
    cullConstants->cullMainView = params.cullMainView;
    params.commandList->PushConstant(cullConstants, 0, sizeof(CullConstants));

    if (bindDepthPyramid)
    {
        params.cullingDescriptorSet.Bind("_depthPyramid"_h, params.depthPyramid);
    }

    params.commandList->BindDescriptorSet(params.debugDescriptorSet, params.frameIndex);
    params.commandList->BindDescriptorSet(params.globalDescriptorSet, params.frameIndex);
    params.commandList->BindDescriptorSet(params.cullingDescriptorSet, params.frameIndex);

    params.commandList->Dispatch((numInstances + 31) / 32, 1, 1);

    params.commandList->EndPipeline(pipeline);
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

                RunInstancedCullingDispatch(params, !params.disableTwoStepCulling, true);

                params.commandList->PopMarker();
            }

            params.commandList->BufferBarrier(params.culledDrawCallCountBuffer, Renderer::BufferPassUsage::COMPUTE);

            // Create indirect argument buffer
            DispatchCreateIndirect(params, params.createIndirectAfterCullSet, &params.debugDescriptorSet, params.baseInstanceLookupOffset, params.drawCallDataSize, Renderer::BufferResource::Invalid(), 0);

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
                u32 numShadowViews;
                u32 occlusionCull;
                u32 instanceIDOffset;
                u32 modelIDOffset;
                u32 drawCallDataSize;
                u32 modelIDIsDrawCallID;
                u32 cullingDataIsWorldspace;
                u32 debugDrawColliders;
                u32 bitMaskBufferUintsPerView;
                u32 currentBitmaskIndex;
            };
            CullConstants* cullConstants = params.graphResources->FrameNew<CullConstants>();
            cullConstants->viewportSizeX = u32(viewportSize.x);
            cullConstants->viewportSizeY = u32(viewportSize.y);
            cullConstants->maxDrawCount = numDrawCalls;
            cullConstants->numShadowViews = params.numShadowViews;
            cullConstants->occlusionCull = params.occlusionCull;
            cullConstants->instanceIDOffset = params.instanceIDOffset;
            cullConstants->modelIDOffset = params.modelIDOffset;
            cullConstants->drawCallDataSize = params.drawCallDataSize;

            cullConstants->modelIDIsDrawCallID = params.modelIDIsDrawCallID;
            cullConstants->cullingDataIsWorldspace = params.cullingDataIsWorldspace;
            cullConstants->debugDrawColliders = params.debugDrawColliders;
            cullConstants->bitMaskBufferUintsPerView = params.cullingResources->GetBitMaskBufferUintsPerView();
            cullConstants->currentBitmaskIndex = params.frameIndex;
            params.commandList->PushConstant(cullConstants, 0, sizeof(CullConstants));

            params.cullingDescriptorSet.Bind("_depthPyramid"_h, params.depthPyramid);

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

void CulledRenderer::ClipmapCullingPass(CullingPassParams& params)
{
    NC_ASSERT(params.drawCallDataSize > 0, "CulledRenderer : ClipmapCullingPass params provided an invalid drawCallDataSize");
    NC_ASSERT(params.cullingResources->IsInstanced(), "CulledRenderer : ClipmapCullingPass only supports instanced culling resources");

    const u32 numDrawCalls = params.cullingResources->GetDrawCallCount();
    u32 numInstances = params.cullingResources->GetNumInstances();

    if (numDrawCalls == 0 || numInstances == 0 || params.numShadowViews == 0)
        return;

    // Frustum-only cull of the clipmap views into their bitmask slices. No counter resets (the
    // per-clipmap fill in the geometry pass resets and rebuilds the shared draw sets), no occlusion
    std::string debugName = params.passName + " Clipmap Culling";
    params.commandList->PushMarker(debugName, Color::Yellow);

    params.occlusionCull = false;
    params.cullMainView = false;
    params.debugDrawColliders = false;

    // Clipmaps require the bitmask permutation; _depthPyramid stays bound from the main culling
    // pass, rebinding here would rewrite an already-bound set
    RunInstancedCullingDispatch(params, true, false);

    params.commandList->PopMarker();
}

// Rebuilds the shared instance counts/lookup/argument buffers from a view's bitmask slice.
// filtered selects the SVSM caster-split fill variant, keepDynamic its instance class
void CulledRenderer::RunInstancedGeometryFill(GeometryPassParams& params, u32 viewIndex, bool filtered, bool keepDynamic)
{
    const u32 numDrawCalls = params.cullingResources->GetDrawCallCount();

    // SVSM fills gate their drawcall-granularity setup (instance-count clear + CreateIndirect)
    // on the Finalize-written per-view args; the counts buffer stays always-zero between uses
    // because CreateIndirect clears after consumption. The args byte layout comes through params
    // (the ShadowRenderer's stride/offset constants)
    const bool gatedSetup = params.svsmPass && params.svsmFillArgsBuffer != Renderer::BufferResource::Invalid();
    Renderer::BufferResource fillArgsBuffer = gatedSetup ? params.svsmFillArgsBuffer : Renderer::BufferResource::Invalid();
    const u32 fillArgsOffset = gatedSetup ? (viewIndex - 1) * params.svsmFillArgsViewStride + (keepDynamic ? params.svsmFillArgsDynamicOffset : 0) : 0;
    const u32 overheadArgsOffset = gatedSetup ? (viewIndex - 1) * params.svsmFillArgsViewStride + (keepDynamic ? params.svsmFillArgsDynamicOverheadOffset : params.svsmFillArgsStaticOverheadOffset) : 0;

    // Reset the counters. The per-drawcall instance counts skip their FillBuffer in the gated
    // path: CreateIndirect clears each count after consuming it, so the buffer is always zero
    // between uses (and a gated-off CreateIndirect means nothing read the counts this view)
    params.commandList->BufferBarrier(params.drawCountBuffer, Renderer::BufferPassUsage::TRANSFER);
    params.commandList->BufferBarrier(params.triangleCountBuffer, Renderer::BufferPassUsage::TRANSFER);
    if (!gatedSetup)
    {
        params.commandList->BufferBarrier(params.culledInstanceCountsBuffer, Renderer::BufferPassUsage::TRANSFER);
        params.commandList->FillBuffer(params.culledInstanceCountsBuffer, 0, sizeof(u32) * numDrawCalls, 0);
        params.commandList->BufferBarrier(params.culledInstanceCountsBuffer, Renderer::BufferPassUsage::TRANSFER);
    }
    params.commandList->FillBuffer(params.drawCountBuffer, 0, sizeof(u32), 0);
    params.commandList->FillBuffer(params.triangleCountBuffer, 0, sizeof(u32), 0);
    params.commandList->BufferBarrier(params.drawCountBuffer, Renderer::BufferPassUsage::TRANSFER);
    params.commandList->BufferBarrier(params.triangleCountBuffer, Renderer::BufferPassUsage::TRANSFER);

    // Fill the instances visible in this view
    DispatchInstancedFill(params, "Instanced Geometry Fill", params.fillDescriptorSet, filtered, params.frameIndex, viewIndex * params.cullingResources->GetBitMaskBufferUintsPerView(), keepDynamic, params.baseInstanceLookupOffset, params.drawCallDataSize, fillArgsBuffer, fillArgsOffset);

    params.commandList->BufferBarrier(params.culledInstanceCountsBuffer, Renderer::BufferPassUsage::COMPUTE);

    // The draws consume this count, a zero-work view must still draw zero
    params.commandList->FillBuffer(params.culledDrawCallCountBuffer, 0, sizeof(u32), 0);
    params.commandList->BufferBarrier(params.culledDrawCallCountBuffer, Renderer::BufferPassUsage::TRANSFER);

    // Create indirect argument buffer
    DispatchCreateIndirect(params, params.createIndirectDescriptorSet, nullptr, params.baseInstanceLookupOffset, params.drawCallDataSize, fillArgsBuffer, overheadArgsOffset);

    params.commandList->BufferBarrier(params.culledDrawCallsBuffer, Renderer::BufferPassUsage::COMPUTE);
    params.commandList->BufferBarrier(params.culledDrawCallCountBuffer, Renderer::BufferPassUsage::COMPUTE);
    params.commandList->BufferBarrier(params.drawCountBuffer, Renderer::BufferPassUsage::COMPUTE);
    params.commandList->BufferBarrier(params.triangleCountBuffer, Renderer::BufferPassUsage::COMPUTE);
}

void CulledRenderer::GeometryPass(GeometryPassParams& params)
{
    NC_ASSERT(params.drawCallback != nullptr, "CulledRenderer : GeometryPass got params with invalid drawCallback");

    const u32 numDrawCalls = params.cullingResources->GetDrawCallCount();

    // svsmProfileGeometry: per-view fill/draw GPU timings, they surface in the perf editor's
    // render pass list
    const bool profileSVSM = params.svsmPass && *CVarSystem::Get()->GetIntCVar(CVarCategory::Client | CVarCategory::Rendering, "svsmProfileGeometry"_h) != 0;
    RenderUtils::SVSMGeometryProfiler Profiled(_renderer, *params.commandList, "SVSM", profileSVSM);

    for (u32 i = params.firstViewIndex; i < params.numShadowViews + 1; i++)
    {
        std::string markerName = (i == 0) ? "Main" : "Clipmap " + std::to_string(i - 1);
        params.commandList->PushMarker(markerName, Color::PastelYellow);

        if (i == 1)
        {
            // The viewport spans the virtual texture so SV_Position.xy is the virtual texel.
            // No depth bias: there is no depth attachment for it to apply to
            params.commandList->SetViewport(0, 0, static_cast<f32>(params.svsmExtent.x), static_cast<f32>(params.svsmExtent.y), 0.0f, 1.0f);
            params.commandList->SetScissorRect(0, params.svsmExtent.x, 0, params.svsmExtent.y);
        }

        // Reset the counters
        if (!params.cullingResources->IsInstanced() && params.cullingResources->HasSupportForTwoStepCulling())
        {
            params.commandList->BufferBarrier(params.drawCountBuffer, Renderer::BufferPassUsage::TRANSFER);
            params.commandList->FillBuffer(params.drawCountBuffer, 0, sizeof(u32), 0);
            params.commandList->BufferBarrier(params.drawCountBuffer, Renderer::BufferPassUsage::TRANSFER);

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
                    u32 currentBitmaskIndex;
                };

                FillDrawCallConstants* fillConstants = params.graphResources->FrameNew<FillDrawCallConstants>();
                fillConstants->numTotalDraws = numDrawCalls;
                fillConstants->bitmaskOffset = i * params.cullingResources->GetBitMaskBufferUintsPerView();
                fillConstants->diffAgainstPrev = 1; // Geometry should diff against prev
                fillConstants->currentBitmaskIndex = params.frameIndex;
                params.commandList->PushConstant(fillConstants, 0, sizeof(FillDrawCallConstants));

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
        else if (params.cullingResources->IsInstanced() && params.cullingResources->HasSupportForTwoStepCulling() && i > 0)
        {
            // The main view (i == 0) consumes the culling pass's output directly, cascades rebuild the
            // shared instance counts/lookup/argument buffers from their bitmask slice before drawing
            Profiled("Fill", i, [&] { RunInstancedGeometryFill(params, i, params.svsmSplitFills, false); });
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
            drawParams.svsmPass = params.svsmPass;
            drawParams.svsmExtent = params.svsmExtent;
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


            const bool svsmClipRects = params.svsmPass && i > 0
                && *CVarSystem::Get()->GetIntCVar(CVarCategory::Client | CVarCategory::Rendering, "svsmClipRects"_h) != 0;
            Profiled("Draw", i, [&] { RenderUtils::DrawSVSMClipRects(svsmClipRects, drawParams, [&](DrawParams& rectDrawParams) { params.drawCallback(rectDrawParams); }); });
        }

        // Copy from our draw count buffer to the readback buffer
        params.commandList->CopyBuffer(params.drawCountReadBackBuffer, sizeof(u32) * i, params.drawCountBuffer, 0, sizeof(u32));
        params.commandList->CopyBuffer(params.triangleCountReadBackBuffer, sizeof(u32) * i, params.triangleCountBuffer, 0, sizeof(u32));

        if (params.enableDrawing)
        {
            params.commandList->PopMarker();
        }

        // SVSM caster split: rebuild the shared buffers with only dynamic instances and draw them
        // into the dynamic pool. Runs after the static readback copies so the perf stats show the
        // static (dominant) counts. Rings without resident dynamic pages cost a zero-group
        // indirect fill and a zero-count draw (Finalize's fill args, same-frame GPU truth)
        if (params.svsmSplitFills && params.enableDrawing && i > 0)
        {
            params.commandList->PushMarker("Dynamic", Color::PastelOrange);

            Profiled("DynFill", i, [&] { RunInstancedGeometryFill(params, i, true, true); });

            DrawParams drawParams;
            drawParams.cullingEnabled = params.cullingEnabled;
            drawParams.svsmPass = params.svsmPass;
            drawParams.svsmDynamicPass = true;
            drawParams.svsmExtent = params.svsmExtent;
            drawParams.viewIndex = i;
            drawParams.cullingResources = params.cullingResources;
            drawParams.argumentBuffer = params.culledDrawCallsBuffer;
            drawParams.drawCountBuffer = params.culledDrawCallCountBuffer;
            drawParams.drawCountIndex = 0;
            drawParams.numMaxDrawCalls = params.cullingResources->GetDrawCallCount();

            Profiled("DynDraw", i, [&] { params.drawCallbackDynamic(drawParams); });

            // Dynamic surviving-count readback: the fill wrote this view's dynamic instance count
            // to drawCountBuffer, snapshot it so missing dynamic draws are visible in the perf editor
            params.commandList->CopyBuffer(params.svsmDynamicDrawCountReadBackBuffer, sizeof(u32) * i, params.drawCountBuffer, 0, sizeof(u32));

            params.commandList->PopMarker();
        }

        params.commandList->PopMarker();
    }

    // Finish by resetting the viewport and scissor
    vec2 renderSize = _renderer->GetRenderSize();
    params.commandList->SetViewport(0, 0, renderSize.x, renderSize.y, 0.0f, 1.0f);
    params.commandList->SetScissorRect(0, static_cast<u32>(renderSize.x), 0, static_cast<u32>(renderSize.y));
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
        for (u32 i = 0; i < 2; i++)
        {
            for (u32 filtered = 0; filtered < 2; filtered++)
            {
                pipelineDesc.debugName = (filtered == 0) ? "FillInstancedDrawcallsFromBitmask" : "FillInstancedDrawcallsFromBitmaskFiltered";

                std::vector<Renderer::PermutationField> permutationFields =
                {
                    { "IS_INDEXED", std::to_string(i) },
                    { "DYNAMIC_FILTER", std::to_string(filtered) }
                };
                u32 shaderEntryNameHash = Renderer::GetShaderEntryNameHash("Utils/FillInstancedDrawCallsFromBitmask.cs", permutationFields);

                Renderer::ComputeShaderDesc shaderDesc;
                shaderDesc.shaderEntry = _gameRenderer->GetShaderEntry(shaderEntryNameHash, "Utils/FillInstancedDrawCallsFromBitmask.cs");

                pipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);

                if (filtered == 0)
                {
                    _fillInstancedDrawCallsFromBitmaskPipeline[i] = _renderer->CreatePipeline(pipelineDesc);
                }
                else
                {
                    _fillInstancedDrawCallsFilteredPipeline[i] = _renderer->CreatePipeline(pipelineDesc);
                }
            }
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