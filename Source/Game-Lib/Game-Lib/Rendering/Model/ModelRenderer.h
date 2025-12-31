#pragma once
#include "Game-Lib/Rendering/CulledRenderer.h"
#include "Game-Lib/Rendering/CullingResources.h"

#include <Base/Types.h>
#include <Base/Container/ConcurrentQueue.h>
#include <Base/Math/Geometry.h>

#include <FileFormat/Shared.h>
#include <FileFormat/Novus/Model/ComplexModel.h>

#include <Renderer/DescriptorSet.h>
#include <Renderer/FrameResource.h>
#include <Renderer/GPUBuffer.h>
#include <Renderer/GPUVector.h>

#include <entt/fwd.hpp>
#include <robinhood/robinhood.h>

class DebugRenderer;
class GameRenderer;
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
constexpr u32 MODEL_INVALID_TEXTURE_DATA_ID = std::numeric_limits<u32>().max();
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

        u32 numVertices = 0;
        u32 numIndices = 0;

        u32 numTextureUnits = 0;

        u32 numBones = 0;
        u32 numTextureTransforms = 0;

        u32 numDecorationSets = 0;
        u32 numDecorations = 0;
    };

    struct ModelManifest
    {
    public:
        std::string debugName = "";

        u32 opaqueDrawCallOffset = 0;
        u32 numOpaqueDrawCalls = 0;

        u32 transparentDrawCallOffset = 0;
        u32 numTransparentDrawCalls = 0;

        bool hasTemporarilyTransparentDrawCalls = false;
        u32 temporarilyTransparentDrawCallOffset = 0; // Opaque renderbatches set up in the transparent resources

        bool hasSkyboxDrawCalls = false;
        u32 opaqueSkyboxDrawCallOffset = 0;
        u32 transparentSkyboxDrawCallOffset = 0;

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

        robin_hood::unordered_map<u32, u32> opaqueDrawIDToTextureDataID;
        robin_hood::unordered_map<u32, u32> transparentDrawIDToTextureDataID;
        robin_hood::unordered_map<u32, u32> opaqueSkyboxDrawIDToTextureDataID;
        robin_hood::unordered_map<u32, u32> transparentSkyboxDrawIDToTextureDataID;

        robin_hood::unordered_map<u32, u32> opaqueDrawIDToGroupID;
        robin_hood::unordered_map<u32, u32> transparentDrawIDToGroupID;
        robin_hood::unordered_map<u32, u32> opaqueSkyboxDrawIDToGroupID;
        robin_hood::unordered_map<u32, u32> transparentSkyboxDrawIDToGroupID;

        robin_hood::unordered_set<u32> instances;
        robin_hood::unordered_set<u32> skyboxInstances;
        robin_hood::unordered_set<u32> originallyTransparentDrawIDs;
    };

    struct InstanceManifest
    {
    public:
        u32 modelID = 0;
        u64 displayInfoPacked = std::numeric_limits<u64>().max();
        bool isDynamic = false;
        bool visible = true;
        bool transparent = false;
        bool skybox = false;

        robin_hood::unordered_set<u32> enabledGroupIDs;
    };

    struct DisplayInfoManifest
    {
    public:
        bool overrideTextureDatas = false;
        bool hasTemporarilyTransparentDrawCalls = false;
        bool hasSkyboxDrawCalls = false;

        std::vector<u64> skinTextureUnits;
        std::vector<u64> hairTextureUnits;

        robin_hood::unordered_map<u32, u32> opaqueDrawIDToTextureDataID;
        robin_hood::unordered_map<u32, u32> transparentDrawIDToTextureDataID;

        robin_hood::unordered_map<u32, u32> opaqueSkyboxDrawIDToTextureDataID;
        robin_hood::unordered_map<u32, u32> transparentSkyboxDrawIDToTextureDataID;
    };

    struct DrawCallData
    {
    public:
        u32 baseInstanceLookupOffset = 0;
        u32 modelID = 0;
    };

    struct TextureData
    {
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
        u32 textureTransformMatrixOffset = InvalidID;
        u32 modelVertexOffset = InvalidID;
        u32 animatedVertexOffset = InvalidID;
        f32 opacity = 1.0f;
        f32 highlightIntensity = 1.0f;
        u32 padding0;
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
        u32 rgba = 0xFFFFFFFF;
        u32 padding0; 
        u32 padding1;
        u32 padding2;
    };

    struct PackedAnimatedVertexPositions
    {
    public:
        u32 packed0;
        u32 packed1;
    };

    struct TextureLoadRequest
    {
    public:
        u32 textureUnitOffset = 0;
        u32 textureIndex = 0;
        u32 textureHash = 0;
    };

    struct ChangeGroupRequest
    {
    public:
        u32 instanceID = 0;
        u32 groupIDStart = 0;
        u32 groupIDEnd = 0;
        bool enable = false;
    };

    struct ChangeSkinTextureRequest
    {
    public:
        u32 instanceID = 0;
        Renderer::TextureID textureID;
    };

    struct ChangeHairTextureRequest
    {
    public:
        u32 instanceID = 0;
        Renderer::TextureID textureID;
    };

    struct ChangeVisibilityRequest
    {
    public:
        u32 instanceID = 0;
        bool visible = false;
    };

    struct ChangeTransparencyRequest
    {
    public:
        u32 instanceID = 0;
        bool transparent = false;
        f32 opacity = 1.0f;
    };

    struct ChangeHighlightRequest
    {
    public:
        u32 instanceID = 0;
        f32 highlightIntensity = 1.0f;
    };

    struct ChangeTextureUnitColorRequest
    {
    public:
        u32 instanceID = 0;
        u32 textureUnitIndex = 0;
        Color color = Color::White;
    };

    struct ChangeSkyboxRequest
    {
    public:
        u32 instanceID = 0;
        bool skybox = false;
    };

