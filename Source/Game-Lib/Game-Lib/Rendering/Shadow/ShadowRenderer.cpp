#include "ShadowRenderer.h"
#include <Game-Lib/Application/EnttRegistries.h>
#include <Game-Lib/ECS/Components/Camera.h>
#include <Game-Lib/ECS/Singletons/DayNightCycle.h>
#include <Game-Lib/ECS/Systems/CalculateShadowCameraMatrices.h>
#include <Game-Lib/ECS/Systems/UpdateAreaLights.h>
#include <Game-Lib/Rendering/Debug/DebugRenderer.h>
#include <Game-Lib/Rendering/GameRenderer.h>
#include <Game-Lib/Rendering/Terrain/TerrainRenderer.h>
#include <Game-Lib/Rendering/Model/ModelRenderer.h>
#include <Game-Lib/Rendering/RenderResources.h>
#include <Game-Lib/Rendering/Camera.h>
#include <Game-Lib/Util/ServiceLocator.h>

#include <Renderer/Descriptors/ComputeShaderDesc.h>

#include <Base/CVarSystem/CVarSystem.h>

#include <Renderer/Renderer.h>
#include <Renderer/RenderGraph.h>

#include <entt/entt.hpp>

AutoCVar_Int CVAR_ShadowEnabled(CVarCategory::Client | CVarCategory::Rendering, "shadowEnabled", "enable shadows", 1, CVarFlags::EditCheckbox);
AutoCVar_Float CVAR_ShadowStrength(CVarCategory::Client | CVarCategory::Rendering, "shadowStrength", "directional shadow strength, overwritten each frame from the sun elevation", 1.0f);

AutoCVar_Int CVAR_ShadowDebugMatrices(CVarCategory::Client | CVarCategory::Rendering, "shadowDebugMatrices", "debug shadow matrices by applying them to the camera", 0, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_ShadowDebugMatrixIndex(CVarCategory::Client | CVarCategory::Rendering, "shadowDebugMatricesIndex", "index of the cascade to debug", 0);

AutoCVar_Int CVAR_ShadowDrawMatrices(CVarCategory::Client | CVarCategory::Rendering, "shadowDrawMatrices", "debug shadow matrices by debug drawing them", 0, CVarFlags::EditCheckbox);

AutoCVar_Int CVAR_ShadowFilterMode(CVarCategory::Client | CVarCategory::Rendering, "shadowFilterMode", "0: No filtering, 1: Percentage Closer Filtering, 2: Percentage Closer Soft Shadows", 1);
AutoCVar_Float CVAR_ShadowFilterSize(CVarCategory::Client | CVarCategory::Rendering, "shadowFilterSize", "size of the filter used for shadow sampling", 3.0f);
AutoCVar_Float CVAR_ShadowFilterPenumbraSize(CVarCategory::Client | CVarCategory::Rendering, "shadowFilterPenumbraSize", "size of the filter used for penumbra sampling", 3.0f);

AutoCVar_Float CVAR_ShadowDepthBiasConstantFactor(CVarCategory::Client | CVarCategory::Rendering, "shadowDepthBiasConstant", "constant factor of depth bias to prevent shadow acne", -2.0f);
AutoCVar_Float CVAR_ShadowDepthBiasClamp(CVarCategory::Client | CVarCategory::Rendering, "shadowDepthBiasClamp", "clamp of depth bias to prevent shadow acne", 0.0f);
AutoCVar_Float CVAR_ShadowDepthBiasSlopeFactor(CVarCategory::Client | CVarCategory::Rendering, "shadowDepthBiasSlope", "slope factor of depth bias to prevent shadow acne", -5.0f);
AutoCVar_Float CVAR_ShadowNormalOffsetBias(CVarCategory::Client | CVarCategory::Rendering, "shadowNormalOffsetBias", "receiver offset along the surface normal in cascade texels, fights acne on hard angles", 1.0f);

AutoCVar_Int CVAR_ShadowUseSDSM(CVarCategory::Client | CVarCategory::Rendering, "shadowUseSDSM", "fit cascade cameras on the GPU instead of the legacy CPU path", 1, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_ShadowSDSMUseDepthBounds(CVarCategory::Client | CVarCategory::Rendering, "shadowSDSMUseDepthBounds", "distribute cascades over the visible depth range instead of the full shadow range", 1, CVarFlags::EditCheckbox);
AutoCVar_Float CVAR_ShadowSDSMQuantizeStep(CVarCategory::Client | CVarCategory::Rendering, "shadowSDSMQuantizeStep", "SDSM bounds are quantized outward to this step to keep stable snapping effective", 8.0f);
AutoCVar_Float CVAR_ShadowSDSMShrinkDelay(CVarCategory::Client | CVarCategory::Rendering, "shadowSDSMShrinkDelay", "seconds the visible range must stay smaller before the SDSM bounds shrink to it in one jump, expansion is instant", 2.5f);
AutoCVar_Int CVAR_ShadowSDSMValidateParity(CVarCategory::Client | CVarCategory::Rendering, "shadowSDSMValidateParity", "compare GPU-fitted cascade cameras against the CPU math and log the max delta", 0, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_ShadowSDSMUseXYBounds(CVarCategory::Client | CVarCategory::Rendering, "shadowSDSMUseXYBounds", "fit cascades to the light-space footprint of visible samples: 0 = off, 1 = diagnostics only, 2 = drive the cameras", 2);
AutoCVar_Float CVAR_ShadowSDSMXYMarginTexels(CVarCategory::Client | CVarCategory::Rendering, "shadowSDSMXYMarginTexels", "filter-kernel margin in texels added to the fitted XY bounds, the normal offset bias is added on top", 4.0f);

ShadowRenderer::ShadowRenderer(Renderer::Renderer* renderer, GameRenderer* gameRenderer, DebugRenderer* debugRenderer, TerrainRenderer* terrainRenderer, ModelRenderer* modelRenderer, RenderResources& resources)
    : _renderer(renderer)
    , _gameRenderer(gameRenderer)
    , _debugRenderer(debugRenderer)
    , _terrainRenderer(terrainRenderer)
    , _modelRenderer(modelRenderer)
    , _depthMinMaxDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS)
    , _cascadeFitRangeDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS)
    , _cascadeXYReduceDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS)
    , _cascadeFitCamerasDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS)
{
    CreatePermanentResources(resources);
}

