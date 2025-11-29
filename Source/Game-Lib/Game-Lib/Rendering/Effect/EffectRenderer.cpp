#include "EffectRenderer.h"

#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/Rendering/Debug/DebugRenderer.h"
#include "Game-Lib/Rendering/RenderResources.h"
#include "Game-Lib/Util/PhysicsUtil.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/CVarSystem/CVarSystem.h>

#include <Renderer/Renderer.h>
#include <Renderer/RenderGraph.h>

#include <entt/entt.hpp>

AutoCVar_ShowFlag CVAR_EnablePostProcessing(CVarCategory::Client | CVarCategory::Rendering, "enablePostProcessing", "Enable post processing effects", ShowFlag::ENABLED, CVarFlags::None);

// SSAO
AutoCVar_ShowFlag CVAR_EnableSSAO(CVarCategory::Client | CVarCategory::Rendering, "enableSSAO", "Enable screen space ambient occlusion", ShowFlag::ENABLED, CVarFlags::None);
AutoCVar_Float CVAR_SsaoRadius(CVarCategory::Client | CVarCategory::Rendering, "ssaoRadius", "[0.0,  ~ ] World (view) space size of the occlusion sphere", 1.2f, CVarFlags::EditFloatDrag);
AutoCVar_Float CVAR_SsaoShadowMultiplier(CVarCategory::Client | CVarCategory::Rendering, "ssaoShadowMultiplier", "[0.0, 5.0] Effect strength linear multiplier", 1.0f, CVarFlags::EditFloatDrag);
AutoCVar_Float CVAR_SsaoShadowPower(CVarCategory::Client | CVarCategory::Rendering, "ssaoShadowPower", "[0.5, 5.0] Effect strength pow modifier", 1.50f, CVarFlags::EditFloatDrag);
AutoCVar_Float CVAR_SsaoShadowClamp(CVarCategory::Client | CVarCategory::Rendering, "ssaoShadowClamp", "[0.0, 1.0] Effect max limit (applied after multiplier but before blur)", 0.98f, CVarFlags::EditFloatDrag);
AutoCVar_Float CVAR_SsaoHorizonAngleThreshold(CVarCategory::Client | CVarCategory::Rendering, "ssaoHorizonAngleThreshold", "[0.0, 0.2] Limits self-shadowing", 0.06f, CVarFlags::EditFloatDrag);
AutoCVar_Float CVAR_SsaoFadeOutFrom(CVarCategory::Client | CVarCategory::Rendering, "ssaoFadeOutFrom", "[0.0,  ~ ] Distance to start fading out the effect", 5000.0f, CVarFlags::EditFloatDrag);
AutoCVar_Float CVAR_SsaoFadeOutTo(CVarCategory::Client | CVarCategory::Rendering, "ssaoFadeOutTo", "[0.0,  ~ ] Distance at which the effect is faded out", 8000.0f, CVarFlags::EditFloatDrag);
AutoCVar_Int CVAR_SsaoQualityLevel(CVarCategory::Client | CVarCategory::Rendering, "ssaoQualityLevel", "[0, 4] Effect quality, affects number of taps etc", 4, CVarFlags::None);
AutoCVar_Float CVAR_SsaoAdaptiveQualityLimit(CVarCategory::Client | CVarCategory::Rendering, "ssaoAdaptiveQualityLimit", "[0.0, 1.0] (only for quality level 4)", 0.45f, CVarFlags::EditFloatDrag);
AutoCVar_Int CVAR_SsaoBlurPassCount(CVarCategory::Client | CVarCategory::Rendering, "ssaoBlurPassCount", "[0, 8] Number of edge-sensitive smart blur passes to apply", 2, CVarFlags::None);
AutoCVar_Float CVAR_SsaoSharpness(CVarCategory::Client | CVarCategory::Rendering, "ssaoSharpness", "[0.0, 1.0] How much to bleed over edges; 1: not at all, 0.5: half-half; 0.0: completely ignore edges", 0.98f, CVarFlags::EditFloatDrag);
AutoCVar_Float CVAR_SsaoTemporalSupersamplingAngleOffset(CVarCategory::Client | CVarCategory::Rendering, "ssaoTemporalSupersamplingAngleOffset", "[0.0,  PI] Used to rotate sampling kernel", 0.0f, CVarFlags::EditFloatDrag);
AutoCVar_Float CVAR_SsaoTemporalSupersamplingRadiusOffset(CVarCategory::Client | CVarCategory::Rendering, "ssaoTemporalSupersamplingRadiusOffset", "[0.0, 2.0] Used to scale sampling kernel", 0.0f, CVarFlags::EditFloatDrag);
AutoCVar_Float CVAR_SsaoDetailShadowStrength(CVarCategory::Client | CVarCategory::Rendering, "ssaoDetailShadowStrength", "[0.0, 5.0] Used for high-res detail AO using neighboring depth pixels: adds a lot of detail but also reduces temporal stability (adds aliasing)", 0.5f, CVarFlags::EditFloatDrag);
AutoCVar_Float CVAR_SsaoBilateralSigmaSquared(CVarCategory::Client | CVarCategory::Rendering, "ssaoBilateralSigmaSquared", "[0.0,  ~ ] Sigma squared value for use in bilateral upsampler giving Gaussian blur term. Should be greater than 0.0", 5.0f, CVarFlags::EditFloatDrag);
AutoCVar_Float CVAR_SsaoBilateralSimilarityDistanceSigma(CVarCategory::Client | CVarCategory::Rendering, "ssaoBilateralSimilarityDistanceSigma", "[0.0,  ~ ] Sigma squared value for use in bilateral upsampler giving similarity weighting for neighbouring pixels. Should be greater than 0.0", 0.01f, CVarFlags::EditFloatDrag);

