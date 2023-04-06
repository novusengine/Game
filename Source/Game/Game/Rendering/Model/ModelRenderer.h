#pragma once
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

constexpr u32 MODEL_INVALID_TEXTURE_ID = 0; // This refers to the debug texture
constexpr u32 MODEL_INVALID_TEXTURE_TRANSFORM_ID = std::numeric_limits<u16>().max();
constexpr u8 MODEL_INVALID_TEXTURE_UNIT_INDEX = std::numeric_limits<u8>().max();

class ModelRenderer
{
public:
	struct ReserveInfo
	{
		u32 numInstances = 0;
		u32 numModels = 0;

		u32 numOpaqueDrawcalls = 0;
		u32 numTransparentDrawcalls = 0;

		u32 numVertices = 0;
		u32 numIndices = 0;

		u32 numTextureUnits = 0;
	};

	struct ModelManifest
	{
		u32 opaqueDrawCallOffset = 0;
		u32 numOpaqueDrawCalls = 0;

		u32 transparentDrawCallOffset = 0;
		u32 numTransparentDrawCalls = 0;

		u32 vertexOffset = 0;
		u32 numVertices = 0;

		u32 indexOffset = 0;
		u32 numIndices = 0;
	};

	struct DrawCallData
	{
		u32 instanceID = 0;
		u32 textureUnitOffset = 0;
		u16 numTextureUnits = 0;
		u16 numUnlitTextureUnits = 0;
	};

	struct InstanceData
	{
		u32 modelID = 0;
		u32 boneDeformOffset;
		u32 boneInstanceDataOffset;
		u32 textureTransformDeformOffset;
		u32 textureTransformInstanceDataOffset;
		u32 modelVertexOffset = 0;
		u32 animatedVertexOffset = 0;
	};

	struct TextureUnit
	{
		u16 data = 0; // Texture Flag + Material Flag + Material Blending Mode
		u16 materialType = 0; // Shader ID
		u32 textureIds[2] = { MODEL_INVALID_TEXTURE_ID, MODEL_INVALID_TEXTURE_ID };
		u16 textureTransformIds[2] = { MODEL_INVALID_TEXTURE_TRANSFORM_ID, MODEL_INVALID_TEXTURE_TRANSFORM_ID };
	};

public:
	ModelRenderer(Renderer::Renderer* renderer, DebugRenderer* debugRenderer);
	~ModelRenderer();

	void Update(f32 deltaTime);
	void Clear();

	void Reserve(const ReserveInfo& reserveInfo);
	u32 LoadModel(const std::string& name, Model::ComplexModel& model);
	u32 AddInstance(u32 modelID, const Terrain::Placement& placement);

	void AddOccluderPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
	void AddCullingPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
	void AddGeometryPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);

	Renderer::DescriptorSet& GetMaterialPassDescriptorSet() { return _materialPassDescriptorSet; }

	Renderer::GPUVector<mat4x4>& GetInstanceMatrices() { return _instanceMatrices; }
	std::vector<ModelManifest> GetModelManifests() { return _modelManifests; }
	u32 GetInstanceIDFromDrawCallID(u32 drawCallID, bool isOpaque);

	// Drawcall stats
	u32 GetNumDrawCalls() { return 0; }
	u32 GetNumOccluderDrawCalls() { return _numOccluderDrawCalls; }
	u32 GetNumSurvivingDrawCalls(u32 viewID) { return _numSurvivingDrawCalls[viewID]; }

	// Triangle stats
	u32 GetNumTriangles() { return 0; }
	u32 GetNumOccluderTriangles() { return _numOccluderDrawCalls * Terrain::CELL_NUM_TRIANGLES; }
	u32 GetNumSurvivingGeometryTriangles(u32 viewID) { return _numSurvivingDrawCalls[viewID] * Terrain::CELL_NUM_TRIANGLES; }

private:
	void CreatePermanentResources();

	void SyncToGPU();

	struct DrawParams
	{
		bool shadowPass = false;
		u32 shadowCascade = 0;
		Renderer::RenderPassMutableResource visibilityBuffer;
		Renderer::RenderPassMutableResource depth;
		Renderer::BufferID argumentBuffer;
		Renderer::BufferID drawCountBuffer;
		u32 drawCountIndex = 0;
		u32 numMaxDrawCalls = 0;
	};
	void Draw(const RenderResources& resources, u8 frameIndex, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, const DrawParams& params);

private:
	PRAGMA_NO_PADDING_START
		// Stuff here
		PRAGMA_NO_PADDING_END

private:
	Renderer::Renderer* _renderer = nullptr;
	DebugRenderer* _debugRenderer = nullptr;

	std::vector<ModelManifest> _modelManifests;
	std::atomic<u32> _modelManifestsIndex = 0;

	std::vector<u32> _modelIDToNumInstances;
	std::mutex _modelIDToNumInstancesMutex;

	Renderer::GPUVector<Model::ComplexModel::Vertex> _vertices;
	std::atomic<u32> _verticesIndex = 0;

	Renderer::GPUVector<u16> _indices;
	std::atomic<u32> _indicesIndex = 0;

	Renderer::GPUVector<InstanceData> _instanceDatas;
	Renderer::GPUVector<mat4x4> _instanceMatrices;
	std::atomic<u32> _instanceIndex = 0;

	Renderer::GPUVector<TextureUnit> _textureUnits;
	std::atomic<u32> _textureUnitIndex = 0;

	Renderer::GPUVector<Renderer::IndexedIndirectDraw> _opaqueDrawCalls;
	Renderer::GPUVector<DrawCallData> _opaqueDrawCallDatas;
	std::atomic<u32> _opaqueDrawCallsIndex = 0;

	Renderer::GPUVector<Renderer::IndexedIndirectDraw> _transparentDrawCalls;
	Renderer::GPUVector<DrawCallData> _transparentDrawCallDatas;
	std::atomic<u32> _transparentDrawCallsIndex = 0;

	// GPU-only workbuffers
	Renderer::BufferID _occluderArgumentBuffer;
	Renderer::BufferID _argumentBuffer;

	Renderer::BufferID _occluderDrawCountReadBackBuffer;
	Renderer::BufferID _drawCountReadBackBuffer;

	FrameResource<Renderer::BufferID, 2> _culledInstanceBitMaskBuffer;
	Renderer::BufferID _culledInstanceBuffer[Renderer::Settings::MAX_VIEWS];

	Renderer::TextureArrayID _textures;

	Renderer::SamplerID _sampler;
	Renderer::SamplerID _occlusionSampler;

	Renderer::DescriptorSet _occluderFillPassDescriptorSet;
	Renderer::DescriptorSet _opaqueCullingDescriptorSet;
	Renderer::DescriptorSet _geometryPassDescriptorSet;
	Renderer::DescriptorSet _materialPassDescriptorSet;

	u32 _numOccluderDrawCalls = 0;
	u32 _numSurvivingDrawCalls[Renderer::Settings::MAX_VIEWS] = { 0 };
};