private:
        struct ModelOffsets
        {
        public:
            u32 modelIndex = 0;
            u32 verticesStartIndex = 0;
            u32 indicesStartIndex = 0;

            u32 decorationSetStartIndex = 0;
            u32 decorationStartIndex = 0;
        };

        struct TextureDataOffsets
        {
            u32 textureDatasStartIndex = 0;
        };

        struct TextureUnitOffsets
        {
        public:
            u32 textureUnitsStartIndex = 0;
        };

        struct AnimationOffsets
        {
        public:
            u32 boneStartIndex = 0;
            u32 textureTransformStartIndex = 0;
        };

        struct InstanceOffsets
        {
        public:
            u32 instanceIndex = 0;
        };

        struct DrawCallOffsets
        {
        public:
            u32 opaqueDrawCallStartIndex = 0;
            u32 transparentDrawCallStartIndex = 0;
        };

public:
    ModelRenderer(Renderer::Renderer* renderer, GameRenderer* gameRenderer, DebugRenderer* debugRenderer);
    ~ModelRenderer();

    void Update(f32 deltaTime);
    void Clear();

    void Reserve(const ReserveInfo& reserveInfo);

    Renderer::TextureID LoadTexture(const std::string& path, u32& arrayIndex);

    u32 LoadModel(const std::string& name, Model::ComplexModel& model);
    u32 AddPlacementInstance(entt::entity entityID, u32 modelID, u32 modelHash, Model::ComplexModel* model, const vec3& position, const quat& rotation, f32 scale, u32 doodadSet, bool canUseDoodadSet);
    u32 AddInstance(entt::entity entityID, u32 modelID, Model::ComplexModel* model, const mat4x4& transformMatrix, u64 displayInfoPacked = std::numeric_limits<u64>().max());
    void RemoveInstance(u32 instanceID);
    void ModifyInstance(entt::entity entityID, u32 instanceID, u32 modelID, Model::ComplexModel* model, const mat4x4& transformMatrix, u64 displayInfoPacked = std::numeric_limits<u64>().max());
    void ReplaceTextureUnits(entt::entity entityID, u32 modelID, Model::ComplexModel* model, u32 instanceID, u64 displayInfoPacked);

    void RequestChangeGroup(u32 instanceID, u32 groupIDStart, u32 groupIDEnd, bool enable);
    void RequestChangeSkinTexture(u32 instanceID, Renderer::TextureID textureID);
    void RequestChangeHairTexture(u32 instanceID, Renderer::TextureID textureID);
    void RequestChangeVisibility(u32 instanceID, bool visible);
    void RequestChangeTransparency(u32 instanceID, bool transparent, f32 opacity);
    void RequestChangeHighlight(u32 instanceID, f32 highlightIntensity);
    void RequestChangeSkybox(u32 instanceID, bool skybox);

    bool AddUninstancedAnimationData(u32 modelID, u32& boneMatrixOffset, u32& textureTransformMatrixOffset);
    bool SetInstanceAnimationData(u32 instanceID, u32 boneMatrixOffset, u32 textureTransformMatrixOffset);
    bool SetUninstancedBoneMatricesAsDirty(u32 modelID, u32 boneMatrixOffset, u32 localBoneIndex, u32 count, const mat4x4* boneMatrixArray);
    bool SetUninstancedTextureTransformMatricesAsDirty(u32 modelID, u32 textureTransformMatrixOffset, u32 localTextureTransformIndex, u32 count, const mat4x4* textureTransformMatrixArray);

    bool AddAnimationInstance(u32 instanceID);
    bool SetBoneMatricesAsDirty(u32 instanceID, u32 localBoneIndex, u32 count, const mat4x4* boneMatrixArray);
    bool SetTextureTransformMatricesAsDirty(u32 instanceID, u32 localTextureTransformIndex, u32 count, const mat4x4* boneMatrixArray);

    void AddSkyboxPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);

    void AddOccluderPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
    void AddCullingPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
    void AddGeometryPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);

    void AddTransparencyCullingPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
    void AddTransparencyGeometryPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);

    void RegisterMaterialPassBufferUsage(Renderer::RenderGraphBuilder& builder);

    Renderer::GPUVector<mat4x4>& GetInstanceMatrices() { return _instanceMatrices; }
    const std::vector<ModelManifest>& GetModelManifests() { return _modelManifests; }

    CullingResourcesIndexed<DrawCallData>& GetOpaqueCullingResources() { return _opaqueCullingResources; }
    CullingResourcesIndexed<DrawCallData>& GetTransparentCullingResources() { return _transparentCullingResources; }

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
    void CreateModelPipelines();
    void InitDescriptorSets();

    void AllocateModel(const Model::ComplexModel& model, ModelOffsets& offsets);
    void AllocateTextureData(u32 numTextureDatas, TextureDataOffsets& offsets);
    void AllocateTextureUnits(const Model::ComplexModel& model, TextureUnitOffsets& offsets);
    void AllocateAnimation(u32 modelID, AnimationOffsets& offsets);
    void AllocateInstance(u32 modelID, InstanceOffsets& offsets);
    void AllocateDrawCalls(u32 modelID, DrawCallOffsets& offsets);

    void DeallocateAnimation(u32 boneStartIndex, u32 numBones, u32 textureTransformStartIndex, u32 numTextureTransforms);

    void MakeInstanceTransparent(u32 instanceID, InstanceManifest& instanceManifest, ModelManifest& modelManifest);
    void MakeInstanceSkybox(u32 instanceID, InstanceManifest& instanceManifest, bool skybox);

    void CompactInstanceRefs();
    void SyncToGPU();

    void Draw(const RenderResources& resources, u8 frameIndex, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, const DrawParams& params);
    void DrawTransparent(const RenderResources& resources, u8 frameIndex, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, const DrawParams& params);
    void DrawSkybox(const RenderResources& resources, u8 frameIndex, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList, const DrawParams& params, bool isTransparent);

