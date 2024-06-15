#pragma once
#include <Base/Types.h>
#include <Renderer/RenderSettings.h>
#include <Renderer/GPUVector.h>
#include <Renderer/Buffer.h>

#include <Renderer/Descriptors/SamplerDesc.h>
#include <Renderer/Descriptors/TextureArrayDesc.h>

namespace Renderer
{
	class Renderer;
	class RenderGraph;
}

struct RenderResources;
class DebugRenderer;
class TerrainRenderer;
class ModelRenderer;

class ShadowRenderer
{
public:
	ShadowRenderer(Renderer::Renderer* renderer, DebugRenderer* debugRenderer, TerrainRenderer* terrainRenderer, ModelRenderer* modelRenderer, RenderResources& resources);
	~ShadowRenderer();

	void Update(f32 deltaTime, RenderResources& resources);

	void AddShadowPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);

private:
	void CreatePermanentResources(RenderResources& resources);

private:
	struct ShadowCascadeDebugInformation
	{
		vec3 frustumCorners[8];
		vec3 frustumPlanePos;
		vec4 frustumPlanes[6];
	};

private:
	Renderer::Renderer* _renderer = nullptr;
	DebugRenderer* _debugRenderer = nullptr;
	TerrainRenderer* _terrainRenderer = nullptr;
	ModelRenderer* _modelRenderer = nullptr;

	Renderer::SamplerID _shadowCmpSampler;
	Renderer::SamplerID _shadowPointClampSampler;

	Renderer::TextureArrayID _shadowDepthTextures;
	u32 _numInitializedShadowDepthImages = 0;

	mat4x4 _cascadeProjectionMatrices[Renderer::Settings::MAX_SHADOW_CASCADES];

	ShadowCascadeDebugInformation _cascadeDebugInformation[Renderer::Settings::MAX_SHADOW_CASCADES];

	vec3 _boundingBoxMin;
	vec3 _boundingBoxMax;

	u32 _currentCascade = 0;
};