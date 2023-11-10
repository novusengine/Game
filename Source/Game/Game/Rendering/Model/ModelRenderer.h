#pragma once
#include "Game/Rendering/CulledRenderer.h"
#include "Game/Rendering/CullingResources.h"

#include <Base/Types.h>
#include <Base/Math/Geometry.h>

#include <FileFormat/Shared.h>
#include <FileFormat/Novus/Model/ComplexModel.h>

#include <Renderer/DescriptorSet.h>
#include <Renderer/FrameResource.h>
#include <Renderer/GPUBuffer.h>
#include <Renderer/GPUVector.h>

class DebugRenderer;
struct RenderResources;

namespace Renderer
{
	class Renderer;
	class RenderGraph;
	class RenderGraphResources;
    class RenderGraphBuilder;
}

struct DrawParams;

constexpr u32 MODEL_INVALID_TEXTURE_ID = 0; // This refers to the debug texture
constexpr u32 MODEL_INVALID_TEXTURE_TRANSFORM_ID = std::numeric_limits<u16>().max();
constexpr u8 MODEL_INVALID_TEXTURE_UNIT_INDEX = std::numeric_limits<u8>().max();

class ModelRenderer : CulledRenderer
{
public:
	struct ReserveInfo
	{
	public:
		u32 numInstances = 0;
		u32 numModels = 0;

		u32 numOpaqueDrawcalls = 0;
		u32 numTransparentDrawcalls = 0;

		u32 numUniqueOpaqueDrawcalls = 0;
		u32 numUniqueTransparentDrawcalls = 0;

		u32 numVertices = 0;
		u32 numIndices = 0;

		u32 numTextureUnits = 0;

		u32 numBones = 0;

		u32 numDecorationSets = 0;
		u32 numDecorations = 0;
	};

	struct ModelManifest
	{
	public:
		std::string debugName = "";

		u32 opaqueDrawCallTemplateOffset = 0;
		u32 numOpaqueDrawCalls = 0;

		u32 transparentDrawCallTemplateOffset = 0;
		u32 numTransparentDrawCalls = 0;

		u32 opaqueDrawCallOffset = 0;
		u32 transparentDrawCallOffset = 0;

		u32 vertexOffset = 0;
		u32 numVertices = 0;

		u32 indexOffset = 0;
		u32 numIndices = 0;

		u32 numBones = 0;
		u32 numTextureTransforms = 0;

		u32 decorationSetOffset = 0;
		u32 numDecorationSets = 0;

		u32 decorationOffset = 0;
		u32 numDecorations = 0;

		bool isAnimated = false;
	};

	struct DrawCallData
	{
	public:
		u32 instanceID = 0;
		u32 modelID = 0;
		u32 textureUnitOffset = 0;
		u16 numTextureUnits = 0;
		u16 numUnlitTextureUnits = 0;
	};

	struct InstanceData
	{
	public:
		static constexpr u32 InvalidID = std::numeric_limits<u32>().max();

		u32 modelID = 0;
		u32 boneMatrixOffset = InvalidID;
		u32 boneInstanceDataOffset = InvalidID;
		u32 textureTransformDeformOffset = InvalidID;
		u32 textureTransformInstanceDataOffset = InvalidID;
		u32 modelVertexOffset = InvalidID;
		u32 animatedVertexOffset = InvalidID;
	};

	struct InstanceDataCPU
	{
	public:
		u32 numBones = 0;
		u32 numTextureTransforms = 0;
	};

	struct TextureUnit
	{
	public:
		u16 data = 0; // Texture Flag + Material Flag + Material Blending Mode
		u16 materialType = 0; // Shader ID
		u32 textureIds[2] = { MODEL_INVALID_TEXTURE_ID, MODEL_INVALID_TEXTURE_ID };
		u16 textureTransformIds[2] = { MODEL_INVALID_TEXTURE_TRANSFORM_ID, MODEL_INVALID_TEXTURE_TRANSFORM_ID };
	};

	struct PackedAnimatedVertexPositions
	{
		u32 packed0;
		u32 packed1;
	};

public:
	ModelRenderer(Renderer::Renderer* renderer, DebugRenderer* debugRenderer);
	~ModelRenderer();

