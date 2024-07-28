#include "ShadowRenderer.h"
#include <Game/ECS/Components/Camera.h>
#include <Game/ECS/Singletons/ActiveCamera.h>
#include <Game/ECS/Singletons/MapDB.h>
#include <Game/ECS/Util/Transforms.h>
#include <Game/Rendering/Debug/DebugRenderer.h>
#include <Game/Rendering/Terrain/TerrainRenderer.h>
#include <Game/Rendering/Model/ModelRenderer.h>
#include <Game/Rendering/RenderResources.h>
#include <Game/Rendering/Camera.h>
#include <Game/Util/ServiceLocator.h>

#include <Base/CVarSystem/CVarSystem.h>

#include <Renderer/Renderer.h>
#include <Renderer/RenderGraph.h>

#include <limits>
#include <glm/gtc/matrix_transform.hpp>
#include <entt/entt.hpp>

AutoCVar_Int CVAR_ShadowEnabled(CVarCategory::Client | CVarCategory::Rendering, "shadowEnabled", "enable shadows", 0, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_ShadowFrozen(CVarCategory::Client | CVarCategory::Rendering, "shadowFreeze", "freeze shadows", 0, CVarFlags::EditCheckbox);

AutoCVar_Int CVAR_ShadowDebugMatrices(CVarCategory::Client | CVarCategory::Rendering, "shadowDebugMatrices", "debug shadow matrices by applying them to the camera", 0, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_ShadowDebugMatrixIndex(CVarCategory::Client | CVarCategory::Rendering, "shadowDebugMatricesIndex", "index of the cascade to debug", 0);

AutoCVar_Int CVAR_ShadowDrawMatrices(CVarCategory::Client | CVarCategory::Rendering, "shadowDrawMatrices", "debug shadow matrices by debug drawing them", 0, CVarFlags::EditCheckbox);

AutoCVar_Int CVAR_ShadowFilterMode(CVarCategory::Client | CVarCategory::Rendering, "shadowFilterMode", "0: No filtering, 1: Percentage Closer Filtering, 2: Percentage Closer Soft Shadows", 1);
AutoCVar_Float CVAR_ShadowFilterSize(CVarCategory::Client | CVarCategory::Rendering, "shadowFilterSize", "size of the filter used for shadow sampling", 3.0f);
AutoCVar_Float CVAR_ShadowFilterPenumbraSize(CVarCategory::Client | CVarCategory::Rendering, "shadowFilterPenumbraSize", "size of the filter used for penumbra sampling", 3.0f);

AutoCVar_Int CVAR_TerrainCastShadow(CVarCategory::Client | CVarCategory::Rendering, "shadowTerrainCastShadow", "should Terrain cast shadows", 1, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_ModelsCastShadow(CVarCategory::Client | CVarCategory::Rendering, "shadowModelsCastShadow", "should Models cast shadows", 1, CVarFlags::EditCheckbox);

AutoCVar_Float CVAR_ShadowDepthBiasConstantFactor(CVarCategory::Client | CVarCategory::Rendering, "shadowDepthBiasConstant", "constant factor of depth bias to prevent shadow acne", -2.0f);
AutoCVar_Float CVAR_ShadowDepthBiasClamp(CVarCategory::Client | CVarCategory::Rendering, "shadowDepthBiasClamp", "clamp of depth bias to prevent shadow acne", 0.0f);
AutoCVar_Float CVAR_ShadowDepthBiasSlopeFactor(CVarCategory::Client | CVarCategory::Rendering, "shadowDepthBiasSlope", "slope factor of depth bias to prevent shadow acne", -5.0f);

#define TIMESLICED_CASCADES 0

ShadowRenderer::ShadowRenderer(Renderer::Renderer* renderer, DebugRenderer* debugRenderer, TerrainRenderer* terrainRenderer, ModelRenderer* modelRenderer, RenderResources& resources)
	: _renderer(renderer)
	, _debugRenderer(debugRenderer)
	, _terrainRenderer(terrainRenderer)
	, _modelRenderer(modelRenderer)
{
	CreatePermanentResources(resources);
}

ShadowRenderer::~ShadowRenderer()
{
}

void ShadowRenderer::Update(f32 deltaTime, RenderResources& resources)
{
	CVarSystem* cvarSystem = CVarSystem::Get();
	const u32 numCascades = static_cast<u32>(*cvarSystem->GetIntCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowCascadeNum"));

	const bool debugMatrices = CVAR_ShadowDebugMatrices.Get();
	const i32 debugMatrixIndex = CVAR_ShadowDebugMatrixIndex.Get();
	if (debugMatrices && debugMatrixIndex >= 0 && debugMatrixIndex < static_cast<i32>(numCascades))
	{
		std::vector<Camera>& gpuCameras = resources.cameras.Get();
		const Camera& debugCascadeCamera = gpuCameras[debugMatrixIndex + 1]; // +1 because the first camera is the main camera

		Camera& mainCamera = gpuCameras[0];

		mainCamera = debugCascadeCamera;
		resources.cameras.SetDirtyElement(0);
	}

	if (CVAR_ShadowDrawMatrices.Get())
	{
		std::vector<Camera>& gpuCameras = resources.cameras.Get();
		
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
			const Camera& debugCascadeCamera = gpuCameras[i + 1]; // +1 because the first camera is the main camera
			_debugRenderer->DrawFrustum(debugCascadeCamera.worldToClip, colors[i]);
		}
	}
}

void ShadowRenderer::AddShadowPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
	const bool shadowFrozen = CVAR_ShadowFrozen.Get();
	if (shadowFrozen)
		return;

	struct ShadowPassData
	{
		Renderer::DepthImageMutableResource shadowDepthCascades[Renderer::Settings::MAX_SHADOW_CASCADES];

		Renderer::DescriptorSetResource shadowDescriptorSet;
	};

	CVarSystem* cvarSystem = CVarSystem::Get();
	u32 numCascades = static_cast<u32>(*cvarSystem->GetIntCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowCascadeNum"));
	numCascades = std::min(numCascades, static_cast<u32>(resources.shadowDepthCascades.size()));

	if (numCascades == 0)
		return;

	const bool shadowEnabled = CVAR_ShadowEnabled.Get();

	renderGraph->AddPass<ShadowPassData>("Shadow Pass",
		[=, &resources](ShadowPassData& data, Renderer::RenderGraphBuilder& builder)
		{
			for (u32 i = 0; i < numCascades; i++)
			{
				data.shadowDepthCascades[i] = builder.Write(resources.shadowDepthCascades[i], Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::CLEAR);
			}

			data.shadowDescriptorSet = builder.Use(resources.shadowDescriptorSet);

			return true; // Return true from setup to enable this pass, return false to disable it
		},
		[=](ShadowPassData& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList)
		{
			f32 biasConstantFactor = CVAR_ShadowDepthBiasConstantFactor.GetFloat();
			f32 biasClamp = CVAR_ShadowDepthBiasClamp.GetFloat();
			f32 biasSlopeFactor = CVAR_ShadowDepthBiasSlopeFactor.GetFloat();

			commandList.SetDepthBias(biasConstantFactor, biasClamp, biasSlopeFactor);

			Renderer::DepthImageMutableResource cascadeDepthResource;
			for (u32 i = 0; i < Renderer::Settings::MAX_SHADOW_CASCADES; i++)
			{
				if (i < numCascades)
				{
					cascadeDepthResource = data.shadowDepthCascades[i];
				}

				data.shadowDescriptorSet.BindArray("_shadowCascadeRTs", cascadeDepthResource, i);
			}
		});
}

void ShadowRenderer::CreatePermanentResources(RenderResources& resources)
{
	Renderer::SamplerDesc samplerDesc;
	samplerDesc.enabled = true;
	samplerDesc.filter = Renderer::SamplerFilter::MIN_MAG_MIP_LINEAR;
	samplerDesc.addressU = Renderer::TextureAddressMode::WRAP;
	samplerDesc.addressV = Renderer::TextureAddressMode::WRAP;
	samplerDesc.addressW = Renderer::TextureAddressMode::CLAMP;
	samplerDesc.shaderVisibility = Renderer::ShaderVisibility::PIXEL;
	samplerDesc.comparisonEnabled = true;
	samplerDesc.comparisonFunc = Renderer::ComparisonFunc::GREATER;

	_shadowCmpSampler = _renderer->CreateSampler(samplerDesc);
	resources.shadowDescriptorSet.Bind("_shadowCmpSampler"_h, _shadowCmpSampler);

	samplerDesc.filter = Renderer::SamplerFilter::MIN_MAG_MIP_POINT;
	samplerDesc.addressU = Renderer::TextureAddressMode::CLAMP;
	samplerDesc.addressV = Renderer::TextureAddressMode::CLAMP;
	samplerDesc.comparisonEnabled = false;

	_shadowPointClampSampler = _renderer->CreateSampler(samplerDesc);
	resources.shadowDescriptorSet.Bind("_shadowPointClampSampler"_h, _shadowPointClampSampler);
}