EffectRenderer::EffectRenderer(Renderer::Renderer* renderer, GameRenderer* gameRenderer)
    : _renderer(renderer)
    , _gameRenderer(gameRenderer)
{
    CreatePermanentResources();
}

EffectRenderer::~EffectRenderer()
{

}

void EffectRenderer::Update(f32 deltaTime)
{
    ZoneScoped;
    /*vec2 renderSize = _renderer->GetRenderSize();
    if (_lastRenderSize != renderSize)
    {
        _lastRenderSize = renderSize;

        if (_cacaoContext != nullptr)
        {
            _renderer->FlushGPU();
            ffxCacaoContextDestroy(_cacaoContext);
            _cacaoContext = nullptr;
        }

        if (renderSize.x == 1 || renderSize.y == 1)
            return;

        FfxCacaoContextDescription cacaoDescription = {};
        const size_t scratchBufferSize = _renderer->ffxGetScratchMemorySize(FFX_CACAO_CONTEXT_COUNT * 2);
        void* scratchBuffer = malloc(scratchBufferSize);
        _renderer->ffxGetInterface(&cacaoDescription.backendInterface, scratchBuffer, scratchBufferSize, FFX_CACAO_CONTEXT_COUNT * 2);

        cacaoDescription.width = static_cast<u32>(renderSize.x);
        cacaoDescription.height = static_cast<u32>(renderSize.y);
        cacaoDescription.useDownsampledSsao = false;

        _cacaoContext = new FfxCacaoContext();
        FfxErrorCode code = ffxCacaoContextCreate(_cacaoContext, &cacaoDescription);
        if (code != FfxErrorCodes::FFX_OK)
        {
            NC_LOG_CRITICAL("Failed to create CACAO context");
            return;
        }
    }

    _cacaoSettings->radius = CVAR_SsaoRadius.GetFloat();
    _cacaoSettings->shadowMultiplier = CVAR_SsaoShadowMultiplier.GetFloat();
    _cacaoSettings->shadowPower = CVAR_SsaoShadowPower.GetFloat();
    _cacaoSettings->shadowClamp = CVAR_SsaoShadowClamp.GetFloat();
    _cacaoSettings->horizonAngleThreshold = CVAR_SsaoHorizonAngleThreshold.GetFloat();
    _cacaoSettings->fadeOutFrom = CVAR_SsaoFadeOutFrom.GetFloat();
    _cacaoSettings->fadeOutTo = CVAR_SsaoFadeOutTo.GetFloat();
    _cacaoSettings->qualityLevel = static_cast<FfxCacaoQuality>(CVAR_SsaoQualityLevel.Get());
    _cacaoSettings->adaptiveQualityLimit = CVAR_SsaoAdaptiveQualityLimit.GetFloat();
    _cacaoSettings->blurPassCount = static_cast<u32>(CVAR_SsaoBlurPassCount.Get());
    _cacaoSettings->sharpness = CVAR_SsaoSharpness.GetFloat();
    _cacaoSettings->temporalSupersamplingAngleOffset = CVAR_SsaoTemporalSupersamplingAngleOffset.GetFloat();
    _cacaoSettings->temporalSupersamplingRadiusOffset = CVAR_SsaoTemporalSupersamplingRadiusOffset.GetFloat();
    _cacaoSettings->detailShadowStrength = CVAR_SsaoDetailShadowStrength.GetFloat();
    _cacaoSettings->bilateralSigmaSquared = CVAR_SsaoBilateralSigmaSquared.GetFloat();
    _cacaoSettings->bilateralSimilarityDistanceSigma = CVAR_SsaoBilateralSimilarityDistanceSigma.GetFloat();*/
}

