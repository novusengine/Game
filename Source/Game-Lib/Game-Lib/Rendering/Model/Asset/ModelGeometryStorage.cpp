#include "ModelGeometryStorage.h"

#include <Renderer/Renderer.h>

#include <cstring>
#include <limits>

namespace
{
    template <typename T>
    bool CanAppendSpan(const Renderer::GPUVector<T>& destination, std::span<const T> source)
    {
        return source.size() <= std::numeric_limits<u32>::max() - destination.Count();
    }

    template <typename T>
    u32 AppendSpan(Renderer::GPUVector<T>& destination, std::span<const T> source)
    {
        if (source.empty())
            return destination.Count();

        const u32 base = destination.AddCount(static_cast<u32>(source.size()));
        std::memcpy(&destination[base], source.data(), source.size_bytes());
        return base;
    }

    template <typename T>
    void Configure(Renderer::GPUVector<T>& buffer, const char* name)
    {
        buffer.SetDebugName(name);
        buffer.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
    }
} // namespace

namespace ModelLoading
{
    ModelGeometryStorage::ModelGeometryStorage(bool validateTransfers)
        : _records(validateTransfers), _meshes(validateTransfers), _meshLODs(validateTransfers), _submeshes(validateTransfers), _meshlets(validateTransfers),
          _positions(validateTransfers), _vertexAttributes(validateTransfers), _skinningData(validateTransfers), _meshletVertexIndices(validateTransfers),
          _meshletTriangles(validateTransfers), _jointPaletteRemaps(validateTransfers), _materialSlots(validateTransfers),
          _embeddedInstanceSets(validateTransfers), _embeddedInstances(validateTransfers)
    {
        Configure(_records, "Model Asset Records");
        Configure(_meshes, "Model Meshes");
        Configure(_meshLODs, "Model Mesh LODs");
        Configure(_submeshes, "Model Submeshes");
        Configure(_meshlets, "Model Meshlets");
        Configure(_positions, "Model Positions");
        Configure(_vertexAttributes, "Model Vertex Attributes");
        Configure(_skinningData, "Model Skinning Data");
        Configure(_meshletVertexIndices, "Model Meshlet Vertex Indices");
        Configure(_meshletTriangles, "Model Meshlet Triangles");
        Configure(_jointPaletteRemaps, "Model Joint Palette Remaps");
        Configure(_materialSlots, "Model Material Slots");
        Configure(_embeddedInstanceSets, "Model Embedded Instance Sets");
        Configure(_embeddedInstances, "Model Embedded Instances");
    }

