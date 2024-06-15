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

AutoCVar_Int CVAR_ShadowsEnabled(CVarCategory::Client | CVarCategory::Rendering, "shadowEnabled", "enable shadows", 0, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_ShadowsFrozen(CVarCategory::Client | CVarCategory::Rendering, "shadowFreeze", "freeze shadows", 0, CVarFlags::EditCheckbox);

AutoCVar_Int CVAR_ShadowCascadeNum(CVarCategory::Client | CVarCategory::Rendering, "shadowCascadeNum", "number of cascades", 0);

AutoCVar_Int CVAR_ShadowsDebugMatrices(CVarCategory::Client | CVarCategory::Rendering, "shadowDebugMatrices", "debug shadow matrices by applying them to the camera", 0, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_ShadowsDebugMatrixIndex(CVarCategory::Client | CVarCategory::Rendering, "shadowDebugMatricesIndex", "index of the cascade to debug", 0);

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
	/*Camera* camera = ServiceLocator::GetCamera();

	const u32 numCascades = CVAR_ShadowCascadeNum.Get();

	u32 colors[4] = {
		0xff0000ff, // R
		0xff00ff00, // G
		0xffff0000, // B
		0xff00ffff // Yellow
	};

	const u32 debugMatrixIndex = CVAR_ShadowsDebugMatrixIndex.Get();
	for (u32 i = 0; i < numCascades; i++)
	{
		//const u32 debugMatrixIndex = i;

		const vec3& near0 = _cascadeDebugInformation[debugMatrixIndex].frustumCorners[0];
		const vec3& near1 = _cascadeDebugInformation[debugMatrixIndex].frustumCorners[1];
		const vec3& near2 = _cascadeDebugInformation[debugMatrixIndex].frustumCorners[2];
		const vec3& near3 = _cascadeDebugInformation[debugMatrixIndex].frustumCorners[3];

		const vec3& far0 = _cascadeDebugInformation[debugMatrixIndex].frustumCorners[4];
		const vec3& far1 = _cascadeDebugInformation[debugMatrixIndex].frustumCorners[5];
		const vec3& far2 = _cascadeDebugInformation[debugMatrixIndex].frustumCorners[6];
		const vec3& far3 = _cascadeDebugInformation[debugMatrixIndex].frustumCorners[7];

		// Near plane
		_debugRenderer->DrawLine3D(near0, near1, colors[i]);
		_debugRenderer->DrawLine3D(near1, near2, colors[i]);
		_debugRenderer->DrawLine3D(near2, near3, colors[i]);
		_debugRenderer->DrawLine3D(near3, near0, colors[i]);

		// Far plane
		_debugRenderer->DrawLine3D(far0, far1, colors[i]);
		_debugRenderer->DrawLine3D(far1, far2, colors[i]);
		_debugRenderer->DrawLine3D(far2, far3, colors[i]);
		_debugRenderer->DrawLine3D(far3, far0, colors[i]);

		// Edges
		_debugRenderer->DrawLine3D(near0, far0, colors[i]);
		_debugRenderer->DrawLine3D(near1, far1, colors[i]);
		_debugRenderer->DrawLine3D(near2, far2, colors[i]);
		_debugRenderer->DrawLine3D(near3, far3, colors[i]);

		// Draw cascade culling plane normals
		f32 scale = 10000.0f;*/

		/*if (i == debugMatrixIndex)
		{
			vec3 pos = _cascadeDebugInformation[i].frustumPlanePos;

			vec3 left = vec3(_cascadeDebugInformation[i].frustumPlanes[(size_t)FrustumPlane::Left]) * _cascadeDebugInformation[i].frustumPlanes[(size_t)FrustumPlane::Left].w * scale;
			vec3 right = vec3(_cascadeDebugInformation[i].frustumPlanes[(size_t)FrustumPlane::Right]) * _cascadeDebugInformation[i].frustumPlanes[(size_t)FrustumPlane::Right].w * scale;
			vec3 bottom = vec3(_cascadeDebugInformation[i].frustumPlanes[(size_t)FrustumPlane::Bottom]) * _cascadeDebugInformation[i].frustumPlanes[(size_t)FrustumPlane::Bottom].w * scale;
			vec3 top = vec3(_cascadeDebugInformation[i].frustumPlanes[(size_t)FrustumPlane::Top]) * _cascadeDebugInformation[i].frustumPlanes[(size_t)FrustumPlane::Top].w * scale;
			vec3 near = vec3(_cascadeDebugInformation[i].frustumPlanes[(size_t)FrustumPlane::Near]) * _cascadeDebugInformation[i].frustumPlanes[(size_t)FrustumPlane::Near].w * scale;
			vec3 far = vec3(_cascadeDebugInformation[i].frustumPlanes[(size_t)FrustumPlane::Far]) * _cascadeDebugInformation[i].frustumPlanes[(size_t)FrustumPlane::Far].w * scale;

			_debugRenderer->DrawLine3D(pos, pos + left, 0xFF0000FF);
			_debugRenderer->DrawLine3D(pos, pos + right, 0xFF000080);
			_debugRenderer->DrawLine3D(pos, pos + bottom, 0xFF00FF00);
			_debugRenderer->DrawLine3D(pos, pos + top, 0xFF008000);
			_debugRenderer->DrawLine3D(pos, pos + near, 0xFFFF0000);
			_debugRenderer->DrawLine3D(pos, pos + far, 0xFF800000);
		}
	}*/
}

void ShadowRenderer::AddShadowPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
	const bool shadowsFrozen = CVAR_ShadowsFrozen.Get();
	if (shadowsFrozen)
		return;

	struct ShadowPassData
	{
		Renderer::DepthImageMutableResource shadowDepthCascades[Renderer::Settings::MAX_SHADOW_CASCADES];

		Renderer::DescriptorSetResource shadowDescriptorSet;
	};

	CVarSystem* cvarSystem = CVarSystem::Get();
	const u32 numCascades = static_cast<u32>(*cvarSystem->GetIntCVar(CVarCategory::Client | CVarCategory::Rendering, "shadowCascadeNum"));

	if (numCascades == 0)
		return;

	const bool shadowsEnabled = CVAR_ShadowsEnabled.Get();
	if (!shadowsEnabled)
	{
		renderGraph->AddPass<ShadowPassData>("Shadows Pass",
			[=](ShadowPassData& data, Renderer::RenderGraphBuilder& builder)
			{
				for (u32 i = 0; i < numCascades; i++)
				{
					data.shadowDepthCascades[i] = builder.Write(resources.shadowDepthCascades[i], Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::CLEAR);
				}

				return true; // Return true from setup to enable this pass, return false to disable it
			},
			[=](ShadowPassData& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList)
			{
				// This is literally just here to clear the shadowDepth...
			});

		return;
	}

	const bool debugMatrices = CVAR_ShadowsDebugMatrices.Get();
	if (debugMatrices)
	{
		const u32 debugMatrixIndex = CVAR_ShadowsDebugMatrixIndex.Get();

		//const ViewData& shadowCascadeViewData = _shadowCascadeViewDatas[frameIndex].ReadGet(debugMatrixIndex);

		//resources.viewConstantBuffer->resource = shadowCascadeViewData;
		//resources.viewConstantBuffer->Apply(frameIndex);
	}

	const bool terrainCastShadows = CVAR_TerrainCastShadow.Get();
	const bool modelsCastShadows = CVAR_ModelsCastShadow.Get();

	renderGraph->AddPass<ShadowPassData>("Shadows Pass",
		[this, &resources, numCascades](ShadowPassData& data, Renderer::RenderGraphBuilder& builder)
		{
#if TIMESLICED_CASCADES
			data.shadowDepthCascades[_currentCascade] = builder.Write(resources.shadowDepthCascades[_currentCascade], Renderer::PipelineType::GRAPHICS, Renderer::RenderGraphBuilder::LoadMode::CLEAR);
#else
			for (u32 i = 0; i < numCascades; i++)
			{
				data.shadowDepthCascades[i] = builder.Write(resources.shadowDepthCascades[i], Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::CLEAR);
			}
#endif
			data.shadowDescriptorSet = builder.Use(resources.shadowDescriptorSet);

			return true; // Return true from setup to enable this pass, return false to disable it
		},
		[=](ShadowPassData& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList)
		{
			commandList.PushMarker("Init", Color::White);

			Renderer::DepthImageMutableResource cascadeDepthResource;
			for (u32 i = 0; i < Renderer::Settings::MAX_SHADOW_CASCADES; i++)
			{
				if (i < numCascades)
				{
					cascadeDepthResource = data.shadowDepthCascades[i];
				}

				data.shadowDescriptorSet.BindArray("_shadowCascadeRTs", cascadeDepthResource, i);
			}

			uvec2 shadowDepthDimensions = _renderer->GetImageDimensions(resources.shadowDepthCascades[0]);

			f32 biasConstantFactor = CVAR_ShadowDepthBiasConstantFactor.GetFloat();
			f32 biasClamp = CVAR_ShadowDepthBiasClamp.GetFloat();
			f32 biasSlopeFactor = CVAR_ShadowDepthBiasSlopeFactor.GetFloat();

			commandList.SetViewport(0, 0, static_cast<f32>(shadowDepthDimensions.x), static_cast<f32>(shadowDepthDimensions.y), 0.0f, 1.0f);
			commandList.SetScissorRect(0, shadowDepthDimensions.x, 0, shadowDepthDimensions.y);
			commandList.SetDepthBias(biasConstantFactor, biasClamp, biasSlopeFactor);

			commandList.PopMarker();

			if (terrainCastShadows)
			{
				/*const u32 numDrawCalls = _terrainRenderer->GetNumDrawCalls();
				commandList.PushMarker("Terrain Draw " + std::to_string(numDrawCalls), Color::White);

				const bool cullingEnabled = *CVarSystem::Get()->GetIntCVar("terrain.culling.Enable") == 1;

				TerrainRenderer::DrawParams drawParams;
				drawParams.shadowPass = true;
				drawParams.cullingEnabled = cullingEnabled;
				drawParams.argumentBuffer = _terrainRenderer->_argumentBuffer;

#if TIMESLICED_CASCADES
				commandList.PushMarker("Cascade " + std::to_string(_currentCascade), Color::White);
				drawParams.depth = data.shadowDepthCascades[_currentCascade];
				drawParams.shadowCascade = _currentCascade;

				_terrainRenderer->Draw(resources, frameIndex, graphResources, commandList, drawParams);
				commandList.PopMarker();
#else
				for (u32 i = 0; i < numCascades; i++)
				{
					commandList.PushMarker("Cascade " + std::to_string(i), Color::White);
					drawParams.instanceBuffer = cullingEnabled ? _terrainRenderer->_culledInstanceBuffer[i + 1] : _terrainRenderer->_instances.GetBuffer();
					drawParams.depth = data.shadowDepthCascades[i];
					drawParams.shadowCascade = i;
					drawParams.argumentsIndex = i + 1;

					_terrainRenderer->Draw(resources, frameIndex, graphResources, commandList, drawParams);
					commandList.PopMarker();
				}
#endif

				commandList.PopMarker();*/
			}

			if (modelsCastShadows)
			{
				/*const u32 numDrawCalls = _mapObjectRenderer->GetNumDrawCalls();
				commandList.PushMarker("MapObject Draw " + std::to_string(numDrawCalls), Color::White);

				const bool cullingEnabled = *CVarSystem::Get()->GetIntCVar("mapObjects.cullEnable") == 1;
				const bool deterministicOrder = *CVarSystem::Get()->GetIntCVar("mapObjects.deterministicOrder") == 1;

				MapObjectRenderer::DrawParams drawParams;
				drawParams.shadowPass = true;
				drawParams.drawCountBuffer = _mapObjectRenderer->_drawCountBuffer;
				drawParams.numMaxDrawCalls = numDrawCalls;

#if TIMESLICED_CASCADES
				commandList.PushMarker("Cascade " + std::to_string(_currentCascade), Color::White);
				drawParams.depth = data.shadowDepthCascades[_currentCascade];
				drawParams.shadowCascade = _currentCascade;

				_mapObjectRenderer->Draw(resources, frameIndex, graphResources, commandList, drawParams);
				commandList.PopMarker();
#else
				for (u32 i = 0; i < numCascades; i++)
				{
					commandList.PushMarker("Cascade " + std::to_string(i), Color::White);

					drawParams.depth = data.shadowDepthCascades[i];
					drawParams.argumentBuffer = (cullingEnabled) ? _mapObjectRenderer->_culledDrawCallBuffer[i + 1] : _mapObjectRenderer->_drawCalls.GetBuffer();
					drawParams.shadowCascade = i;
					drawParams.drawCountIndex = i + 1;

					_mapObjectRenderer->Draw(resources, frameIndex, graphResources, commandList, drawParams);
					commandList.PopMarker();
				}
#endif

				commandList.PopMarker();*/
			}

			// Finish by resetting the viewport, scissor and depth bias
			vec2 renderSize = _renderer->GetRenderSize();
			commandList.SetViewport(0, 0, renderSize.x, renderSize.y, 0.0f, 1.0f);
			commandList.SetScissorRect(0, static_cast<u32>(renderSize.x), 0, static_cast<u32>(renderSize.y));
			commandList.SetDepthBias(0, 0, 0);

			_currentCascade = (_currentCascade + 1) % numCascades;
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