private:
    Renderer::Renderer* _renderer = nullptr;
    GameRenderer* _gameRenderer = nullptr;
    DebugRenderer* _debugRenderer = nullptr;

    std::mutex _textureLoadMutex;

    std::vector<ModelManifest> _modelManifests;
    std::vector<std::unique_ptr<std::mutex>> _modelManifestsInstancesMutexes;

    std::vector<InstanceManifest> _instanceManifests;

    std::mutex _displayInfoManifestsMutex;
    robin_hood::unordered_map<u64, DisplayInfoManifest> _displayInfoManifests; // The u64 is a PackedDisplayInfo
    robin_hood::unordered_map<u64, DisplayInfoManifest> _uniqueDisplayInfoManifests;

    // If ModelData Flag == 0x4 and no CreatureExtraModelData always use unique DisplayInfoManifest
    // If CreatureExtraModelData share between all instances with the same PackedDisplayInfo and CreatureExtraModelData
    // If neither of above share between all instances with the same PackedDisplayInfo

    std::vector<Model::ComplexModel::DecorationSet> _modelDecorationSets;
    std::vector<Model::ComplexModel::Decoration> _modelDecorations;

    std::vector<u32> _modelIDToNumInstances;
    std::mutex _modelIDToNumInstancesMutex;

    Renderer::GPUVector<Model::ComplexModel::Vertex> _vertices;
    Renderer::GPUVector<u16> _indices;

    Renderer::GPUVector<InstanceData> _instanceDatas;
    Renderer::GPUVector<mat4x4> _instanceMatrices;

    Renderer::GPUVector<TextureUnit> _textureUnits;
    Renderer::GPUVector<TextureData> _textureDatas;

    Renderer::GPUVector<mat4x4> _boneMatrices;
    Renderer::GPUVector<mat4x4> _textureTransformMatrices;

    CullingResourcesIndexed<DrawCallData> _opaqueCullingResources;
    CullingResourcesIndexed<DrawCallData> _transparentCullingResources;

    CullingResourcesIndexed<DrawCallData> _opaqueSkyboxCullingResources;
    CullingResourcesIndexed<DrawCallData> _transparentSkyboxCullingResources;

    Renderer::GraphicsPipelineID _drawPipeline;
    Renderer::GraphicsPipelineID _drawShadowPipeline;
    Renderer::GraphicsPipelineID _drawTransparentPipeline;
    Renderer::GraphicsPipelineID _drawSkyboxOpaquePipeline;
    Renderer::GraphicsPipelineID _drawSkyboxTransparentPipeline;

    // GPU-only workbuffers
    Renderer::BufferID _occluderArgumentBuffer;
    Renderer::BufferID _argumentBuffer;

    Renderer::GPUBuffer<PackedAnimatedVertexPositions> _animatedVertices;
    std::atomic<u32> _animatedVerticesIndex = 0;

    Renderer::TextureArrayID _textures;

    std::vector<Renderer::SamplerID> _samplers;
    Renderer::SamplerID _occlusionSampler;

    u32 _numOccluderDrawCalls = 0;
    u32 _numSurvivingDrawCalls[Renderer::Settings::MAX_VIEWS] = { 0 };

    moodycamel::ConcurrentQueue<TextureLoadRequest> _textureLoadRequests;
    std::vector<TextureLoadRequest> _textureLoadWork;
    robin_hood::unordered_set<u32> _dirtyTextureUnitOffsets;

    moodycamel::ConcurrentQueue<ChangeGroupRequest> _changeGroupRequests;
    std::vector<ChangeGroupRequest> _changeGroupWork;

    moodycamel::ConcurrentQueue<ChangeSkinTextureRequest> _changeSkinTextureRequests;
    std::vector<ChangeSkinTextureRequest> _changeSkinTextureWork;

    moodycamel::ConcurrentQueue<ChangeHairTextureRequest> _changeHairTextureRequests;
    std::vector<ChangeHairTextureRequest> _changeHairTextureWork;

    moodycamel::ConcurrentQueue<ChangeVisibilityRequest> _changeVisibilityRequests;
    std::vector<ChangeVisibilityRequest> _changeVisibilityWork;

    moodycamel::ConcurrentQueue<ChangeTransparencyRequest> _changeTransparencyRequests;
    std::vector<ChangeTransparencyRequest> _changeTransparencyWork;

    moodycamel::ConcurrentQueue<ChangeHighlightRequest> _changeHighlightRequests;
    std::vector<ChangeHighlightRequest> _changeHighlightWork;

    moodycamel::ConcurrentQueue<ChangeSkyboxRequest> _changeSkyboxRequests;
    std::vector<ChangeSkyboxRequest> _changeSkyboxWork;

    std::atomic_bool _instancesDirty = false;

    std::mutex _modelOffsetsMutex;
    std::mutex _textureDataOffsetsMutex;
    std::mutex _textureOffsetsMutex;
    std::mutex _animationOffsetsMutex;
    std::mutex _instanceOffsetsMutex;
    std::mutex _drawCallOffsetsMutex;
};