void EffectRenderer::AddSSAOPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    /*if (CVAR_EnablePostProcessing.Get() == ShowFlag::DISABLED)
        return;

    bool enableSSAO = CVAR_EnableSSAO.Get() == ShowFlag::ENABLED && _cacaoContext != nullptr;

    if (enableSSAO)
    {
        const std::vector<Camera>& cameras = resources.cameras.Get();
        const Camera& camera = cameras[0];

        // Get Projection matrix
        _proj = camera.viewToClip;

        // Get normalWorldToView matrix
        _normalsWorldToView = glm::transpose(camera.worldToView);

        struct SSAOPassData
        {
            Renderer::DepthImageResource depth;
            Renderer::ImageResource packedNormals;
            Renderer::ImageMutableResource output;
        };

        renderGraph->AddPass<SSAOPassData>("SSAO Pass",
            [this, &resources](SSAOPassData& data, Renderer::RenderGraphBuilder& builder) // Setup
            {
                data.depth = builder.Read(resources.depth, Renderer::PipelineType::COMPUTE);
                data.packedNormals = builder.Read(resources.packedNormals, Renderer::PipelineType::COMPUTE);
                data.output = builder.Write(resources.ssaoTarget, Renderer::PipelineType::COMPUTE, Renderer::LoadMode::LOAD);

                return true; // Return true from setup to enable this pass, return false to disable it
            },
            [this, &resources, frameIndex](SSAOPassData& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
            {
                GPU_SCOPED_PROFILER_ZONE(commandList, SSAOPass);

                FfxErrorCode errorCode = ffxCacaoUpdateSettings(_cacaoContext, _cacaoSettings, false);
                if (errorCode != FfxErrorCodes::FFX_OK)
                {
                    NC_LOG_CRITICAL("Failed to update CACAO settings");
                    return;
                }

                // Not sure about these
                const f32 normalUnpackMul = 2.0f;
                const f32 normalUnpackAdd = -1.0f;
                commandList.DispatchCacao(_cacaoContext, data.depth, data.packedNormals, data.output, &_proj, &_normalsWorldToView, normalUnpackMul, normalUnpackAdd);
            });
    }*/
    
}

void EffectRenderer::CreatePermanentResources()
{
    /*_cacaoSettings = new FfxCacaoSettings();
    memcpy(_cacaoSettings, &FFX_CACAO_DEFAULT_SETTINGS, sizeof(FfxCacaoSettings));
    _cacaoSettings->generateNormals = false;*/
}