ShadowRenderer::~ShadowRenderer()
{
}

void ShadowRenderer::Update(f32 deltaTime, RenderResources& resources)
{
    ZoneScoped;

    // Read back last frame's reduced depth bounds for diagnostics
    {
        u32* readBackData = static_cast<u32*>(_renderer->MapBuffer(_depthMinMaxReadBackBuffer));
        if (readBackData != nullptr)
        {
            _depthMinMaxReadBack[0] = readBackData[0];
            _depthMinMaxReadBack[1] = readBackData[1];
        }
        _renderer->UnmapBuffer(_depthMinMaxReadBackBuffer);
    }

    _lastDeltaTime = deltaTime;

    CVarSystem* cvarSystem = CVarSystem::Get();
    const u32 numCascades = static_cast<u32>(*cvarSystem->GetIntCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowCascadeNum"));
    const bool useSDSM = CVAR_ShadowUseSDSM.Get();

    // With SDSM the cascade cameras live on the GPU, the debug tools read the (one frame old) readback copies
    if (useSDSM)
    {
        Camera* readBackData = static_cast<Camera*>(_renderer->MapBuffer(_cascadeCamerasReadBackBuffer));
        if (readBackData != nullptr)
        {
            memcpy(_readBackCascadeCameras, readBackData, sizeof(Camera) * Renderer::Settings::MAX_SHADOW_CASCADES);
        }
        _renderer->UnmapBuffer(_cascadeCamerasReadBackBuffer);

        f32* sdsmData = static_cast<f32*>(_renderer->MapBuffer(_sdsmDataReadBackBuffer));
        if (sdsmData != nullptr)
        {
            memcpy(_sdsmDataReadBack, sdsmData, sizeof(f32) * SDSM_DATA_FLOAT_COUNT);
        }
        _renderer->UnmapBuffer(_sdsmDataReadBackBuffer);
    }

    auto GetCascadeCamera = [&](u32 cascadeIndex) -> const Camera&
    {
        return useSDSM ? _readBackCascadeCameras[cascadeIndex] : resources.cameras[cascadeIndex + 1];
    };

    if (CVAR_ShadowSDSMValidateParity.Get() && useSDSM)
    {
        // The CPU mirror holds the legacy math result while validation is on (computed but not uploaded).
        // Only meaningful with a static camera, the readback is one frame old
        f32 maxDelta = 0.0f;
        for (u32 i = 0; i < numCascades; i++)
        {
            const mat4x4& gpuMatrix = _readBackCascadeCameras[i].worldToClip;
            const mat4x4& cpuMatrix = resources.cameras[i + 1].worldToClip;

            for (u32 col = 0; col < 4; col++)
            {
                for (u32 row = 0; row < 4; row++)
                {
                    maxDelta = Math::Max(maxDelta, glm::abs(gpuMatrix[col][row] - cpuMatrix[col][row]));
                }
            }
        }

        static u32 parityLogCounter = 0;
        if (parityLogCounter++ % 60 == 0)
        {
            NC_LOG_INFO("SDSM Parity: max worldToClip delta {0} across {1} cascades (stand still, readback is one frame old)", maxDelta, numCascades);
        }
    }

    const bool debugMatrices = CVAR_ShadowDebugMatrices.Get();
    const i32 debugMatrixIndex = CVAR_ShadowDebugMatrixIndex.Get();
    if (debugMatrices && debugMatrixIndex >= 0 && debugMatrixIndex < static_cast<i32>(numCascades))
    {
        const Camera& debugCascadeCamera = GetCascadeCamera(debugMatrixIndex);

        Camera& mainCamera = resources.cameras[0];

        mainCamera = debugCascadeCamera;
        resources.cameras.SetDirtyElement(0);
    }

    if (CVAR_ShadowDrawMatrices.Get())
    {
        Color colors[] =
        {
            Color::Red,
            Color::Green,
            Color::Blue,
            Color::Yellow,
            Color::Magenta,
            Color::Cyan,
            Color::PastelOrange,
            Color::PastelGreen
        };

        for (u32 i = 0; i < numCascades; i++)
        {
            const Camera& debugCascadeCamera = GetCascadeCamera(i);
            _debugRenderer->DrawFrustum(debugCascadeCamera.worldToClip, colors[i]);
        }
    }
}

void ShadowRenderer::AddDepthMinMaxPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    struct Data
    {
        Renderer::DepthImageResource depth;

        Renderer::BufferMutableResource depthMinMaxBuffer;
        Renderer::BufferMutableResource depthMinMaxReadBackBuffer;

        Renderer::DescriptorSetResource passSet;
    };

    CVarSystem* cvarSystem = CVarSystem::Get();
    const u32 numCascades = static_cast<u32>(*cvarSystem->GetIntCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowCascadeNum"));
    const bool dispatchEnabled = CVAR_ShadowEnabled.Get() && numCascades > 0;

    renderGraph->AddPass<Data>("Shadow Depth MinMax",
        [this, &resources](Data& data, Renderer::RenderGraphBuilder& builder)
        {
            using BufferUsage = Renderer::BufferPassUsage;

            data.depth = builder.Read(resources.depth, Renderer::PipelineType::COMPUTE);

            data.depthMinMaxBuffer = builder.Write(_depthMinMaxBuffer, BufferUsage::TRANSFER | BufferUsage::COMPUTE);
            data.depthMinMaxReadBackBuffer = builder.Write(_depthMinMaxReadBackBuffer, BufferUsage::TRANSFER);

            data.passSet = builder.Use(_depthMinMaxDescriptorSet);

            return true; // Return true from setup to enable this pass, return false to disable it
        },
        [this, frameIndex, dispatchEnabled](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList)
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, ShadowDepthMinMax);

            // Reset to the sentinel, if the dispatch is skipped or every pixel is sky it survives
            // and the consumers fall back to the full range
            commandList.FillBuffer(data.depthMinMaxBuffer, 0, sizeof(u32), 0xFFFFFFFF);
            commandList.FillBuffer(data.depthMinMaxBuffer, sizeof(u32), sizeof(u32), 0);
            commandList.BufferBarrier(data.depthMinMaxBuffer, Renderer::BufferPassUsage::TRANSFER);

            if (dispatchEnabled)
            {
                commandList.BeginPipeline(_depthMinMaxPipeline);

                data.passSet.Bind("_depth"_h, data.depth);
                commandList.BindDescriptorSet(data.passSet, frameIndex);

                uvec2 depthDimensions = graphResources.GetImageDimensions(data.depth);
                commandList.Dispatch((depthDimensions.x + 15) / 16, (depthDimensions.y + 15) / 16, 1);

                commandList.EndPipeline(_depthMinMaxPipeline);

                commandList.BufferBarrier(data.depthMinMaxBuffer, Renderer::BufferPassUsage::COMPUTE);
            }

            commandList.CopyBuffer(data.depthMinMaxReadBackBuffer, 0, data.depthMinMaxBuffer, 0, sizeof(u32) * 2);
        });
}

