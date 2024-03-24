#pragma once
#include "Game/Rendering/CullingResources.h"

#include <Base/Types.h>
#include <Base/Math/Geometry.h>

#include <FileFormat/Shared.h>
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
	class CommandList;
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
		bool isIndexed = true;
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

	template <typename Data>
	void OccluderPassSetup(Data& data, Renderer::RenderGraphBuilder& builder, CullingResourcesIndexedBase* cullingResources, u8 frameIndex)
	{
		using BufferUsage = Renderer::BufferPassUsage;

		data.culledDrawCallsBuffer = builder.Write(cullingResources->GetCulledDrawsBuffer(0), BufferUsage::GRAPHICS | BufferUsage::COMPUTE);
		data.culledDrawCallsBitMaskBuffer = builder.Write(cullingResources->GetCulledDrawCallsBitMaskBuffer(!frameIndex), BufferUsage::TRANSFER | BufferUsage::GRAPHICS | BufferUsage::COMPUTE);

		data.drawCountBuffer = builder.Write(cullingResources->GetDrawCountBuffer(), BufferUsage::TRANSFER | BufferUsage::GRAPHICS | BufferUsage::COMPUTE);
		data.triangleCountBuffer = builder.Write(cullingResources->GetTriangleCountBuffer(), BufferUsage::TRANSFER | BufferUsage::GRAPHICS | BufferUsage::COMPUTE);
		data.drawCountReadBackBuffer = builder.Write(cullingResources->GetOccluderDrawCountReadBackBuffer(), BufferUsage::TRANSFER);
		data.triangleCountReadBackBuffer = builder.Write(cullingResources->GetOccluderTriangleCountReadBackBuffer(), BufferUsage::TRANSFER);

		builder.Read(cullingResources->GetDrawCalls().GetBuffer(), BufferUsage::GRAPHICS | BufferUsage::COMPUTE);
		builder.Read(cullingResources->GetInstanceRefs().GetBuffer(), BufferUsage::GRAPHICS | BufferUsage::COMPUTE);

		data.occluderFillSet = builder.Use(cullingResources->GetOccluderFillDescriptorSet());
		data.createIndirectDescriptorSet = builder.Use(cullingResources->GetCullingDescriptorSet());
		data.drawSet = builder.Use(cullingResources->GetGeometryPassDescriptorSet());
	}

	template <typename Data>
	void OccluderPassSetup(Data& data, Renderer::RenderGraphBuilder& builder, CullingResourcesNonIndexedBase* cullingResources, u8 frameIndex)
	{
		using BufferUsage = Renderer::BufferPassUsage;

		data.culledDrawCallsBuffer = builder.Write(cullingResources->GetCulledDrawsBuffer(0), BufferUsage::GRAPHICS | BufferUsage::COMPUTE);
		data.culledDrawCallsBitMaskBuffer = builder.Write(cullingResources->GetCulledDrawCallsBitMaskBuffer(!frameIndex), BufferUsage::TRANSFER | BufferUsage::GRAPHICS | BufferUsage::COMPUTE);

		data.drawCountBuffer = builder.Write(cullingResources->GetDrawCountBuffer(), BufferUsage::TRANSFER | BufferUsage::GRAPHICS | BufferUsage::COMPUTE);
		data.triangleCountBuffer = builder.Write(cullingResources->GetTriangleCountBuffer(), BufferUsage::TRANSFER | BufferUsage::GRAPHICS | BufferUsage::COMPUTE);
		data.drawCountReadBackBuffer = builder.Write(cullingResources->GetOccluderDrawCountReadBackBuffer(), BufferUsage::TRANSFER);
		data.triangleCountReadBackBuffer = builder.Write(cullingResources->GetOccluderTriangleCountReadBackBuffer(), BufferUsage::TRANSFER);

		builder.Read(cullingResources->GetDrawCalls().GetBuffer(), BufferUsage::GRAPHICS | BufferUsage::COMPUTE);
		builder.Read(cullingResources->GetInstanceRefs().GetBuffer(), BufferUsage::GRAPHICS | BufferUsage::COMPUTE);

		data.occluderFillSet = builder.Use(cullingResources->GetOccluderFillDescriptorSet());
		data.createIndirectDescriptorSet = builder.Use(cullingResources->GetCullingDescriptorSet());
		data.drawSet = builder.Use(cullingResources->GetGeometryPassDescriptorSet());
	}

	struct OccluderPassParams : public PassParams
	{
		Renderer::ImageMutableResource rt0;
		Renderer::ImageMutableResource rt1;
		Renderer::DepthImageMutableResource depth;

		Renderer::BufferMutableResource culledDrawCallsBuffer;
		Renderer::BufferMutableResource culledDrawCallCountBuffer;
		Renderer::BufferMutableResource culledDrawCallsBitMaskBuffer;
		Renderer::BufferMutableResource culledInstanceCountsBuffer;
		
		Renderer::BufferMutableResource drawCountBuffer;
		Renderer::BufferMutableResource triangleCountBuffer;
		Renderer::BufferMutableResource drawCountReadBackBuffer;
		Renderer::BufferMutableResource triangleCountReadBackBuffer;

		Renderer::DescriptorSetResource globalDescriptorSet;
		Renderer::DescriptorSetResource occluderFillDescriptorSet;
		Renderer::DescriptorSetResource createIndirectDescriptorSet;
		Renderer::DescriptorSetResource drawDescriptorSet;

		std::function<void(DrawParams&)> drawCallback;

		u32 baseInstanceLookupOffset = 0;
		u32 drawCallDataSize = 0;

		bool enableDrawing = false; // Allows us to do everything but the actual drawcall, for debugging
		bool disableTwoStepCulling = false;
		bool isIndexed = true;
		bool useInstancedCulling = false;
	};
	void OccluderPass(OccluderPassParams& params);

	template <typename Data>
	void CullingPassSetup(Data& data, Renderer::RenderGraphBuilder& builder, CullingResourcesIndexedBase* cullingResources, u8 frameIndex)
	{
		using BufferUsage = Renderer::BufferPassUsage;

		data.prevCulledDrawCallsBitMask = builder.Read(cullingResources->GetCulledDrawCallsBitMaskBuffer(!frameIndex), BufferUsage::COMPUTE);
		data.currentCulledDrawCallsBitMask = builder.Write(cullingResources->GetCulledDrawCallsBitMaskBuffer(frameIndex), BufferUsage::COMPUTE);
		data.culledDrawCallsBuffer = builder.Write(cullingResources->GetCulledDrawsBuffer(0), BufferUsage::COMPUTE);

		data.drawCountBuffer = builder.Write(cullingResources->GetDrawCountBuffer(), BufferUsage::TRANSFER | BufferUsage::COMPUTE);
		data.triangleCountBuffer = builder.Write(cullingResources->GetTriangleCountBuffer(), BufferUsage::TRANSFER | BufferUsage::COMPUTE);
		data.drawCountReadBackBuffer = builder.Write(cullingResources->GetDrawCountReadBackBuffer(), BufferUsage::TRANSFER);
		data.triangleCountReadBackBuffer = builder.Write(cullingResources->GetTriangleCountReadBackBuffer(), BufferUsage::TRANSFER);

		builder.Read(cullingResources->GetDrawCalls().GetBuffer(), BufferUsage::COMPUTE);
		builder.Read(cullingResources->GetInstanceRefs().GetBuffer(), BufferUsage::COMPUTE);

		data.cullingSet = builder.Use(cullingResources->GetCullingDescriptorSet());
	}

	template <typename Data>
	void CullingPassSetup(Data& data, Renderer::RenderGraphBuilder& builder, CullingResourcesNonIndexedBase* cullingResources, u8 frameIndex)
	{
		using BufferUsage = Renderer::BufferPassUsage;

		data.prevCulledDrawCallsBitMask = builder.Read(cullingResources->GetCulledDrawCallsBitMaskBuffer(!frameIndex), BufferUsage::COMPUTE);
		data.currentCulledDrawCallsBitMask = builder.Write(cullingResources->GetCulledDrawCallsBitMaskBuffer(frameIndex), BufferUsage::COMPUTE);
		data.culledDrawCallsBuffer = builder.Write(cullingResources->GetCulledDrawsBuffer(0), BufferUsage::COMPUTE);
		
		data.drawCountBuffer = builder.Write(cullingResources->GetDrawCountBuffer(), BufferUsage::TRANSFER | BufferUsage::COMPUTE);
		data.triangleCountBuffer = builder.Write(cullingResources->GetTriangleCountBuffer(), BufferUsage::TRANSFER | BufferUsage::COMPUTE);
		data.drawCountReadBackBuffer = builder.Write(cullingResources->GetDrawCountReadBackBuffer(), BufferUsage::TRANSFER);
		data.triangleCountReadBackBuffer = builder.Write(cullingResources->GetTriangleCountReadBackBuffer(), BufferUsage::TRANSFER);

		builder.Read(cullingResources->GetDrawCalls().GetBuffer(), BufferUsage::COMPUTE);
		builder.Read(cullingResources->GetInstanceRefs().GetBuffer(), BufferUsage::COMPUTE);

		data.cullingSet = builder.Use(cullingResources->GetCullingDescriptorSet());
	}

	struct CullingPassParams : public PassParams
	{
		Renderer::ImageResource depthPyramid;

		Renderer::BufferResource prevCulledDrawCallsBitMask;

		Renderer::BufferMutableResource currentCulledDrawCallsBitMask;
		Renderer::BufferMutableResource culledInstanceCountsBuffer;
		Renderer::BufferMutableResource culledDrawCallsBuffer;
		Renderer::BufferMutableResource culledDrawCallCountBuffer;

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

		bool modelIDIsDrawCallID = false;
		bool cullingDataIsWorldspace = false;
		bool debugDrawColliders = false;

		u32 instanceIDOffset = 0;
		u32 modelIDOffset = 0;
		u32 baseInstanceLookupOffset = 0;
		u32 drawCallDataSize = 0;

		bool useInstancedCulling = false;
	};
	void CullingPass(CullingPassParams& params);

	template <typename Data>
	void GeometryPassSetup(Data& data, Renderer::RenderGraphBuilder& builder, CullingResourcesIndexedBase* cullingResources, u8 frameIndex)
	{
		using BufferUsage = Renderer::BufferPassUsage;

		data.culledDrawCallsBuffer = builder.Write(cullingResources->GetCulledDrawsBuffer(0), BufferUsage::GRAPHICS);
		data.drawCountBuffer = builder.Write(cullingResources->GetDrawCountBuffer(), BufferUsage::TRANSFER | BufferUsage::GRAPHICS);
		data.triangleCountBuffer = builder.Write(cullingResources->GetTriangleCountBuffer(), BufferUsage::TRANSFER | BufferUsage::GRAPHICS);
		data.drawCountReadBackBuffer = builder.Write(cullingResources->GetDrawCountReadBackBuffer(), BufferUsage::TRANSFER);
		data.triangleCountReadBackBuffer = builder.Write(cullingResources->GetTriangleCountReadBackBuffer(), BufferUsage::TRANSFER);

		builder.Read(cullingResources->GetDrawCalls().GetBuffer(), BufferUsage::GRAPHICS);
		builder.Read(cullingResources->GetInstanceRefs().GetBuffer(), BufferUsage::GRAPHICS);

		data.drawSet = builder.Use(cullingResources->GetGeometryPassDescriptorSet());
	}

	template <typename Data>
	void GeometryPassSetup(Data& data, Renderer::RenderGraphBuilder& builder, CullingResourcesNonIndexedBase* cullingResources, u8 frameIndex)
	{
		using BufferUsage = Renderer::BufferPassUsage;

		data.culledDrawCallsBuffer = builder.Write(cullingResources->GetCulledDrawsBuffer(0), BufferUsage::GRAPHICS);
		data.drawCountBuffer = builder.Write(cullingResources->GetDrawCountBuffer(), BufferUsage::TRANSFER | BufferUsage::GRAPHICS);
		data.triangleCountBuffer = builder.Write(cullingResources->GetTriangleCountBuffer(), BufferUsage::TRANSFER | BufferUsage::GRAPHICS);
		data.drawCountReadBackBuffer = builder.Write(cullingResources->GetDrawCountReadBackBuffer(), BufferUsage::TRANSFER);
		data.triangleCountReadBackBuffer = builder.Write(cullingResources->GetTriangleCountReadBackBuffer(), BufferUsage::TRANSFER);

		builder.Read(cullingResources->GetDrawCalls().GetBuffer(), BufferUsage::GRAPHICS);
		builder.Read(cullingResources->GetInstanceRefs().GetBuffer(), BufferUsage::GRAPHICS);

		data.drawSet = builder.Use(cullingResources->GetGeometryPassDescriptorSet());
	}

	struct GeometryPassParams : public PassParams
	{
		Renderer::ImageMutableResource rt0;
		Renderer::ImageMutableResource rt1;
		Renderer::DepthImageMutableResource depth;

		Renderer::BufferMutableResource drawCallsBuffer;
		Renderer::BufferMutableResource culledDrawCallsBuffer;
		Renderer::BufferMutableResource culledDrawCallCountBuffer;

		Renderer::BufferMutableResource drawCountBuffer;
		Renderer::BufferMutableResource triangleCountBuffer;
		Renderer::BufferMutableResource drawCountReadBackBuffer;
		Renderer::BufferMutableResource triangleCountReadBackBuffer;

		Renderer::DescriptorSetResource globalDescriptorSet;
		Renderer::DescriptorSetResource drawDescriptorSet;

		std::function<void(DrawParams&)> drawCallback;

		bool enableDrawing = false; // Allows us to do everything but the actual drawcall, for debugging
		bool cullingEnabled = false;
		bool useInstancedCulling = false;
		u32 numCascades = 0;
	};
	void GeometryPass(GeometryPassParams& params);

	void SyncToGPU();
	void BindCullingResource(CullingResourcesBase& resources);

private:
	void CreatePermanentResources();

protected:
	Renderer::Renderer* _renderer = nullptr;
	DebugRenderer* _debugRenderer = nullptr;

	Renderer::GPUVector<Model::ComplexModel::CullingData> _cullingDatas;

	Renderer::SamplerID _occlusionSampler;
};