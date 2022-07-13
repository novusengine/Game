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
        u32 meshletOffset;
        u32 meshletCount;
        u32 indexOffset;
        u32 padding;
    };

    struct Vertex
    {
    public:
        vec3 position;
        vec3 normal;
        vec2 uv;
    };

    struct Meshlet
    {
    public:
        u32 indexStart = 0;
        u32 indexCount = 0;
    };

public:
    ModelRenderer(Renderer::Renderer* renderer);

    void AddModel(const Model::Header& modelToLoad, std::shared_ptr<Bytebuffer>& buffer);

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

    Renderer::GPUVector<Vertex> _vertices;
    Renderer::GPUVector<u32> _indices;
    Renderer::GPUVector<Meshlet> _meshlets;
    Renderer::GPUVector<InstanceData> _instanceData;
};