	void Update(f32 deltaTime);
	void Clear();

	void Reserve(const ReserveInfo& reserveInfo);
	void FitBuffersAfterLoad();
	u32 LoadModel(const std::string& name, Model::ComplexModel& model);
	u32 AddPlacementInstance(u32 modelID, const Terrain::Placement& placement);
	u32 AddInstance(u32 modelID, const mat4x4& transformMatrix);
	void ModifyInstance(u32 instanceID, u32 modelID, const mat4x4& transformMatrix);

	bool AddAnimationInstance(u32 instanceID);
	bool SetBoneMatricesAsDirty(u32 instanceID, u32 localBoneIndex, u32 count, mat4x4* boneMatrixArray);

	void AddOccluderPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
	void AddCullingPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
	void AddGeometryPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);

	void AddTransparencyCullingPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
	void AddTransparencyGeometryPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);

	Renderer::DescriptorSet& GetMaterialPassDescriptorSet() { return _materialPassDescriptorSet; }
	void RegisterMaterialPassBufferUsage(Renderer::RenderGraphBuilder& builder);

	Renderer::GPUVector<mat4x4>& GetInstanceMatrices() { return _instanceMatrices; }
	std::vector<ModelManifest> GetModelManifests() { return _modelManifests; }
	u32 GetInstanceIDFromDrawCallID(u32 drawCallID, bool isOpaque);

	CullingResources<DrawCallData>& GetOpaqueCullingResources() { return _opaqueCullingResources; }
	CullingResources<DrawCallData>& GetTransparentCullingResources() { return _transparentCullingResources; }

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

	void Draw(const RenderResources& resources, u8 frameIndex, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, const DrawParams& params);
	void DrawTransparent(const RenderResources& resources, u8 frameIndex, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, const DrawParams& params);

private:
	PRAGMA_NO_PADDING_START
		// Stuff here
	PRAGMA_NO_PADDING_END

private:
	Renderer::Renderer* _renderer = nullptr;
	DebugRenderer* _debugRenderer = nullptr;

	std::mutex _textureLoadMutex;

	std::vector<ModelManifest> _modelManifests;
	std::atomic<u32> _modelManifestsIndex = 0;

	std::vector<Model::ComplexModel::DecorationSet> _modelDecorationSets;
	std::atomic<u32> _modelDecorationSetsIndex = 0;

	std::vector<Model::ComplexModel::Decoration> _modelDecorations;
	std::atomic<u32> _modelDecorationsIndex = 0;

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

	Renderer::GPUVector<mat4x4> _boneMatrices;
	std::atomic<u32> _boneMatrixIndex = 0;

	std::vector<Renderer::IndexedIndirectDraw> _modelOpaqueDrawCallTemplates;
	std::vector<DrawCallData> _modelOpaqueDrawCallDataTemplates;
	std::atomic<u32> _modelOpaqueDrawCallTemplateIndex = 0;

	std::vector<Renderer::IndexedIndirectDraw> _modelTransparentDrawCallTemplates;
	std::vector<DrawCallData> _modelTransparentDrawCallDataTemplates;
	std::atomic<u32> _modelTransparentDrawCallTemplateIndex = 0;

	CullingResources<DrawCallData> _opaqueCullingResources;
	CullingResources<DrawCallData> _transparentCullingResources;

	// GPU-only workbuffers
	Renderer::BufferID _occluderArgumentBuffer;
	Renderer::BufferID _argumentBuffer;

	Renderer::GPUBuffer<PackedAnimatedVertexPositions> _animatedVertices;
	std::atomic<u32> _animatedVerticesIndex = 0;

	Renderer::TextureArrayID _textures;

	Renderer::SamplerID _sampler;
	Renderer::SamplerID _occlusionSampler;

	Renderer::DescriptorSet _materialPassDescriptorSet;

	u32 _numOccluderDrawCalls = 0;
	u32 _numSurvivingDrawCalls[Renderer::Settings::MAX_VIEWS] = { 0 };
};