void ShadowRenderer::AddCascadeFitPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    struct Data
    {
        Renderer::DepthImageResource depth;

        Renderer::BufferMutableResource cameras;
        Renderer::BufferMutableResource sdsmDataBuffer;
        Renderer::BufferMutableResource cascadeBoundsBuffer;
        Renderer::BufferMutableResource sdsmDataReadBackBuffer;
        Renderer::BufferMutableResource cascadeCamerasReadBackBuffer;

        Renderer::DescriptorSetResource rangeSet;
        Renderer::DescriptorSetResource reduceSet;
        Renderer::DescriptorSetResource camerasSet;
    };

    CVarSystem* cvarSystem = CVarSystem::Get();
    u32 numCascades = static_cast<u32>(*cvarSystem->GetIntCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowCascadeNum"));
    numCascades = std::min(numCascades, static_cast<u32>(resources.shadowDepthCascades.size()));

    const bool freezeCascades = *cvarSystem->GetIntCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowFreezeCascades") != 0;
    const bool dispatchEnabled = CVAR_ShadowUseSDSM.Get() && CVAR_ShadowEnabled.Get() && numCascades > 0 && !freezeCascades;

    if (!dispatchEnabled)
        return;

    // Record-time inputs for the push constants, the shadow sun steps in discrete intervals
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    auto& dayNightCycle = registry->ctx().get<ECS::Singletons::DayNightCycle>();
    const f32 shadowTimeOfDay = ECS::Systems::GetShadowTimeOfDay(dayNightCycle.GetTimeInSecondsF32());
    const vec3 lightDirection = ECS::Systems::UpdateAreaLights::GetLightDirection(shadowTimeOfDay);

    renderGraph->AddPass<Data>("Shadow Cascade Fit",
        [this, &resources](Data& data, Renderer::RenderGraphBuilder& builder)
        {
            using BufferUsage = Renderer::BufferPassUsage;

            data.depth = builder.Read(resources.depth, Renderer::PipelineType::COMPUTE);

            data.cameras = builder.Write(resources.cameras.GetBuffer(), BufferUsage::COMPUTE | BufferUsage::TRANSFER);
            builder.Read(_depthMinMaxBuffer, BufferUsage::COMPUTE);
            data.sdsmDataBuffer = builder.Write(_sdsmDataBuffer, BufferUsage::COMPUTE | BufferUsage::TRANSFER);
            data.cascadeBoundsBuffer = builder.Write(_cascadeBoundsBuffer, BufferUsage::TRANSFER | BufferUsage::COMPUTE);
            data.sdsmDataReadBackBuffer = builder.Write(_sdsmDataReadBackBuffer, BufferUsage::TRANSFER);
            data.cascadeCamerasReadBackBuffer = builder.Write(_cascadeCamerasReadBackBuffer, BufferUsage::TRANSFER);

            data.rangeSet = builder.Use(_cascadeFitRangeDescriptorSet);
            data.reduceSet = builder.Use(_cascadeXYReduceDescriptorSet);
            data.camerasSet = builder.Use(_cascadeFitCamerasDescriptorSet);

            return true; // Return true from setup to enable this pass, return false to disable it
        },
        [this, frameIndex, numCascades, lightDirection, cvarSystem](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList)
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, ShadowCascadeFit);

            struct CascadeFitConstants
            {
                vec4 lightDirection;
                u32 numCascades;
                f32 cascadeSplitLambda;
                f32 cascadeTextureSize;
                u32 stableShadows;
                f32 shadowMaxDistance;
                f32 quantizeStep;
                f32 shrinkDelay;
                f32 deltaTime;
                u32 useDepthBounds;
                f32 casterMargin;
                u32 useXYBounds;
                f32 xyMarginTexels;
            };

            CascadeFitConstants* constants = graphResources.FrameNew<CascadeFitConstants>();
            constants->lightDirection = vec4(lightDirection, 0.0f);
            constants->numCascades = numCascades;
            constants->cascadeSplitLambda = static_cast<f32>(*cvarSystem->GetFloatCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowCascadeSplitLambda"));
            constants->cascadeTextureSize = static_cast<f32>(*cvarSystem->GetIntCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowCascadeSize"));
            constants->stableShadows = *cvarSystem->GetIntCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowStable") != 0;
            constants->shadowMaxDistance = static_cast<f32>(*cvarSystem->GetFloatCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowMaxDistance"));
            constants->quantizeStep = CVAR_ShadowSDSMQuantizeStep.GetFloat();
            constants->shrinkDelay = CVAR_ShadowSDSMShrinkDelay.GetFloat();
            constants->deltaTime = _lastDeltaTime;
            constants->useDepthBounds = CVAR_ShadowSDSMUseDepthBounds.Get();
            constants->casterMargin = static_cast<f32>(*cvarSystem->GetFloatCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowCasterMargin"));

            // The XY path presumes the stable up rule and depth bounds, and parity compares
            // against the legacy CPU math which it intentionally diverges from
            u32 useXYBounds = static_cast<u32>(Math::Max(CVAR_ShadowSDSMUseXYBounds.Get(), 0));
            if (CVAR_ShadowSDSMValidateParity.Get() || !constants->useDepthBounds || !constants->stableShadows)
            {
                useXYBounds = Math::Min(useXYBounds, 1u);
            }
            constants->useXYBounds = useXYBounds;
            constants->xyMarginTexels = CVAR_ShadowSDSMXYMarginTexels.GetFloat() + CVAR_ShadowNormalOffsetBias.GetFloat();

            // Reset the light-space bounds to the encoded sentinels
            commandList.FillBuffer(data.cascadeBoundsBuffer, 0, sizeof(u32) * 24, 0xFFFFFFFF);
            commandList.FillBuffer(data.cascadeBoundsBuffer, sizeof(u32) * 24, sizeof(u32) * 24, 0);
            commandList.BufferBarrier(data.cascadeBoundsBuffer, Renderer::BufferPassUsage::TRANSFER);

            // Range: reduced depth bounds -> working range + split distances
            commandList.BeginPipeline(_cascadeFitRangePipeline);
            commandList.PushConstant(constants, 0, sizeof(CascadeFitConstants));

            data.rangeSet.Bind("_srcCameras"_h, data.cameras);
            commandList.BindDescriptorSet(data.rangeSet, frameIndex);

            commandList.Dispatch(1, 1, 1);
            commandList.EndPipeline(_cascadeFitRangePipeline);

            commandList.BufferBarrier(data.sdsmDataBuffer, Renderer::BufferPassUsage::COMPUTE);

            // XY reduce: light-space AABB of the visible samples per cascade
            if (useXYBounds >= 1)
            {
                commandList.BeginPipeline(_cascadeXYReducePipeline);
                commandList.PushConstant(constants, 0, sizeof(CascadeFitConstants));

                data.reduceSet.Bind("_depth"_h, data.depth);
                data.reduceSet.Bind("_srcCameras"_h, data.cameras);
                commandList.BindDescriptorSet(data.reduceSet, frameIndex);

                uvec2 depthDimensions = graphResources.GetImageDimensions(data.depth);
                commandList.Dispatch((depthDimensions.x + 15) / 16, (depthDimensions.y + 15) / 16, 1);

                commandList.EndPipeline(_cascadeXYReducePipeline);

                commandList.BufferBarrier(data.cascadeBoundsBuffer, Renderer::BufferPassUsage::COMPUTE);
            }

            // Cameras: build the cascade cameras from the splits
            commandList.BeginPipeline(_cascadeFitCamerasPipeline);
            commandList.PushConstant(constants, 0, sizeof(CascadeFitConstants));

            data.camerasSet.Bind("_rwCameras"_h, data.cameras);
            commandList.BindDescriptorSet(data.camerasSet, frameIndex);

            commandList.Dispatch(1, 1, 1);
            commandList.EndPipeline(_cascadeFitCamerasPipeline);

            commandList.BufferBarrier(data.cameras, Renderer::BufferPassUsage::COMPUTE);
            commandList.BufferBarrier(data.sdsmDataBuffer, Renderer::BufferPassUsage::COMPUTE);

            // Readbacks for the debug tooling, element 0 of the cameras buffer is the main camera
            commandList.CopyBuffer(data.cascadeCamerasReadBackBuffer, 0, data.cameras, sizeof(Camera), sizeof(Camera) * numCascades);
            commandList.CopyBuffer(data.sdsmDataReadBackBuffer, 0, data.sdsmDataBuffer, 0, sizeof(f32) * SDSM_DATA_FLOAT_COUNT);
        });
}

bool ShadowRenderer::GetEffectiveShadowRange(f32& outMinDistance, f32& outMaxDistance) const
{
    f32 usedMin = _sdsmDataReadBack[2];
    f32 usedMax = _sdsmDataReadBack[3];
    if (usedMax <= usedMin) // Not fitted yet
        return false;

    outMinDistance = usedMin;
    outMaxDistance = usedMax;
    return true;
}

bool ShadowRenderer::GetCascadeFittedBounds(u32 cascadeIndex, vec3& outExtents, bool& outValid) const
{
    if (CVAR_ShadowSDSMUseXYBounds.Get() < 1 || cascadeIndex >= Renderer::Settings::MAX_SHADOW_CASCADES)
        return false;

    // cascadeDiag lives after SDSMState (8) + splitDist (8) + splitDepth (8) + cascadeStable (32)
    const f32* diag = &_sdsmDataReadBack[56 + cascadeIndex * 4];
    outExtents = vec3(diag[0], diag[1], diag[2]);
    outValid = diag[3] > 0.5f;
    return true;
}

bool ShadowRenderer::GetDepthBoundsViewDistances(const RenderResources& resources, f32& outMinDistance, f32& outMaxDistance) const
{
    if (_depthMinMaxReadBack[1] == 0) // Sentinel, no valid depth samples reduced yet
        return false;

    const vec4& nearFar = resources.cameras[0].nearFar;
    f32 nearClip = nearFar.x;
    f32 farClip = nearFar.y;

    // Reversed Z linearization, depth 1 = near plane, depth 0 = far plane
    auto Linearize = [nearClip, farClip](f32 depth) { return (nearClip * farClip) / (nearClip + depth * (farClip - nearClip)); };

    outMinDistance = Linearize(glm::uintBitsToFloat(_depthMinMaxReadBack[1])); // Max depth bits = nearest sample
    outMaxDistance = Linearize(glm::uintBitsToFloat(_depthMinMaxReadBack[0])); // Min depth bits = farthest sample
    return true;
}

void ShadowRenderer::AddShadowPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    struct ShadowPassData
    {
        Renderer::DepthImageMutableResource shadowDepthCascades[Renderer::Settings::MAX_SHADOW_CASCADES];

        Renderer::DescriptorSetResource lightDescriptorSet;
    };

    CVarSystem* cvarSystem = CVarSystem::Get();
    u32 numCascades = static_cast<u32>(*cvarSystem->GetIntCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowCascadeNum"));
    numCascades = std::min(numCascades, static_cast<u32>(resources.shadowDepthCascades.size()));

    if (numCascades == 0)
        return;

    renderGraph->AddPass<ShadowPassData>("Shadow Pass",
        [=, &resources](ShadowPassData& data, Renderer::RenderGraphBuilder& builder)
        {
            for (u32 i = 0; i < numCascades; i++)
            {
                data.shadowDepthCascades[i] = builder.Write(resources.shadowDepthCascades[i], Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::CLEAR);
            }

            data.lightDescriptorSet = builder.Use(resources.lightDescriptorSet);

            return true; // Return true from setup to enable this pass, return false to disable it
        },
        [=](ShadowPassData& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList)
        {
            Renderer::DepthImageMutableResource cascadeDepthResource;
            for (u32 i = 0; i < Renderer::Settings::MAX_SHADOW_CASCADES; i++)
            {
                if (i < numCascades)
                {
                    cascadeDepthResource = data.shadowDepthCascades[i];
                }

                data.lightDescriptorSet.BindArray("_shadowCascadeRTs", cascadeDepthResource, i);
            }
        });
}

void ShadowRenderer::CreatePermanentResources(RenderResources& resources)
{
    Renderer::SamplerDesc samplerDesc;
    samplerDesc.enabled = true;
    samplerDesc.filter = Renderer::SamplerFilter::MIN_MAG_MIP_LINEAR;
    samplerDesc.addressU = Renderer::TextureAddressMode::CLAMP;
    samplerDesc.addressV = Renderer::TextureAddressMode::CLAMP;
    samplerDesc.addressW = Renderer::TextureAddressMode::CLAMP;
    samplerDesc.shaderVisibility = Renderer::ShaderVisibility::PIXEL;
    samplerDesc.comparisonEnabled = true;
    samplerDesc.comparisonFunc = Renderer::ComparisonFunc::GREATER;

    _shadowCmpSampler = _renderer->CreateSampler(samplerDesc);
    resources.lightDescriptorSet.Bind("_shadowCmpSampler"_h, _shadowCmpSampler);

    samplerDesc.filter = Renderer::SamplerFilter::MIN_MAG_MIP_POINT;
    samplerDesc.addressU = Renderer::TextureAddressMode::CLAMP;
    samplerDesc.addressV = Renderer::TextureAddressMode::CLAMP;
    samplerDesc.comparisonEnabled = false;

    _shadowPointClampSampler = _renderer->CreateSampler(samplerDesc);
    resources.lightDescriptorSet.Bind("_shadowPointClampSampler"_h, _shadowPointClampSampler);

    // Depth min/max reduction for SDSM cascade fitting
    {
        Renderer::ComputePipelineDesc pipelineDesc;
        pipelineDesc.debugName = "Shadow Depth MinMax";

        Renderer::ComputeShaderDesc shaderDesc;
        shaderDesc.shaderEntry = _gameRenderer->GetShaderEntry("Shadows/DepthMinMax.cs"_h, "Shadows/DepthMinMax.cs");
        pipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);

        _depthMinMaxPipeline = _renderer->CreatePipeline(pipelineDesc);

        _depthMinMaxDescriptorSet.RegisterPipeline(_renderer, _depthMinMaxPipeline);
        _depthMinMaxDescriptorSet.Init(_renderer);

        Renderer::BufferDesc bufferDesc;
        bufferDesc.name = "ShadowDepthMinMax";
        bufferDesc.size = sizeof(u32) * 2;
        bufferDesc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION | Renderer::BufferUsage::TRANSFER_SOURCE;

        _depthMinMaxBuffer = _renderer->CreateAndFillBuffer(_depthMinMaxBuffer, bufferDesc, [](void* mappedMemory, size_t size)
        {
            u32* values = static_cast<u32*>(mappedMemory);
            values[0] = 0xFFFFFFFF; // Min depth bits sentinel
            values[1] = 0;          // Max depth bits sentinel
        });
        _depthMinMaxDescriptorSet.Bind("_depthMinMax"_h, _depthMinMaxBuffer);

        bufferDesc.name = "ShadowDepthMinMaxReadBack";
        bufferDesc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;
        bufferDesc.cpuAccess = Renderer::BufferCPUAccess::ReadOnly;
        _depthMinMaxReadBackBuffer = _renderer->CreateBuffer(_depthMinMaxReadBackBuffer, bufferDesc);
    }

    // SDSM cascade fitting
    {
        Renderer::ComputePipelineDesc pipelineDesc;
        pipelineDesc.debugName = "Shadow Cascade Fit Range";

        Renderer::ComputeShaderDesc shaderDesc;
        shaderDesc.shaderEntry = _gameRenderer->GetShaderEntry("Shadows/CascadeFitRange.cs"_h, "Shadows/CascadeFitRange.cs");
        pipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);

        _cascadeFitRangePipeline = _renderer->CreatePipeline(pipelineDesc);

        _cascadeFitRangeDescriptorSet.RegisterPipeline(_renderer, _cascadeFitRangePipeline);
        _cascadeFitRangeDescriptorSet.Init(_renderer);

        pipelineDesc.debugName = "Shadow Cascade XY Reduce";
        shaderDesc.shaderEntry = _gameRenderer->GetShaderEntry("Shadows/CascadeXYReduce.cs"_h, "Shadows/CascadeXYReduce.cs");
        pipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);

        _cascadeXYReducePipeline = _renderer->CreatePipeline(pipelineDesc);

        _cascadeXYReduceDescriptorSet.RegisterPipeline(_renderer, _cascadeXYReducePipeline);
        _cascadeXYReduceDescriptorSet.Init(_renderer);

        pipelineDesc.debugName = "Shadow Cascade Fit Cameras";
        shaderDesc.shaderEntry = _gameRenderer->GetShaderEntry("Shadows/CascadeFitCameras.cs"_h, "Shadows/CascadeFitCameras.cs");
        pipelineDesc.computeShader = _renderer->LoadShader(shaderDesc);

        _cascadeFitCamerasPipeline = _renderer->CreatePipeline(pipelineDesc);

        _cascadeFitCamerasDescriptorSet.RegisterPipeline(_renderer, _cascadeFitCamerasPipeline);
        _cascadeFitCamerasDescriptorSet.Init(_renderer);

        Renderer::BufferDesc bufferDesc;
        bufferDesc.name = "ShadowSDSMData";
        bufferDesc.size = sizeof(f32) * SDSM_DATA_FLOAT_COUNT;
        bufferDesc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION | Renderer::BufferUsage::TRANSFER_SOURCE;

        _sdsmDataBuffer = _renderer->CreateAndFillBuffer(_sdsmDataBuffer, bufferDesc, [](void* mappedMemory, size_t size)
        {
            memset(mappedMemory, 0, size); // smoothedMax <= smoothedMin marks the state uninitialized
        });

        Renderer::BufferDesc boundsDesc;
        boundsDesc.name = "ShadowCascadeBounds";
        boundsDesc.size = sizeof(u32) * 48; // Encoded light-space mins [0..23] and maxs [24..47], 3 axes per cascade
        boundsDesc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;
        _cascadeBoundsBuffer = _renderer->CreateAndFillBuffer(_cascadeBoundsBuffer, boundsDesc, [](void* mappedMemory, size_t size)
        {
            u32* values = static_cast<u32*>(mappedMemory);
            for (u32 i = 0; i < 24; i++) values[i] = 0xFFFFFFFF;
            for (u32 i = 24; i < 48; i++) values[i] = 0;
        });

        _cascadeFitRangeDescriptorSet.Bind("_sdsmData"_h, _sdsmDataBuffer);
        _cascadeFitRangeDescriptorSet.Bind("_depthMinMax"_h, _depthMinMaxBuffer);
        _cascadeXYReduceDescriptorSet.Bind("_sdsmData"_h, _sdsmDataBuffer);
        _cascadeXYReduceDescriptorSet.Bind("_cascadeBounds"_h, _cascadeBoundsBuffer);
        _cascadeFitCamerasDescriptorSet.Bind("_sdsmData"_h, _sdsmDataBuffer);
        _cascadeFitCamerasDescriptorSet.Bind("_cascadeBounds"_h, _cascadeBoundsBuffer);
        // _srcCameras / _rwCameras / _depth are bound per frame in AddCascadeFitPass, those resources can be recreated

        bufferDesc.name = "ShadowSDSMDataReadBack";
        bufferDesc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;
        bufferDesc.cpuAccess = Renderer::BufferCPUAccess::ReadOnly;
        _sdsmDataReadBackBuffer = _renderer->CreateBuffer(_sdsmDataReadBackBuffer, bufferDesc);

        bufferDesc.name = "ShadowCascadeCamerasReadBack";
        bufferDesc.size = sizeof(Camera) * Renderer::Settings::MAX_SHADOW_CASCADES;
        bufferDesc.usage = Renderer::BufferUsage::STORAGE_BUFFER | Renderer::BufferUsage::TRANSFER_DESTINATION;
        bufferDesc.cpuAccess = Renderer::BufferCPUAccess::ReadOnly;
        _cascadeCamerasReadBackBuffer = _renderer->CreateBuffer(_cascadeCamerasReadBackBuffer, bufferDesc);
    }
}