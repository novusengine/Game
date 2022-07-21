#pragma once
#include <Base/Types.h>

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
        u32 indexCount;
        u32 instanceCount;
        u32 firstIndex;
        u32 vertexOffset;
        u32 instanceID;
    };

    struct InstanceData
    {
    public:
        u32 meshletOffset = 0;
        u32 meshletCount = 0;
        u32 indexOffset = 0;
        u32 padding = 0;
    };

    struct Meshlet
    {
    public:
        u32 indexStart = 0;
        u32 indexCount = 0;
        u32 instanceDataID = 0;
        u32 padding = 0;
    };

public:
    ModelRenderer(Renderer::Renderer* renderer);

    void AddModel(const Model::Header& modelToLoad, std::shared_ptr<Bytebuffer>& buffer, vec3 position, quat rotation, vec3 scale);

    void Update(f32 deltaTime);

    void AddCullingPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
    void AddGeometryPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);

private:
    void CreatePermanentResources();
    void SyncBuffers();

private:
    Renderer::Renderer* _renderer;

    Renderer::BufferID _culledIndexBuffer;
    Renderer::BufferID _indirectArgumentBuffer;

    Renderer::DescriptorSet _cullDescriptorSet;
    Renderer::DescriptorSet _drawDescriptorSet;

    Renderer::GPUVector<vec4> _vertexPositions;
    Renderer::GPUVector<vec4> _vertexNormals;
    Renderer::GPUVector<vec2> _vertexUVs;
    Renderer::GPUVector<u32> _indices;
    Renderer::GPUVector<Meshlet> _meshlets;
    Renderer::GPUVector<InstanceData> _instanceData;
    Renderer::GPUVector<mat4x4> _instanceMatrices;
};