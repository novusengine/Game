#pragma once
#include "Game/Rendering/CullingResources.h"

#include <Base/Types.h>
#include <Base/Math/Geometry.h>

#include <FileFormat/Warcraft/Shared.h>
#include <FileFormat/Novus/Model/ComplexModel.h>

#include <Renderer/DescriptorSet.h>
#include <Renderer/FrameResource.h>
#include <Renderer/GPUVector.h>

class DebugRenderer;
struct RenderResources;

namespace Renderer
{
	class Renderer;
	class RenderGraph;
	class RenderGraphResources;
}

struct DrawParams;

class CulledRenderer
{
protected:
	CulledRenderer(Renderer::Renderer* renderer, DebugRenderer* debugRenderer);
	~CulledRenderer();

	void Update(f32 deltaTime);
	void Clear();

	struct PassParams
	{
		std::string passName = "";

		RenderResources* renderResources;
		Renderer::RenderGraphResources* graphResources;
		Renderer::CommandList* commandList;
		CullingResourcesBase* cullingResources;
		
		u8 frameIndex;
	};

	struct OccluderPassParams : public PassParams
	{
		Renderer::RenderPassMutableResource visibilityBuffer;
		Renderer::RenderPassMutableResource depth;

		bool enableDrawing = false; // Allows us to do everything but the actual drawcall, for debugging
		bool disableTwoStepCulling = false;
	};
	void OccluderPass(OccluderPassParams& params);

	struct CullingPassParams : public PassParams
	{
		u32 numCascades = 0;
		bool occlusionCull = true;

		u32 instanceIDOffset = 0;
		u32 modelIDOffset = 0;
		u32 drawCallDataSize = 0;
	};
	void CullingPass(CullingPassParams& params);

	struct GeometryPassParams : public PassParams
	{
		Renderer::RenderPassMutableResource visibilityBuffer;
		Renderer::RenderPassMutableResource depth;

		bool enableDrawing = false; // Allows us to do everything but the actual drawcall, for debugging
		bool cullingEnabled = false;
		u32 numCascades = 0;
	};
	void GeometryPass(GeometryPassParams& params);

	void SyncToGPU();
	void SetupCullingResource(CullingResourcesBase& resources);

protected:
	struct DrawParams
	{
		bool cullingEnabled = false;
		bool shadowPass = false;
		u32 shadowCascade = 0;
		Renderer::RenderPassMutableResource visibilityBuffer;
		Renderer::RenderPassMutableResource depth;
		Renderer::BufferID argumentBuffer;
		Renderer::BufferID drawCountBuffer;
		u32 drawCountIndex = 0;
		u32 numMaxDrawCalls = 0;
	};

private:
	void CreatePermanentResources();

	virtual void Draw(const RenderResources& resources, u8 frameIndex, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, const DrawParams& params) = 0;

private:
	Renderer::Renderer* _renderer = nullptr;
	DebugRenderer* _debugRenderer = nullptr;
	
protected:
	Renderer::GPUVector<Model::ComplexModel::CullingData> _cullingDatas;

	Renderer::SamplerID _occlusionSampler;
};