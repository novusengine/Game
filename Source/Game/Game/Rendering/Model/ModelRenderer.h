#pragma once
#include <Base/Types.h>
#include <Base/Container/SafeUnorderedMap.h>

#include <FileFormat/Models/Model.h>

#include <Renderer/GPUVector.h>

#include <atomic>

namespace Renderer
{
    class RenderGraph;
    class Renderer;
}
struct RenderResources;
class Bytebuffer;

class ModelRenderer
{
public:
    struct DrawCall
    {
    public:
        u32 indexCount = 0;
        u32 instanceCount = 0;
        u32 firstIndex = 0;
        u32 vertexOffset = 0;
        u32 instanceID = 0;
    };

    struct Dispatch
    {
    public:
        u32 x = 0;
        u32 y = 0;
        u32 z = 0;
    };

    struct PaddedDispatch
    {
    public:
        u32 numInstances = 0;
        Dispatch dispatch = {};
    };

    struct ModelData
    {
    public:
        u32 meshletOffset = 0;
        u16 meshletCount = 0;
        u16 textureID = 0;
        u32 indexOffset = 0;
        u32 vertexOffset = 0;
    };

    struct InstanceData
    {
    public:
        u32 modelDataID = 0;
        u32 padding = 0;
    };

    struct SurvivedInstanceData
    {
    public:
        u32 instanceDataID = 0;
        u32 numMeshletsBeforeInstance = 0;
    };

    struct MeshletData
    {
    public:
        u32 indexStart = 0;
        u32 indexCount = 0;
    };

    struct MeshletInstance
    {
    public:
        u32 meshletDataID = 0;
        u32 instanceDataID = 0;
    };

public:
    ModelRenderer(Renderer::Renderer* renderer);

    void Update(f32 deltaTime);

    void AddCullingPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
    void AddGeometryPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);

    u32 AddModel(u32 modelHash, const Model::Header& modelToLoad, std::shared_ptr<Bytebuffer>& buffer);
    u32 AddInstance(u32 modelDataID, vec3 position, quat rotation, vec3 scale);

    Renderer::GPUVector<InstanceData>& GetInstanceDatas() { return _instanceData; }

private:
    void CreatePermanentResources();
    void SyncBuffers();

    void CreateCheckerboardCube(Color color1, Color color2);

private:
    Renderer::Renderer* _renderer;

    Renderer::SamplerID _sampler;

    Renderer::BufferID _indirectDrawArgumentBuffer;
    Renderer::BufferID _indirectDispatchArgumentBuffer;
    Renderer::BufferID _meshletInstanceBuffer;
    Renderer::BufferID _meshletInstanceCountBuffer;

    Renderer::DescriptorSet _cullDescriptorSet;
    Renderer::DescriptorSet _drawDescriptorSet;

    Renderer::GPUVector<vec4> _vertexPositions;
    Renderer::GPUVector<vec4> _vertexNormals;
    Renderer::GPUVector<vec2> _vertexUVs;
    Renderer::GPUVector<u32> _indices;
    Renderer::GPUVector<u32> _culledIndices;
    Renderer::GPUVector<ModelData> _modelData;
    Renderer::GPUVector<InstanceData> _instanceData;
    Renderer::GPUVector<SurvivedInstanceData> _survivedInstanceDatas;
    Renderer::GPUVector<mat4x4> _instanceMatrices;
    Renderer::GPUVector<MeshletData> _meshlets;

    Renderer::TextureArrayID _textures;
private:
    SafeUnorderedMap<u32, u32> _modelHashToModelID;
};