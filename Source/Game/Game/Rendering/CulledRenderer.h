#pragma once
#include "Game/Rendering/CullingResources.h"

#include <Base/Types.h>
#include <Base/Math/Geometry.h>

#include <FileFormat/Warcraft/Shared.h>
#include <FileFormat/Novus/Model/ComplexModel.h>

#include <Renderer/DescriptorSetResource.h>
#include <Renderer/FrameResource.h>
#include <Renderer/GPUVector.h>

class DebugRenderer;
struct RenderResources;

namespace Renderer
{
	class Renderer;
	class RenderGraph;
	class RenderGraphResources;
	class DescriptorSetResource;
}

struct DrawParams;

class CulledRenderer
{
protected:
	struct DrawParams
	{
		bool cullingEnabled = false;
		bool shadowPass = false;
		u32 shadowCascade = 0;
		
		Renderer::ImageMutableResource rt0;
		Renderer::ImageMutableResource rt1;
		Renderer::DepthImageMutableResource depth;

		Renderer::BufferMutableResource argumentBuffer;
		Renderer::BufferMutableResource drawCountBuffer;

		Renderer::DescriptorSetResource globalDescriptorSet;
		Renderer::DescriptorSetResource drawDescriptorSet;

		u32 drawCountIndex = 0;
		u32 numMaxDrawCalls = 0;
	};

	CulledRenderer(Renderer::Renderer* renderer, DebugRenderer* debugRenderer);
	~CulledRenderer();

	void Update(f32 deltaTime);
	void Clear();

	struct PassParams
	{
		std::string passName = "";

		Renderer::RenderGraphResources* graphResources;
		Renderer::CommandList* commandList;
		CullingResourcesBase* cullingResources;
		
		u8 frameIndex;
	};

	struct OccluderPassParams : public PassParams
	{
		Renderer::ImageMutableResource rt0;
		Renderer::ImageMutableResource rt1;
		Renderer::DepthImageMutableResource depth;

		Renderer::BufferMutableResource culledDrawCallsBuffer;
		Renderer::BufferMutableResource culledDrawCallsBitMaskBuffer;
		Renderer::BufferMutableResource drawCountBuffer;
		Renderer::BufferMutableResource triangleCountBuffer;
		Renderer::BufferMutableResource drawCountReadBackBuffer;
		Renderer::BufferMutableResource triangleCountReadBackBuffer;

		Renderer::DescriptorSetResource globalDescriptorSet;
		Renderer::DescriptorSetResource occluderFillDescriptorSet;
		Renderer::DescriptorSetResource drawDescriptorSet;

		std::function<void(const DrawParams&)> drawCallback;

		bool enableDrawing = false; // Allows us to do everything but the actual drawcall, for debugging
		bool disableTwoStepCulling = false;
	};
	void OccluderPass(OccluderPassParams& params);

	struct CullingPassParams : public PassParams
	{
		Renderer::ImageResource depthPyramid;

		Renderer::BufferResource prevCulledDrawCallsBitMask;

		Renderer::BufferMutableResource currentCulledDrawCallsBitMask;
		Renderer::BufferMutableResource culledDrawCallsBuffer;
		Renderer::BufferMutableResource drawCountBuffer;
		Renderer::BufferMutableResource triangleCountBuffer;
		Renderer::BufferMutableResource drawCountReadBackBuffer;
		Renderer::BufferMutableResource triangleCountReadBackBuffer;

		Renderer::DescriptorSetResource debugDescriptorSet;
		Renderer::DescriptorSetResource globalDescriptorSet;
		Renderer::DescriptorSetResource cullingDescriptorSet;

		u32 numCascades = 0;
		bool occlusionCull = true;
		bool disableTwoStepCulling = false;
		bool debugDrawColliders = false;

		u32 instanceIDOffset = 0;
		u32 modelIDOffset = 0;
		u32 drawCallDataSize = 0;
	};
	void CullingPass(CullingPassParams& params);

	struct GeometryPassParams : public PassParams
	{
		Renderer::ImageMutableResource rt0;
		Renderer::ImageMutableResource rt1;
		Renderer::DepthImageMutableResource depth;

		Renderer::BufferMutableResource culledDrawCallsBuffer;
		Renderer::BufferMutableResource drawCountBuffer;
		Renderer::BufferMutableResource triangleCountBuffer;
		Renderer::BufferMutableResource drawCountReadBackBuffer;
		Renderer::BufferMutableResource triangleCountReadBackBuffer;

		Renderer::DescriptorSetResource globalDescriptorSet;
		Renderer::DescriptorSetResource drawDescriptorSet;

		std::function<void(const DrawParams&)> drawCallback;

		bool enableDrawing = false; // Allows us to do everything but the actual drawcall, for debugging
		bool cullingEnabled = false;
		u32 numCascades = 0;
	};
	void GeometryPass(GeometryPassParams& params);

	void SyncToGPU();
	void SetupCullingResource(CullingResourcesBase& resources);

private:
	void CreatePermanentResources();

private:
	Renderer::Renderer* _renderer = nullptr;
	DebugRenderer* _debugRenderer = nullptr;
	
protected:
	Renderer::GPUVector<Model::ComplexModel::CullingData> _cullingDatas;

	Renderer::SamplerID _occlusionSampler;
};