    bool ModelGeometryStorage::Append(const ModelAssetView& view, u32 materialTableOffset, u32 materialTableCount, RenderAssets::ModelHandle& outHandle)
    {
        if (_records.Count() == std::numeric_limits<u32>::max() || !CanAppendSpan(_meshes, view.meshes) || !CanAppendSpan(_meshLODs, view.meshLODs) ||
            !CanAppendSpan(_submeshes, view.submeshes) || !CanAppendSpan(_meshlets, view.meshlets) || !CanAppendSpan(_positions, view.positions) ||
            !CanAppendSpan(_vertexAttributes, view.vertexAttributes) || !CanAppendSpan(_skinningData, view.skinningData) ||
            !CanAppendSpan(_meshletVertexIndices, view.meshletVertexIndices) || !CanAppendSpan(_meshletTriangles, view.meshletTriangles) ||
            !CanAppendSpan(_jointPaletteRemaps, view.jointPaletteRemaps) || !CanAppendSpan(_materialSlots, view.materialSlots) ||
            !CanAppendSpan(_embeddedInstanceSets, view.embeddedInstanceSets) || !CanAppendSpan(_embeddedInstances, view.embeddedInstances))
            return false;

        ModelGPURecord record;
        record.bounds = view.root.bounds;
        record.collisionAssetID = view.root.collisionAssetID;
        record.meshBase = AppendSpan(_meshes, view.meshes);
        record.numMeshes = static_cast<u32>(view.meshes.size());
        record.meshLODBase = AppendSpan(_meshLODs, view.meshLODs);
        record.numMeshLODs = static_cast<u32>(view.meshLODs.size());
        record.submeshBase = AppendSpan(_submeshes, view.submeshes);
        record.numSubmeshes = static_cast<u32>(view.submeshes.size());
        record.meshletBase = AppendSpan(_meshlets, view.meshlets);
        record.numMeshlets = static_cast<u32>(view.meshlets.size());
        record.positionBase = AppendSpan(_positions, view.positions);
        record.numPositions = static_cast<u32>(view.positions.size());
        record.vertexAttributeBase = AppendSpan(_vertexAttributes, view.vertexAttributes);
        record.numVertexAttributes = static_cast<u32>(view.vertexAttributes.size());
        record.skinningDataBase = AppendSpan(_skinningData, view.skinningData);
        record.numSkinningData = static_cast<u32>(view.skinningData.size());
        record.meshletVertexIndexBase = AppendSpan(_meshletVertexIndices, view.meshletVertexIndices);
        record.numMeshletVertexIndices = static_cast<u32>(view.meshletVertexIndices.size());
        record.meshletTriangleBase = AppendSpan(_meshletTriangles, view.meshletTriangles);
        record.numMeshletTriangles = static_cast<u32>(view.meshletTriangles.size());
        record.jointPaletteRemapBase = AppendSpan(_jointPaletteRemaps, view.jointPaletteRemaps);
        record.numJointPaletteRemaps = static_cast<u32>(view.jointPaletteRemaps.size());
        record.materialSlotBase = AppendSpan(_materialSlots, view.materialSlots);
        record.numMaterialSlots = static_cast<u32>(view.materialSlots.size());
        record.embeddedInstanceSetBase = AppendSpan(_embeddedInstanceSets, view.embeddedInstanceSets);
        record.numEmbeddedInstanceSets = static_cast<u32>(view.embeddedInstanceSets.size());
        record.embeddedInstanceBase = AppendSpan(_embeddedInstances, view.embeddedInstances);
        record.numEmbeddedInstances = static_cast<u32>(view.embeddedInstances.size());
        record.defaultMaterialTableOffset = materialTableOffset;
        record.defaultMaterialTableCount = materialTableCount;
        record.flags = view.root.flags;
        record.geometryGroupCount = view.root.geometryGroupCount;

        outHandle = RenderAssets::ModelHandle(_records.Add(record));
        return outHandle != RenderAssets::ModelHandle::Invalid();
    }

    void ModelGeometryStorage::SyncToGPU(Renderer::Renderer* renderer)
    {
#define SYNC_BUFFER(member) _bufferGrowths += member.SyncToGPU(renderer) ? 1u : 0u
        SYNC_BUFFER(_records);
        SYNC_BUFFER(_meshes);
        SYNC_BUFFER(_meshLODs);
        SYNC_BUFFER(_submeshes);
        SYNC_BUFFER(_meshlets);
        SYNC_BUFFER(_positions);
        SYNC_BUFFER(_vertexAttributes);
        SYNC_BUFFER(_skinningData);
        SYNC_BUFFER(_meshletVertexIndices);
        SYNC_BUFFER(_meshletTriangles);
        SYNC_BUFFER(_jointPaletteRemaps);
        SYNC_BUFFER(_materialSlots);
        SYNC_BUFFER(_embeddedInstanceSets);
        SYNC_BUFFER(_embeddedInstances);
#undef SYNC_BUFFER
    }

    ModelGeometryStorageStats ModelGeometryStorage::GetStats() const
    {
        ModelGeometryStorageStats stats;
        stats.numModels = _records.Count();
        stats.numMeshes = _meshes.Count();
        stats.numMeshLODs = _meshLODs.Count();
        stats.numSubmeshes = _submeshes.Count();
        stats.numMeshlets = _meshlets.Count();
        stats.numPositions = _positions.Count();
#define ADD_BYTES(member)                                                                                                                                        \
    stats.usedBytes += member.UsedBytes();                                                                                                                       \
    stats.reservedBytes += member.TotalBytes()
        ADD_BYTES(_records);
        ADD_BYTES(_meshes);
        ADD_BYTES(_meshLODs);
        ADD_BYTES(_submeshes);
        ADD_BYTES(_meshlets);
        ADD_BYTES(_positions);
        ADD_BYTES(_vertexAttributes);
        ADD_BYTES(_skinningData);
        ADD_BYTES(_meshletVertexIndices);
        ADD_BYTES(_meshletTriangles);
        ADD_BYTES(_jointPaletteRemaps);
        ADD_BYTES(_materialSlots);
        ADD_BYTES(_embeddedInstanceSets);
        ADD_BYTES(_embeddedInstances);
#undef ADD_BYTES
        stats.bufferGrowths = _bufferGrowths;
        return stats;
    }
} // namespace ModelLoading
